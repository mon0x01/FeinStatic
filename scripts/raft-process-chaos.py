#!/usr/bin/env python3
"""Multi-process Raft failure lab. Uses only the Python standard library."""
from __future__ import annotations
import argparse
import os
import pathlib
import re
import signal
import socket
import sqlite3
import subprocess
import sys
import tempfile
import threading
import time
from typing import Optional


class TcpFaultProxy:
    """A directed TCP link. Stopping it drops that direction; restart heals it."""
    def __init__(self, listen_port: int, target_port: int, name: str, delay_seconds: float = 0.0):
        self.listen_port = listen_port
        self.target_port = target_port
        self.name = name
        self.delay_seconds = delay_seconds
        self.listener: Optional[socket.socket] = None
        self.thread: Optional[threading.Thread] = None
        self.stopping = threading.Event()
        self.connections: set[socket.socket] = set()
        self.lock = threading.Lock()

    def start(self) -> None:
        if self.thread and self.thread.is_alive():
            return
        self.stopping.clear()
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            listener.bind(("127.0.0.1", self.listen_port))
            listener.listen(32)
            listener.settimeout(0.2)
        except BaseException:
            listener.close()
            raise
        self.listener = listener
        self.thread = threading.Thread(target=self._accept_loop, name=self.name, daemon=True)
        self.thread.start()

    def _accept_loop(self) -> None:
        assert self.listener is not None
        while not self.stopping.is_set():
            try:
                downstream, _ = self.listener.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            try:
                upstream = socket.create_connection(("127.0.0.1", self.target_port), timeout=1.5)
                downstream.settimeout(None)
                upstream.settimeout(None)
            except OSError:
                downstream.close()
                continue
            with self.lock:
                self.connections.update((downstream, upstream))
            threading.Thread(target=self._relay, args=(downstream, upstream), daemon=True).start()
            threading.Thread(target=self._relay, args=(upstream, downstream), daemon=True).start()

    def _relay(self, source: socket.socket, destination: socket.socket) -> None:
        delayed = False
        try:
            while not self.stopping.is_set():
                data = source.recv(65536)
                if not data:
                    break
                # Add a deterministic one-time connection delay. Delaying every
                # read can turn normal TCP fragmentation into artificial RPC
                # timeouts and makes the lab needlessly flaky.
                if self.delay_seconds and not delayed:
                    time.sleep(self.delay_seconds)
                    delayed = True
                destination.sendall(data)
        except OSError:
            pass
        finally:
            for item in (source, destination):
                try:
                    item.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                try:
                    item.close()
                except OSError:
                    pass
                with self.lock:
                    self.connections.discard(item)

    def stop(self) -> None:
        self.stopping.set()
        if self.listener:
            try:
                self.listener.close()
            except OSError:
                pass
            self.listener = None
        with self.lock:
            active = list(self.connections)
        for item in active:
            try:
                item.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            try:
                item.close()
            except OSError:
                pass
        if self.thread:
            self.thread.join(timeout=2)
            self.thread = None


