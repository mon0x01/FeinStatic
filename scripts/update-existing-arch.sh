#!/usr/bin/env bash
set -Eeuo pipefail

# Non-destructive v1.9.0 peer lifecycle supervision update for an existing Arch Linux FeinStatic checkout.
PROJECT_ROOT="${FEINSTATIC_PROJECT_ROOT:-$HOME/Desktop/CXX}"
PROJECT="$PROJECT_ROOT/FeinStatic"
ARCHIVE="${1:-$HOME/Downloads/FeinStatic-1.9.0-source.tar.gz}"
BACKUP_PARENT="$PROJECT_ROOT/.FeinStatic-backups"
STAMP="$(date +%Y%m%d-%H%M%S)"
BACKUP="$BACKUP_PARENT/FeinStatic-before-1.9.0-$STAMP"
STAGE=""
UPDATE_STARTED=0
UPDATE_COMPLETE=0

cleanup() {
    [[ -z "$STAGE" || ! -d "$STAGE" ]] || rm -rf -- "$STAGE"
}

on_error() {
    local rc="${1:-1}"
    trap - ERR
    if [[ "$UPDATE_STARTED" == 1 && "$UPDATE_COMPLETE" != 1 && -d "$BACKUP" ]]; then
        echo "
Update did not complete. Restoring the complete pre-update checkout and runtime..." >&2
        rm -rf -- "$PROJECT"
        if ! cp -a -- "$BACKUP" "$PROJECT"; then
            echo "CRITICAL: automatic restore failed. The untouched backup remains at: $BACKUP" >&2
        else
            echo "Rollback completed. Original checkout restored from: $BACKUP" >&2
        fi
    fi
    echo "UPDATE FAILED (exit $rc). Backup: ${BACKUP:-not-created}" >&2
    exit "$rc"
}

trap cleanup EXIT
trap 'on_error "$?"' ERR
fail() { echo "ERROR: $*" >&2; on_error 1; }

[[ -d "$PROJECT" && -f "$PROJECT/CMakeLists.txt" ]] || fail "Existing FeinStatic checkout not found at: $PROJECT"
grep -q 'project(FeinStatic VERSION 1.8.0' "$PROJECT/CMakeLists.txt" || fail "This updater requires an existing FeinStatic 1.8.0 checkout."
[[ -f "$ARCHIVE" ]] || fail "Source archive not found: $ARCHIVE"
for command in pacman cmake ninja gcc g++ clang++ git tar sha256sum python3 pgrep; do
    command -v "$command" >/dev/null 2>&1 || fail "Required command is missing: $command"
done

ARCHIVE_DIR="$(cd "$(dirname "$ARCHIVE")" && pwd)"
CHECKSUM_FILE="$ARCHIVE.sha256"
[[ -f "$CHECKSUM_FILE" ]] || fail "Checksum file is required beside the archive: $CHECKSUM_FILE"
EXPECTED_SHA256="${FEINSTATIC_EXPECTED_SOURCE_SHA256:-}"
[[ "$EXPECTED_SHA256" =~ ^[0-9a-fA-F]{64}$ ]] || fail "A pinned source SHA-256 must be supplied through FEINSTATIC_EXPECTED_SOURCE_SHA256."
ACTUAL_SHA256="$(sha256sum "$ARCHIVE" | cut -d' ' -f1)"
[[ "$ACTUAL_SHA256" == "$EXPECTED_SHA256" ]] || fail "Source archive does not match the pinned SHA-256."
echo "Verifying v1.9.0 source archive checksum..."
(cd "$ARCHIVE_DIR" && sha256sum -c "$(basename "$CHECKSUM_FILE")")
echo "Pinned SHA-256: PASS"

REQUIRED_PACKAGES=(gcc binutils make cmake ninja clang git openssl sqlite boost python)
MISSING_PACKAGES=()
for package in "${REQUIRED_PACKAGES[@]}"; do
    if ! pacman -Q "$package" >/dev/null 2>&1; then MISSING_PACKAGES+=("$package"); fi
