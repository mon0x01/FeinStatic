#!/usr/bin/env bash
#
# Regression tests for fst-team process/session lifecycle management.
#
# Tests cover:
#   - is_agent_busy: correctly identifies BUSY / IDLE / COMPLETED / FAILED / STALE
#   - cmd_cleanup: reconciles stale/crashed process state
#
# Approach: source the full fst-team script but override `main` to a no-op
# so we can call individual functions directly.
#
# Usage: bash test_lifecycle.sh
#

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
FST_TEAM="$SCRIPT_DIR/../fst-team"
PASS=0
FAIL=0

pass() { echo "  PASS: $1"; PASS=$((PASS + 1)); }
fail() { echo "  FAIL: $1"; FAIL=$((FAIL + 1)); }

# Use a temporary state directory so we don't disturb real state.
TEST_STATE_DIR="/tmp/fst-team-test-state-$$"

# -------------------------------------------------------------------
# Override main to a no-op before sourcing, so the script doesn't
# execute its command dispatcher on source.
# -------------------------------------------------------------------
main() { : ; }
export -f main 2>/dev/null || true

# Source the script. It defines all functions but main() is now a no-op.
# shellcheck source=../fst-team
source "$FST_TEAM"

# -------------------------------------------------------------------
# Helper: set up minimal state directory structure
# -------------------------------------------------------------------
setup_state() {
  rm -rf "$TEST_STATE_DIR"
  mkdir -p "$TEST_STATE_DIR"/{agents,sessions,tasks,logs,locks}
}

# -------------------------------------------------------------------
# Helper: write agent state JSON
# -------------------------------------------------------------------
write_agent_state() {
  local agent="$1" pid="$2" task_id="$3" session_id="$4" status="$5" exit_code="$6"
  local ec_field
  if [ -z "$exit_code" ]; then
    ec_field='""'
  else
    ec_field="\"$exit_code\""
  fi
  cat > "$TEST_STATE_DIR/agents/$agent.json" <<EOF
{
  "session_id": "$session_id",
  "pid": "$pid",
  "log_file": "$TEST_STATE_DIR/logs/${agent}.log",
  "task": "test task",
  "task_id": "$task_id",
  "status": "$status",
  "started_at": "2026-01-01T00:00:00Z",
  "exit_code": $ec_field
}
EOF
}

# -------------------------------------------------------------------
# Helper: write session state JSON
# -------------------------------------------------------------------
write_session_state() {
  local session_id="$1" agent="$2" task="$3" task_id="$4" pid="$5" status="$6"
  cat > "$TEST_STATE_DIR/sessions/$session_id.json" <<EOF
{
  "session_id": "$session_id",
  "agent": "$agent",
  "task": "$task",
  "task_id": "$task_id",
  "pid": "$pid",
  "log_file": "$TEST_STATE_DIR/logs/${agent}.log",
  "status": "$status",
  "started_at": "2026-01-01T00:00:00Z"
}
EOF
}

# -------------------------------------------------------------------
# Helper: write task state JSON
# -------------------------------------------------------------------
write_task_state() {
  local task_id="$1" agent="$2" task="$3" pid="$4" status="$5" log_file="$6"
  cat > "$TEST_STATE_DIR/tasks/$task_id.json" <<EOF
{
  "agent": "$agent",
  "task": "$task",
  "task_id": "$task_id",
  "pid": "$pid",
  "status": "$status",
  "started_at": "2026-01-01T00:00:00Z",
  "log_file": "$log_file"
}
EOF
}

# -------------------------------------------------------------------
# Set environment for test state directory
# -------------------------------------------------------------------
export_test_env() {
  export STATE_DIR="$TEST_STATE_DIR"
  export LOG_DIR="$TEST_STATE_DIR/logs"
  export AGENT_DIR="$TEST_STATE_DIR/agents"
  export SESSION_DIR="$TEST_STATE_DIR/sessions"
  export TASK_DIR="$TEST_STATE_DIR/tasks"
  export LOCK_DIR="$TEST_STATE_DIR/locks"
  export KILO_BIN="kilo"
}

# -------------------------------------------------------------------
# Test 1: is_agent_busy returns BUSY when PID alive, no exit file
# -------------------------------------------------------------------
test_busy_pid_alive_no_exit() {
  echo "Test 1: is_agent_busy with PID alive, no exit file → BUSY"
  setup_state
  export_test_env
  local mypid=$$
  write_agent_state "fst-implementation" "$mypid" "task_test1" "ses_test1" "running" ""
  local result
  if is_agent_busy fst-implementation; then
    result="BUSY"
  else
    result="NOT_BUSY"
  fi
  if [ "$result" = "BUSY" ]; then
    pass "PID alive, no exit file → BUSY"
  else
    fail "PID alive, no exit file → BUSY (got: $result)"
  fi
}

