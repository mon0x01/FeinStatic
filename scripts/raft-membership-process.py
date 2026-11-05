#!/usr/bin/env python3
"""Four-process authenticated TCP joint-consensus membership transition lab."""
from __future__ import annotations
import os
import pathlib
import re
import signal
import socket
import subprocess
import tempfile
import time


class MembershipLab:
    def __init__(self, binary: pathlib.Path, helper: pathlib.Path, root: pathlib.Path):
        self.binary = binary
        self.helper = helper
        self.root = root
        self.root.mkdir(parents=True, exist_ok=True)
        self.ports = [free_port() for _ in range(4)]
        self.processes: list[subprocess.Popen | None] = [None] * 4
        self.logs = [open(root / f"node-{i}.log", "w", buffering=1) for i in range(4)]
        self.values: dict[str, str] = {}
        try:
            result = subprocess.run([str(helper), str(root)], text=True, capture_output=True, timeout=20)
            if result.returncode:
                raise RuntimeError(f"preparation failed:\n{result.stdout}\n{result.stderr}")
            for line in result.stdout.splitlines():
                if "=" in line:
                    key, value = line.split("=", 1)
                    self.values[key] = value
            required = ["OBSERVER_ID", *(f"NODE_{i}_ID" for i in range(4))]
            missing = [key for key in required if key not in self.values]
            if missing:
                raise RuntimeError(f"lab helper omitted metadata: {missing}")
            self._write_member_files()
        except BaseException:
            self.close()
            raise

    def _write_member_files(self) -> None:
        for local in range(4):
            rows = []
            for peer in range(4):
                role = "voter" if peer < 3 else "learner"
                rows.append(f"{self.values[f'NODE_{peer}_ID']} 127.0.0.1 {self.ports[peer]} {role}")
            (self.root / f"members-{local}.txt").write_text("\n".join(rows) + "\n")

    def start(self, i: int) -> None:
        current = self.processes[i]
        if current and current.poll() is None:
            raise RuntimeError(f"node {i} is already running")
        args = [str(self.binary), "raft-node", str(self.root / f"node-{i}.db"),
                "127.0.0.1", str(self.ports[i]), str(self.root / f"node-{i}.identity"),
                str(self.root / f"node-{i}.trust"), "feinstatic-v1.4-membership-lab",
                str(self.root / f"members-{i}.txt")]
        self.processes[i] = subprocess.Popen(args, stdout=self.logs[i], stderr=subprocess.STDOUT,
                                             start_new_session=True)

    def stop(self, i: int, force: bool = False) -> None:
        process = self.processes[i]
        if not process or process.poll() is not None:
            return
        try:
            os.killpg(process.pid, signal.SIGKILL if force else signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait(timeout=3)

    def status(self, i: int) -> dict[str, str]:
        args = [str(self.binary), "raft-status-auth", str(self.root / "observer.db"),
                "127.0.0.1", str(self.ports[i]), str(self.root / "observer.identity"),
                str(self.root / "observer.trust"), self.values[f"NODE_{i}_ID"]]
        result = subprocess.run(args, text=True, capture_output=True, timeout=4)
        if result.returncode:
            raise RuntimeError(result.stderr.strip() or result.stdout.strip() or f"status failed for node {i}")
        fields = {}
        for key in ("Role", "Leader", "Term", "Commit index", "Applied index", "Log index",
                    "Ledger height", "Ledger tip", "Membership", "Voters", "Learners"):
            match = re.search(rf"^\s*{re.escape(key)}\s*:\s*(.*?)\s*$", result.stdout, re.M)
            if match:
                fields[key] = match.group(1)
        if len(fields) != 11:
            raise RuntimeError(f"incomplete Raft status for node {i}:\n{result.stdout}")
        return fields

    def wait_for(self, predicate, description: str, timeout: float = 30.0):
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

    def statuses(self):
        return {i: self.status(i) for i in range(4)}

    def leader(self, indices=(0, 1, 2, 3)) -> tuple[int, dict[str, str]] | None:
        states = {i: self.status(i) for i in indices}
        leaders = [(i, state) for i, state in states.items()
                   if state["Role"] == "leader" and state["Leader"] == self.values[f"NODE_{i}_ID"]]
        return leaders[0] if len(leaders) == 1 else None

    def propose(self, i: int) -> int:
        args = [str(self.binary), "raft-propose-block-auth", str(self.root / "observer.db"),
                "127.0.0.1", str(self.ports[i]), str(self.root / "observer.identity"),
                str(self.root / "observer.trust"), self.values[f"NODE_{i}_ID"], self.values["MINER_ADDRESS"]]
        result = subprocess.run(args, text=True, capture_output=True, timeout=8)
        if result.returncode:
            raise RuntimeError(result.stderr.strip() or result.stdout.strip())
        match = re.search(r"Log index\s*:\s*(\d+)", result.stdout)
        if not match:
            raise RuntimeError(f"block proposal did not return a log index:\n{result.stdout}")
        return int(match.group(1))

    def durable_snapshot_index(self, i: int) -> int:
        import sqlite3
        database = self.root / f"node-{i}.db"
        with sqlite3.connect(f"file:{database}?mode=ro", uri=True, timeout=3.0) as connection:
            row = connection.execute("SELECT COALESCE(MAX(snapshot_index), 0) FROM raft_snapshots").fetchone()
        return int(row[0]) if row else 0

    def wait_converged(self, indices, minimum_index: int, timeout: float = 14.0):
        def check():
            states = {i: self.status(i) for i in indices}
            if any(int(s["Commit index"]) < minimum_index or int(s["Applied index"]) < minimum_index
                   for s in states.values()):
                return None
            if len({s["Ledger height"] for s in states.values()}) != 1:
                return None
            if len({s["Ledger tip"] for s in states.values()}) != 1:
                return None
            return states
        return self.wait_for(check, f"nodes {tuple(indices)} converge through Raft index {minimum_index}", timeout)

    def change(self, leader_index: int, voters: list[str]) -> None:
        # The separate observer identity is explicitly trusted by each node and
        # can administer membership without being a consensus voter.
        args = [str(self.binary), "raft-change-members-auth", str(self.root / "observer.db"),
                "127.0.0.1", str(self.ports[leader_index]), str(self.root / "observer.identity"),
                str(self.root / "observer.trust"), self.values[f"NODE_{leader_index}_ID"],
                ",".join(voters)]
        result = subprocess.run(args, text=True, capture_output=True, timeout=8)
        if result.returncode:
            raise RuntimeError(result.stderr.strip() or result.stdout.strip())

    def change_when_replication_acknowledged(self, voter_builder, timeout: float = 20.0):
        """Retry only safe pre-append rejections while the leader observes learner ACKs.

        A follower can apply the leader's latest committed index before the leader
        processes the corresponding AppendResponse and advances match_index. The
        Raft core correctly rejects promotion during that small window. This lab
        waits for that acknowledgement; it never bypasses the core's catch-up gate.
        The voter list is rebuilt if leadership changes while waiting.
        """
        deadline = time.monotonic() + timeout
        last_error = None
        while time.monotonic() < deadline:
            current = self.leader()
            if current is None:
                time.sleep(0.2)
                continue
            leader_index, _ = current
            voters = voter_builder(leader_index)
            try:
                self.change(leader_index, voters)
                return leader_index, voters
            except RuntimeError as error:
                message = str(error)
                if "new Raft voter is not caught up to the leader's committed index" not in message:
                    raise
                last_error = message
                # Let an in-flight authenticated AppendResponse update the
                # leader's match_index. The next attempt is a fresh RPC.
                time.sleep(0.25)
        raise TimeoutError(
            "learner did not reach the leader's acknowledged replication index "
            f"within {timeout:.1f}s; last rejection={last_error}"
        )

    def wait_configuration(self, voters: list[str], learners: list[str], timeout: float = 35.0):
        wanted_voters = ", ".join(sorted(voters))
        wanted_learners = ", ".join(sorted(learners))
        def check():
            states = self.statuses()
            if all(s["Membership"] == "stable" and s["Voters"] == wanted_voters and
                   s["Learners"] == wanted_learners for s in states.values()):
                return states
            return None
        return self.wait_for(check, f"stable voters={wanted_voters}, learners={wanted_learners}", timeout)

    def close(self) -> None:
        for i in range(4):
            self.stop(i)
        for log in getattr(self, "logs", []):
            log.close()


def free_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def run(binary: pathlib.Path, helper: pathlib.Path, workspace: pathlib.Path) -> None:
    lab = MembershipLab(binary, helper, workspace)
    try:
        print("[1/8] Start three voters and one pre-provisioned learner as separate OS processes")
        for i in range(4):
            lab.start(i)
        leader_index, leader_status = lab.wait_for(lab.leader, "initial leader election")
        print(f"      initial leader node {leader_index}, term {leader_status['Term']}")

        print("[2/8] Keep the learner offline while voters commit through a compacted snapshot")
        lab.stop(3)
        last_index = 0
        for offset in range(136):
            active_leader, _ = lab.wait_for(lambda: lab.leader((0, 1, 2)), "three-voter leader", timeout=12.0)
            last_index = lab.propose(active_leader)
            lab.wait_converged((0, 1, 2), last_index)
            if (offset + 1) % 32 == 0:
                print(f"      committed {offset + 1}/136 blocks with learner offline")
        source_snapshot_index = lab.durable_snapshot_index(active_leader)
        if source_snapshot_index <= 0:
            raise RuntimeError("voter majority did not compact while learner was offline")
        print(f"      leader persisted compacted snapshot at Raft index {source_snapshot_index}")

        print("[3/8] Restart learner and verify snapshot catch-up before promotion")
        lab.start(3)
        caught = lab.wait_converged((0, 1, 2, 3), last_index, timeout=75.0)
        learner_snapshot_index = lab.durable_snapshot_index(3)
        if learner_snapshot_index < source_snapshot_index:
            raise RuntimeError(f"learner did not install the leader snapshot: leader={source_snapshot_index}, learner={learner_snapshot_index}")
        print(f"      learner installed snapshot {learner_snapshot_index} and caught up at index {caught[3]['Applied index']}")

        print("[4/8] Propose C_old,new that removes the current leader and promotes the learner")
        old_voters = [lab.values[f"NODE_{i}_ID"] for i in range(3)]
        learner_id = lab.values["NODE_3_ID"]
        leader_index, new_voters = lab.change_when_replication_acknowledged(
            lambda current_leader: [peer for peer in old_voters
                                    if peer != lab.values[f"NODE_{current_leader}_ID"]] + [learner_id]
        )
        leader_id = lab.values[f"NODE_{leader_index}_ID"]
        removed = [leader_id]
        states = lab.wait_configuration(new_voters, removed)
        print("      all four processes committed stable C_new; removed leader became a learner")

        if states[leader_index]["Role"] != "follower":
            raise RuntimeError("removed leader remained eligible for leadership after final configuration")
        elected_after_change = lab.wait_for(lab.leader, "new leader election after removed leader steps down", timeout=20.0)
        if elected_after_change[0] == leader_index:
            raise RuntimeError("removed leader regained leadership after its voter removal")
        print(f"      new voter node {elected_after_change[0]} elected term {elected_after_change[1]['Term']}")

        print("[5/8] Kill and restart the removed voter; verify durable learner recovery")
        lab.stop(leader_index, force=True)
        lab.start(leader_index)
        recovered = lab.wait_configuration(new_voters, removed)
        if recovered[leader_index]["Role"] != "follower":
            raise RuntimeError("restarted removed voter incorrectly became leader/candidate")
        print("      restarted process recovered C_new from SQLite and remained a learner")

        print("[6/8] Re-promote the recovered learner after catch-up (reverse transition)")
        leader = lab.wait_for(lab.leader, "leader before reverse transition")
        def removed_learner_caught_up():
            leader_state = lab.status(leader[0])
            removed_state = lab.status(leader_index)
            return removed_state if int(removed_state["Applied index"]) >= int(leader_state["Commit index"]) else None
        lab.wait_for(removed_learner_caught_up, "removed learner catch-up before re-promotion")
        reverse_leader, _ = lab.change_when_replication_acknowledged(
            lambda _current_leader: old_voters
        )
        final = lab.wait_configuration(old_voters, [lab.values["NODE_3_ID"]])
        print("      all four processes converged after second joint-consensus transition")

        print("[7/8] Verify the final configuration elects exactly one leader among original voters")
        elected_final = lab.wait_for(lab.leader, "new election after learner is demoted", timeout=20.0)
        if elected_final[0] == 3:
            raise RuntimeError("demoted learner regained leadership after the final configuration")
        final = lab.statuses()
        if final[3]["Role"] != "follower":
            raise RuntimeError("learner became leader without promotion")
        print(f"      node {elected_final[0]} elected; one leader, three voters, one learner")

        print("[8/8] Joint-consensus authenticated TCP membership process lab: PASS")
        print(f"Workspace: {workspace}")
        print("Logs: node-0.log through node-3.log")
    finally:
        lab.close()


def main() -> int:
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=pathlib.Path)
    parser.add_argument("--prepare-helper", required=True, type=pathlib.Path)
    parser.add_argument("--workspace", type=pathlib.Path)
    args = parser.parse_args()
    binary, helper = args.binary.resolve(), args.prepare_helper.resolve()
    if not binary.is_file() or not os.access(binary, os.X_OK):
        parser.error(f"binary is not executable: {binary}")
    if not helper.is_file() or not os.access(helper, os.X_OK):
        parser.error(f"prepare helper is not executable: {helper}")
    workspace = args.workspace.resolve() if args.workspace else pathlib.Path(tempfile.mkdtemp(prefix="feinstatic-raft-membership-"))
    try:
        run(binary, helper, workspace)
        return 0
    except KeyboardInterrupt:
        print(f"Membership process lab interrupted; workspace retained at {workspace}", file=__import__('sys').stderr)
        return 130
    except Exception as error:
        print(f"RAFT MEMBERSHIP PROCESS LAB: FAIL: {error}", file=__import__('sys').stderr)
        print(f"Workspace retained at {workspace}", file=__import__('sys').stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