done
if ((${#MISSING_PACKAGES[@]} > 0)); then
    if [[ -e /var/lib/pacman/db.lck ]] || pgrep -x pacman >/dev/null 2>&1 || \
       pgrep -x yay >/dev/null 2>&1 || pgrep -x paru >/dev/null 2>&1 || \
       pgrep -x pikaur >/dev/null 2>&1; then
        fail "Dependencies are missing while a package transaction appears active. Missing: ${MISSING_PACKAGES[*]}"
    fi
    echo "Installing missing Arch packages: ${MISSING_PACKAGES[*]}"
    sudo pacman -S --needed "${MISSING_PACKAGES[@]}"
else
    echo "All required Arch packages are already installed; skipping pacman."
fi

RUNNING_FEINSTATIC="$(pgrep -x feinstatic || true)"
[[ -z "$RUNNING_FEINSTATIC" ]] || { printf 'Running FeinStatic PID(s):\n%s\n' "$RUNNING_FEINSTATIC" >&2; fail "Exit FeinStatic before updating."; }
echo "No FeinStatic process is running; continuing with the update."

DB_PATH="$PROJECT/runtime/feinstatic-fresh.db"
ENDPOINT_REGISTRY_PATH="$DB_PATH.endpoints"
DB_HASH_BEFORE=""
ENDPOINT_REGISTRY_HASH_BEFORE=""
DB_BASELINE_JSON=""
if [[ -f "$DB_PATH" ]]; then
    DB_HASH_BEFORE="$(sha256sum "$DB_PATH" | cut -d' ' -f1)"
    DB_BASELINE_JSON="$(python3 - "$DB_PATH" <<'PY'
import hashlib
import json
import sqlite3
import struct
import sys

path = sys.argv[1]
connection = sqlite3.connect(path, timeout=10)
connection.execute("PRAGMA busy_timeout=10000")
tables = sorted(row[0] for row in connection.execute(
    "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'"
))

def digest_tables(names):
    digest = hashlib.sha256()
    for table in names:
        quoted = '"' + table.replace('"', '""') + '"'
        schema = connection.execute(
            "SELECT type, name, tbl_name, sql FROM sqlite_master WHERE tbl_name=? ORDER BY type, name",
            (table,),
        ).fetchall()
        digest.update(b'T' + table.encode('utf-8') + b'\0')
        for item in schema:
            digest.update(b'S')
            for value in item:
                raw = b'' if value is None else str(value).encode('utf-8')
                digest.update(len(raw).to_bytes(8, 'big') + raw)
        columns = [row[1] for row in connection.execute(f'PRAGMA table_info({quoted})')]
        if not columns:
            raise RuntimeError(f"table disappeared or has no columns: {table}")
        try:
            cursor = connection.execute(f'SELECT * FROM {quoted} ORDER BY rowid')
        except sqlite3.OperationalError:
            order = ', '.join('"' + c.replace('"', '""') + '"' for c in columns)
            cursor = connection.execute(f'SELECT * FROM {quoted} ORDER BY {order}')
        for row in cursor:
            digest.update(b'R')
            for value in row:
                if value is None:
                    digest.update(b'N')
                elif isinstance(value, int):
                    raw = str(value).encode('ascii')
                    digest.update(b'I' + len(raw).to_bytes(8, 'big') + raw)
                elif isinstance(value, float):
                    digest.update(b'F' + struct.pack('!d', value))
                elif isinstance(value, bytes):
                    digest.update(b'B' + len(value).to_bytes(8, 'big') + value)
                else:
                    raw = str(value).encode('utf-8')
                    digest.update(b'T' + len(raw).to_bytes(8, 'big') + raw)
    return digest.hexdigest()

print(json.dumps({'tables': tables, 'digest': digest_tables(tables)}, separators=(',', ':')))
connection.close()
PY
)"
    echo "Captured a logical fingerprint of ${#DB_BASELINE_JSON} bytes for the existing database tables."
fi
if [[ -f "$ENDPOINT_REGISTRY_PATH" ]]; then
    ENDPOINT_REGISTRY_HASH_BEFORE="$(sha256sum "$ENDPOINT_REGISTRY_PATH" | cut -d' ' -f1)"
    echo "Captured existing endpoint-registry hash; it will be preserved."
fi

mkdir -p "$BACKUP_PARENT"
echo "Creating full pre-update backup: $BACKUP"
cp -a -- "$PROJECT" "$BACKUP"
UPDATE_STARTED=1
STAGE="$(mktemp -d "$PROJECT_ROOT/.FeinStatic-stage.XXXXXX")"
echo "Inspecting archive paths..."
mapfile -t ARCHIVE_ENTRIES < <(tar -tzf "$ARCHIVE")
((${#ARCHIVE_ENTRIES[@]} > 0)) || fail "Archive is empty."
for entry in "${ARCHIVE_ENTRIES[@]}"; do
    [[ "$entry" != /* && "/$entry/" != *"/../"* && "$entry" == FeinStatic/* ]] || fail "Unsafe or unexpected archive path: $entry"
done

echo "Extracting and validating v1.9.0 source archive..."
tar --no-same-owner -xzf "$ARCHIVE" -C "$STAGE"
SOURCE="$STAGE/FeinStatic"
[[ -f "$SOURCE/CMakeLists.txt" && -d "$SOURCE/src" && -d "$SOURCE/include" ]] || fail "Archive is not a valid FeinStatic source tree."
[[ ! -e "$SOURCE/.git" && ! -e "$SOURCE/build" && ! -e "$SOURCE/runtime" ]] || fail "Archive must not contain local Git/build/runtime data."
grep -q 'project(FeinStatic VERSION 1.9.0' "$SOURCE/CMakeLists.txt" || fail "Archive is not FeinStatic 1.9.0."
[[ -f "$SOURCE/src/network/peer_lifecycle.cpp" && -f "$SOURCE/include/feinstat/network/peer_lifecycle.hpp" ]] || fail "Archive is missing the peer lifecycle implementation."
[[ -x "$SOURCE/scripts/raft-process-chaos.py" && -x "$SOURCE/scripts/raft-membership-process.py" && -x "$SOURCE/scripts/verify-all.sh" && -x "$SOURCE/scripts/verify-concurrency-tsan.sh" && -f "$SOURCE/tools/raft_lab_prepare.cpp" ]] || fail "Archive is missing its verification/process-lab files."

printf 'Merging v1.9.0 source files (Git history and runtime data preserved)...\n'
cp -a -- "$SOURCE/." "$PROJECT/"
echo "Removing generated build cache only; .git and runtime remain untouched..."
rm -rf -- "$PROJECT/build"

if [[ -n "$DB_HASH_BEFORE" ]]; then
    DB_HASH_AFTER="$(sha256sum "$DB_PATH" | cut -d' ' -f1)"
    [[ "$DB_HASH_BEFORE" == "$DB_HASH_AFTER" ]] || fail "Existing Ledger database file changed during source integration."
    echo "Verified: existing Ledger database file is unchanged before verification."
fi
if [[ -n "$ENDPOINT_REGISTRY_HASH_BEFORE" ]]; then
    ENDPOINT_REGISTRY_HASH_AFTER="$(sha256sum "$ENDPOINT_REGISTRY_PATH" | cut -d' ' -f1)"
    [[ "$ENDPOINT_REGISTRY_HASH_BEFORE" == "$ENDPOINT_REGISTRY_HASH_AFTER" ]] || fail "Existing peer endpoint registry changed during source integration."
    echo "Verified: existing peer endpoint registry is unchanged before verification."
fi

cd "$PROJECT"
echo
echo "========== FEINSTATIC v1.9.0 FULL VERIFICATION =========="
./scripts/verify-all.sh

echo
echo "========== EXISTING DATABASE VERIFICATION =========="
if [[ -f "$DB_PATH" ]]; then
    ./build/release/feinstatic verify "$DB_PATH"
    if [[ -n "$DB_BASELINE_JSON" ]]; then
        DB_BASELINE_JSON="$DB_BASELINE_JSON" python3 - "$DB_PATH" <<'PY'
import hashlib
import json
import sqlite3
import struct
import sys

path = sys.argv[1]
baseline = json.loads(__import__('os').environ['DB_BASELINE_JSON'])
connection = sqlite3.connect(path, timeout=10)
connection.execute("PRAGMA busy_timeout=10000")

def digest_tables(names):
    digest = hashlib.sha256()
    for table in names:
        quoted = '"' + table.replace('"', '""') + '"'
        exists = connection.execute(
            "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?", (table,)
        ).fetchone()
        if not exists:
            raise RuntimeError(f"pre-existing database table is missing after update: {table}")
        schema = connection.execute(
            "SELECT type, name, tbl_name, sql FROM sqlite_master WHERE tbl_name=? ORDER BY type, name",
            (table,),
        ).fetchall()
        digest.update(b'T' + table.encode('utf-8') + b'\0')
        for item in schema:
            digest.update(b'S')
            for value in item:
                raw = b'' if value is None else str(value).encode('utf-8')
                digest.update(len(raw).to_bytes(8, 'big') + raw)
        columns = [row[1] for row in connection.execute(f'PRAGMA table_info({quoted})')]
        if not columns:
            raise RuntimeError(f"pre-existing database table has no columns: {table}")
        try:
            cursor = connection.execute(f'SELECT * FROM {quoted} ORDER BY rowid')
        except sqlite3.OperationalError:
            order = ', '.join('"' + c.replace('"', '""') + '"' for c in columns)
            cursor = connection.execute(f'SELECT * FROM {quoted} ORDER BY {order}')
        for row in cursor:
            digest.update(b'R')
            for value in row:
                if value is None:
                    digest.update(b'N')
                elif isinstance(value, int):
                    raw = str(value).encode('ascii')
                    digest.update(b'I' + len(raw).to_bytes(8, 'big') + raw)
                elif isinstance(value, float):
                    digest.update(b'F' + struct.pack('!d', value))
                elif isinstance(value, bytes):
                    digest.update(b'B' + len(value).to_bytes(8, 'big') + value)
                else:
                    raw = str(value).encode('utf-8')
                    digest.update(b'T' + len(raw).to_bytes(8, 'big') + raw)
    return digest.hexdigest()

actual = digest_tables(baseline['tables'])
connection.close()
if actual != baseline['digest']:
    raise SystemExit('Pre-existing SQLite table schema/data fingerprint changed during the v1.9.0 update.')
print(f"Verified: all {len(baseline['tables'])} pre-existing SQLite tables retain their schema and row data.")
print("No v1.9.0 SQLite schema migration is expected.")
PY
    fi
else
    echo "No existing runtime database found at $DB_PATH; project verification completed without a user DB check."
fi

if [[ -n "$ENDPOINT_REGISTRY_HASH_BEFORE" ]]; then
    ENDPOINT_REGISTRY_HASH_AFTER="$(sha256sum "$ENDPOINT_REGISTRY_PATH" | cut -d' ' -f1)"
    [[ "$ENDPOINT_REGISTRY_HASH_BEFORE" == "$ENDPOINT_REGISTRY_HASH_AFTER" ]] || fail "Existing peer endpoint registry changed during verification."
    echo "Verified: existing peer endpoint registry remains unchanged after verification."
fi

UPDATE_COMPLETE=1
printf '\n==============================================\n'
printf ' FEINSTATIC 1.9.0 UPDATE COMPLETED\n'
printf '==============================================\n'
printf 'Project: %s\n' "$PROJECT"
printf 'Backup:  %s\n' "$BACKUP"
printf 'Git:     %s\n' "$(git -C "$PROJECT" branch --show-current 2>/dev/null || echo 'not initialized')"
printf 'Git history and runtime database were preserved; existing SQLite table schemas and row data were checked.\n'
printf '==============================================\n'