# -------------------------------------------------------------------
# Test 2: is_agent_busy returns NOT_BUSY when exit file exists
# -------------------------------------------------------------------
test_not_busy_exit_file_exists() {
  echo "Test 2: is_agent_busy with exit file → NOT_BUSY"
  setup_state
  export_test_env
  local mypid=$$
  write_agent_state "fst-implementation" "$mypid" "task_test2" "ses_test2" "running" ""
  echo "0" > "$TEST_STATE_DIR/logs/task_test2.exit"
  local result
  if is_agent_busy fst-implementation; then
    result="BUSY"
  else
    result="NOT_BUSY"
  fi
  if [ "$result" = "NOT_BUSY" ]; then
    pass "Exit file exists → NOT_BUSY"
  else
    fail "Exit file exists → NOT_BUSY (got: $result)"
  fi
}

# -------------------------------------------------------------------
# Test 3: is_agent_busy returns NOT_BUSY when PID dead, no exit file (STALE)
# -------------------------------------------------------------------
test_not_busy_pid_dead_stale() {
  echo "Test 3: is_agent_busy with dead PID, no exit file → NOT_BUSY (STALE)"
  setup_state
  export_test_env
  write_agent_state "fst-implementation" "99999999" "task_test3" "ses_test3" "running" ""
  local result
  if is_agent_busy fst-implementation; then
    result="BUSY"
  else
    result="NOT_BUSY"
  fi
  if [ "$result" = "NOT_BUSY" ]; then
    pass "Dead PID, no exit file → NOT_BUSY (STALE)"
  else
    fail "Dead PID, no exit file → NOT_BUSY (STALE) (got: $result)"
  fi
}

# -------------------------------------------------------------------
# Test 4: is_agent_busy returns NOT_BUSY when no PID recorded
# -------------------------------------------------------------------
test_not_busy_no_pid() {
  echo "Test 4: is_agent_busy with no PID → NOT_BUSY"
  setup_state
  export_test_env
  write_agent_state "fst-implementation" "" "task_test4" "ses_test4" "idle" ""
  local result
  if is_agent_busy fst-implementation; then
    result="BUSY"
  else
    result="NOT_BUSY"
  fi
  if [ "$result" = "NOT_BUSY" ]; then
    pass "No PID → NOT_BUSY"
  else
    fail "No PID → NOT_BUSY (got: $result)"
  fi
}

# -------------------------------------------------------------------
# Test 5: cmd_cleanup reconciles stale state (dead PID, no exit file)
# -------------------------------------------------------------------
test_cleanup_stale_pid() {
  echo "Test 5: cmd_cleanup with dead PID, no exit file → reconciles to FAILED"
  setup_state
  export_test_env
  write_agent_state "fst-implementation" "99999998" "task_test5" "ses_test5" "running" ""
  write_session_state "ses_test5" "fst-implementation" "test task" "task_test5" "99999998" "running"
  write_task_state "task_test5" "fst-implementation" "test task" "99999998" "running" "$TEST_STATE_DIR/logs/fst-implementation.log"
  cmd_cleanup > /tmp/test_cleanup_output.txt 2>&1 || true
  local agent_status
  agent_status=$(python3 -c "import json; print(json.load(open('$TEST_STATE_DIR/agents/fst-implementation.json'))['status'])")
  if [ "$agent_status" = "failed" ]; then
    pass "cmd_cleanup: stale PID reconciled to FAILED"
  else
    fail "cmd_cleanup: stale PID reconciled to FAILED (got: $agent_status)"
  fi
}

# -------------------------------------------------------------------
# Test 6: cmd_cleanup reconciles exit file (PID alive, exit file exists)
# -------------------------------------------------------------------
test_cleanup_exit_file() {
  echo "Test 6: cmd_cleanup with exit file → reconciles to COMPLETED"
  setup_state
  export_test_env
  local mypid=$$
  write_agent_state "fst-implementation" "$mypid" "task_test6" "ses_test6" "running" ""
  echo "0" > "$TEST_STATE_DIR/logs/task_test6.exit"
  write_session_state "ses_test6" "fst-implementation" "test task" "task_test6" "$mypid" "running"
  write_task_state "task_test6" "fst-implementation" "test task" "$mypid" "running" "$TEST_STATE_DIR/logs/fst-implementation.log"
  cmd_cleanup > /tmp/test_cleanup2_output.txt 2>&1 || true
  local agent_status
  agent_status=$(python3 -c "import json; print(json.load(open('$TEST_STATE_DIR/agents/fst-implementation.json'))['status'])")
  if [ "$agent_status" = "completed" ]; then
    pass "cmd_cleanup: exit file reconciled to COMPLETED"
  else
    fail "cmd_cleanup: exit file reconciled to COMPLETED (got: $agent_status)"
  fi
}

# -------------------------------------------------------------------
# Run all tests
# -------------------------------------------------------------------
test_busy_pid_alive_no_exit
test_not_busy_exit_file_exists
test_not_busy_pid_dead_stale
test_not_busy_no_pid
test_cleanup_stale_pid
test_cleanup_exit_file

# -------------------------------------------------------------------
# Summary
# -------------------------------------------------------------------
echo ""
echo "========================================"
echo "Results: $PASS passed, $FAIL failed"
echo "========================================"
rm -rf "$TEST_STATE_DIR"
[ "$FAIL" -eq 0 ] && exit 0 || exit 1