class Lab:
    def __init__(self, binary: pathlib.Path, helper: pathlib.Path, root: pathlib.Path):
        self.binary = binary
        self.helper = helper
        self.root = root
        self.root.mkdir(parents=True, exist_ok=True)
        self.ports = [free_port() for _ in range(3)]
        self.proxy_ports = {(i, j): free_port() for i in range(3) for j in range(3) if i != j}
        self.proxies: dict[tuple[int, int], TcpFaultProxy] = {}
        self.processes: list[Optional[subprocess.Popen]] = [None, None, None]
        self.logs = [open(root / f"node-{i}.log", "w", buffering=1) for i in range(3)]
        self.values: dict[str, str] = {}
        try:
            self._prepare()
            self._write_members()
            for (i, j), port in self.proxy_ports.items():
                # One directed path carries artificial latency to exercise delayed RPC delivery.
                delay = 0.010 if (i, j) == (0, 1) else 0.0
                proxy = TcpFaultProxy(port, self.ports[j], f"proxy-{i}-to-{j}", delay)
                proxy.start()
                self.proxies[(i, j)] = proxy
        except BaseException:
            self.close()
            raise

    def _prepare(self) -> None:
        result = subprocess.run([str(self.helper), str(self.root)], text=True,
                                capture_output=True, timeout=20)
        if result.returncode:
            raise RuntimeError(f"lab preparation failed:\n{result.stdout}\n{result.stderr}")
        for line in result.stdout.splitlines():
            if "=" in line:
                key, value = line.split("=", 1)
                self.values[key] = value
        required = ["MINER_ADDRESS", "OBSERVER_ID"] + [f"NODE_{i}_ID" for i in range(3)]
        if any(key not in self.values for key in required):
            raise RuntimeError(f"lab helper returned incomplete metadata:\n{result.stdout}")

    def _write_members(self) -> None:
        for i in range(3):
            lines = []
            for j in range(3):
                port = self.ports[j] if i == j else self.proxy_ports[(i, j)]
                lines.append(f"{self.values[f'NODE_{j}_ID']} 127.0.0.1 {port}")
            (self.root / f"members-{i}.txt").write_text("\n".join(lines) + "\n")

    def start_node(self, i: int) -> None:
        if self.processes[i] and self.processes[i].poll() is None:
            raise RuntimeError(f"node {i} is already running")
        args = [str(self.binary), "raft-node", str(self.root / f"node-{i}.db"),
                "127.0.0.1", str(self.ports[i]), str(self.root / f"node-{i}.identity"),
                str(self.root / f"node-{i}.trust"), "feinstatic-v1.2-process-chaos",
                str(self.root / f"members-{i}.txt")]
        self.processes[i] = subprocess.Popen(args, stdout=self.logs[i], stderr=subprocess.STDOUT,
                                             start_new_session=True)

    def stop_node(self, i: int, force: bool = False) -> None:
        process = self.processes[i]
        if not process or process.poll() is not None:
            return
        requested_signal = signal.SIGKILL if force else signal.SIGTERM
        try:
            # Each node starts a new session; terminate its process group so a
            # future helper/worker child cannot outlive the test harness.
            os.killpg(process.pid, requested_signal)
        except ProcessLookupError:
            pass
        try:
            process.wait(timeout=4)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait(timeout=3)

    def durable_snapshot_index(self, i: int) -> int:
        """Read the durable snapshot object index from a stopped or live node DB."""
        database = self.root / f"node-{i}.db"
        with sqlite3.connect(f"file:{database}?mode=ro", uri=True, timeout=3.0) as connection:
            row = connection.execute(
                "SELECT COALESCE(MAX(snapshot_index), 0) FROM raft_snapshots"
            ).fetchone()
        return int(row[0]) if row else 0

    def status(self, i: int) -> dict[str, str]:
        args = [str(self.binary), "raft-status-auth", str(self.root / "observer.db"),
                "127.0.0.1", str(self.ports[i]), str(self.root / "observer.identity"),
                str(self.root / "observer.trust"), self.values[f"NODE_{i}_ID"]]
        result = subprocess.run(args, text=True, capture_output=True, timeout=3)
        if result.returncode:
            raise RuntimeError(result.stderr.strip() or result.stdout.strip() or f"status failed for node {i}")
        fields = {}
        for key in ("Role", "Leader", "Term", "Commit index", "Applied index", "Log index", "Ledger height", "Ledger tip"):
            match = re.search(rf"^\s*{re.escape(key)}\s*:\s*(.*?)\s*$", result.stdout, re.M)
            if match:
                fields[key] = match.group(1)
        if len(fields) != 8:
            raise RuntimeError(f"could not parse status from node {i}:\n{result.stdout}")
        return fields

    def wait_for(self, predicate, description: str, timeout: float = 18.0):
        deadline = time.monotonic() + timeout
        last = None
        while time.monotonic() < deadline:
            try:
                last = predicate()
                if last:
                    return last
            except (RuntimeError, subprocess.TimeoutExpired, OSError):
                pass
            time.sleep(0.25)
        raise TimeoutError(f"timed out waiting for {description}; last observation={last}")

    def statuses(self, indices=(0, 1, 2)) -> dict[int, dict[str, str]]:
        result = {}
        for i in indices:
            result[i] = self.status(i)
        return result

    def leader(self, indices=(0, 1, 2)) -> tuple[int, dict[str, str]]:
        statuses = self.statuses(indices)
        leaders = [(i, s) for i, s in statuses.items()
                   if s["Role"] == "leader" and s["Leader"] == self.values[f"NODE_{i}_ID"]]
        if len(leaders) != 1:
            return None
        return leaders[0]

    def propose(self, i: int) -> int:
        args = [str(self.binary), "raft-propose-block-auth", str(self.root / "observer.db"),
                "127.0.0.1", str(self.ports[i]), str(self.root / "observer.identity"),
                str(self.root / "observer.trust"), self.values[f"NODE_{i}_ID"],
                self.values["MINER_ADDRESS"]]
        result = subprocess.run(args, text=True, capture_output=True, timeout=6)
        if result.returncode:
            raise RuntimeError(result.stderr.strip() or result.stdout.strip())
        match = re.search(r"Log index\s*:\s*(\d+)", result.stdout)
        if not match:
            raise RuntimeError(f"proposal did not return a log index:\n{result.stdout}")
        return int(match.group(1))

    def wait_converged(self, minimum_index: int, indices=(0, 1, 2), timeout: float = 18.0):
        last_states = None
        def check():
            nonlocal last_states
            states = self.statuses(indices)
            last_states = states
            if any(int(s["Commit index"]) < minimum_index or int(s["Applied index"]) < minimum_index
                   for s in states.values()):
                return None
            tips = {s["Ledger tip"] for s in states.values()}
            heights = {s["Ledger height"] for s in states.values()}
            if len(tips) == 1 and len(heights) == 1:
                return states
            return None
        try:
            return self.wait_for(check, f"all nodes converge through Raft index {minimum_index}", timeout)
        except TimeoutError as error:
            raise TimeoutError(f"{error}; last node states={last_states}") from error

    def close(self) -> None:
        for i in range(3):
            self.stop_node(i)
        for proxy in self.proxies.values():
            proxy.stop()
        for log in self.logs:
            log.close()


def free_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def run_lab(binary: pathlib.Path, helper: pathlib.Path, workspace: pathlib.Path) -> None:
    lab = Lab(binary, helper, workspace)
    try:
        print("[1/9] Starting three independent FeinStatic OS processes (one directed link delayed by 10 ms)")
        for i in range(3):
            lab.start_node(i)
        lab.wait_for(lambda: all(lab.processes[i].poll() is None for i in range(3)), "all nodes stay alive")
        elected = lab.wait_for(lab.leader, "initial three-node leader election")
        leader_index, leader_status = elected
        print(f"      elected node {leader_index}, term {leader_status['Term']}")

        print("[2/9] Committing a Ledger block through the three-process cluster")
        first_index = lab.propose(leader_index)
        first = lab.wait_converged(first_index)
        print(f"      committed/applied Raft index {first_index}; Ledger height {first[0]['Ledger height']}")

        print("[3/9] SIGKILL current leader; verify majority failover")
        lab.stop_node(leader_index, force=True)
        survivors = tuple(i for i in range(3) if i != leader_index)
        replacement_index, replacement = lab.wait_for(lambda: lab.leader(survivors), "replacement leader election")
        print(f"      replacement node {replacement_index}, term {replacement['Term']}")

        print("[4/9] Commit another block with former leader process down")
        second_index = lab.propose(replacement_index)
        survivor_state = lab.wait_converged(second_index, survivors)
        print(f"      majority committed/applied index {second_index}")

        print("[5/9] Restart former leader from its own durable SQLite database")
        lab.start_node(leader_index)
        recovered = lab.wait_converged(second_index, timeout=24.0)
        print(f"      restarted node caught up; all Ledger heights={recovered[0]['Ledger height']}")

        print("[6/9] Partition the current leader from both peers in both directions")
        current = lab.wait_for(lab.leader, "leader before partition")
        isolated_index = current[0]
        majority = tuple(i for i in range(3) if i != isolated_index)
        for key in ((isolated_index, majority[0]), (isolated_index, majority[1]),
                    (majority[0], isolated_index), (majority[1], isolated_index)):
            lab.proxies[key].stop()
        time.sleep(2.2)
        isolated_before = lab.status(isolated_index)
        isolated_proposal = lab.propose(isolated_index)
        time.sleep(2.0)
        isolated_after = lab.status(isolated_index)
        if int(isolated_after["Commit index"]) > int(isolated_before["Commit index"]):
            raise RuntimeError("isolated minority leader advanced commit index without a majority")
        print(f"      isolated node appended index {isolated_proposal} but did not commit it")

        print("[7/9] Verify the connected majority elects a leader and commits independently")
        majority_leader_index, majority_leader = lab.wait_for(lambda: lab.leader(majority), "majority partition leader")
        majority_index = lab.propose(majority_leader_index)
        majority_state = lab.wait_converged(majority_index, majority)
        print(f"      majority committed/applied index {majority_index}; term {majority_leader['Term']}")

        print("[8/9] Heal partition and verify conflict repair plus full convergence")
        for key in ((isolated_index, majority[0]), (isolated_index, majority[1]),
                    (majority[0], isolated_index), (majority[1], isolated_index)):
            lab.proxies[key].start()
        final = lab.wait_converged(majority_index, timeout=28.0)
        terms = {s["Term"] for s in final.values()}
        commits = {s["Commit index"] for s in final.values()}
        applied = {s["Applied index"] for s in final.values()}
        logs = {s["Log index"] for s in final.values()}
        if len(terms) != 1:
            raise RuntimeError(f"nodes report inconsistent terms after healing: {terms}")
        leaders = [i for i, status in final.items()
                   if status["Role"] == "leader" and status["Leader"] == lab.values[f"NODE_{i}_ID"]]
        if len(leaders) != 1:
            raise RuntimeError(f"expected exactly one elected leader after healing; found {leaders}")
        if len(commits) != 1 or len(applied) != 1 or len(logs) != 1:
            raise RuntimeError("Raft indexes did not converge after partition healing: "
                               f"commit={commits}, applied={applied}, log={logs}")
        if logs != commits:
            raise RuntimeError(f"uncommitted log suffix remains after healing: log={logs}, commit={commits}")
        print("      all three processes converged on one committed Ledger tip; no uncommitted suffix remains")

        print("[9/9] Exercise real TCP snapshot transfer to a lagging OS process")
        pre_snapshot_leader, _ = lab.wait_for(lab.leader, "leader before snapshot catch-up")
        lagging_index = next(i for i in range(3) if i != pre_snapshot_leader)
        active_nodes = tuple(i for i in range(3) if i != lagging_index)
        lab.stop_node(lagging_index)
        last_snapshot_catchup_index = 0
        # The default production config compacts after 128 applied entries. Keep
        # one member offline while the other two commit enough entries to force
        # compaction, then restart it so the leader must use InstallSnapshot.
        for offset in range(136):
            active_leader, _ = lab.wait_for(
                lambda: lab.leader(active_nodes), "two-node majority leader", timeout=12.0
            )
            last_snapshot_catchup_index = lab.propose(active_leader)
            lab.wait_converged(last_snapshot_catchup_index, active_nodes, timeout=12.0)
            if (offset + 1) % 32 == 0:
                print(f"      committed {offset + 1}/136 blocks with node {lagging_index} offline")

        leader_snapshot_index = lab.durable_snapshot_index(active_leader)
        if leader_snapshot_index <= 0:
            raise RuntimeError("leader did not persist a Raft application snapshot after the compaction threshold")
        print(f"      leader has durable compacted snapshot at Raft index {leader_snapshot_index}")
        lab.start_node(lagging_index)
        caught_up = lab.wait_converged(last_snapshot_catchup_index, timeout=75.0)
        follower_snapshot_index = lab.durable_snapshot_index(lagging_index)
        if follower_snapshot_index < leader_snapshot_index:
            raise RuntimeError(
                "restarted process converged without installing the leader snapshot: "
                f"leader snapshot={leader_snapshot_index}, follower snapshot={follower_snapshot_index}"
            )
        print(
            f"      restarted process installed snapshot {follower_snapshot_index} and caught up; "
            f"all Ledger heights={caught_up[0]['Ledger height']}"
        )
        print("      snapshot transfer verified over authenticated TCP between independent OS processes")
        print("\nRAFT MULTI-PROCESS CHAOS LAB: PASS")
        print(f"Workspace: {workspace}")
        print("Node logs: node-0.log, node-1.log, node-2.log")
    finally:
        lab.close()


def _handle_termination(signum, _frame) -> None:
    # Raise through run_lab so its finally block terminates child processes
    # and closes all proxy sockets before the harness exits.
    raise KeyboardInterrupt(f"received signal {signum}")


def main() -> int:
    signal.signal(signal.SIGINT, _handle_termination)
    signal.signal(signal.SIGTERM, _handle_termination)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=pathlib.Path)
    parser.add_argument("--prepare-helper", required=True, type=pathlib.Path)
    parser.add_argument("--workspace", type=pathlib.Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    helper = args.prepare_helper.resolve()
    if not binary.is_file() or not os.access(binary, os.X_OK):
        parser.error(f"FeinStatic executable not found or not executable: {binary}")
    if not helper.is_file() or not os.access(helper, os.X_OK):
        parser.error(f"Raft lab preparation helper not found or not executable: {helper}")
    workspace = args.workspace.resolve() if args.workspace else pathlib.Path(tempfile.mkdtemp(prefix="feinstatic-raft-chaos-"))
    try:
        run_lab(binary, helper, workspace)
        return 0
    except KeyboardInterrupt as error:
        print(f"\nRAFT MULTI-PROCESS CHAOS LAB: INTERRUPTED: {error}", file=sys.stderr)
        print(f"Preserved workspace for diagnosis: {workspace}", file=sys.stderr)
        return 130
    except Exception as error:
        print(f"\nRAFT MULTI-PROCESS CHAOS LAB: FAIL: {error}", file=sys.stderr)
        print(f"Preserved workspace for diagnosis: {workspace}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
