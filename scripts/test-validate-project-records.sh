#!/usr/bin/env bash
set -Eeuo pipefail

# Test script for scripts/validate-project-records.sh
# Each test copies a project record to a temp location, introduces a deliberate
# inconsistency, runs the validator, verifies it FAILS, then restores the original
# and verifies it PASSES.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

VALIDATOR="scripts/validate-project-records.sh"

FAILURES=0

fail() {
    echo "FAIL: $*" >&2
    FAILURES=$((FAILURES + 1))
}

pass() {
    echo "PASS: $*"
}

# run_test test_name file modify_cmd
# modify_cmd is a sed expression; TEST_FILE env var is set to the file path
run_test() {
    local test_name="$1"
    local file="$2"
    local modify_cmd="$3"

    # Save original
    cp "$file" "$file.orig"

    # Apply modification
    TEST_FILE="$file" bash -c "$modify_cmd"

    echo "--- $test_name: expecting FAIL ---"
    if bash "$VALIDATOR" >/dev/null 2>&1; then
        fail "$test_name: validator PASSED despite introduced inconsistency"
    else
        pass "$test_name: validator FAILED as expected"
    fi

    # Restore original
    cp "$file.orig" "$file"
    rm -f "$file.orig"

echo "--- $test_name: expecting PASS after restore ---"
    if bash "$VALIDATOR" >/dev/null 2>&1; then
        pass "$test_name: validator PASSED after restoration"
    else
        fail "$test_name: validator FAILED after restoration"
        bash "$VALIDATOR" 2>&1 | grep "^FAIL:" | head -5 >&2
    fi
}

echo "============================================================"
echo "FST Validator Tests"
echo "============================================================"

# --------------------------------------------------------------------
# Test 1: Duplicate requirement ID
# --------------------------------------------------------------------
run_test "Duplicate requirement ID" \
    "docs/fst/operations/project-plan.md" \
    'sed -i "/^| FST-REQ-0001 /a | FST-REQ-0001 | Duplicate entry | Should be caught | — | — | — | — | — | — |" "$TEST_FILE"'

# --------------------------------------------------------------------
# Test 2: Missing requirement ID (remove FST-REQ-0074 row)
# --------------------------------------------------------------------
run_test "Missing requirement ID" \
    "docs/fst/operations/project-plan.md" \
    'sed -i "/^| FST-REQ-0074 /d" "$TEST_FILE"'

# --------------------------------------------------------------------
# Test 3: Phase 1 status not COMPLETE
# --------------------------------------------------------------------
run_test "Phase 1 not COMPLETE" \
    "docs/fst/operations/project-plan.md" \
    'sed -i "s/| 1 | FST-PHASE-01 | Specification \& Foundational Research | COMPLETE |/| 1 | FST-PHASE-01 | Specification \& Foundational Research | IN_PROGRESS |/" "$TEST_FILE"'

# --------------------------------------------------------------------
# Test 4: Defect statistics OPEN count mismatch
# --------------------------------------------------------------------
run_test "Defect statistics OPEN count mismatch" \
    "docs/fst/operations/defect-register.md" \
    'sed -i "s/| \*\*Total\*\* | \*\*4\*\* | \*\*1\*\* | \*\*0\*\* | \*\*11\*\* | \*\*1\*\* | \*\*0\*\* | \*\*0\*\* | \*\*17\*\* |/| **Total** | **5** | **1** | **0** | **11** | **1** | **0** | **0** | **18** |/" "$TEST_FILE"' \
    'DEBUG'

# --------------------------------------------------------------------
# Test 5: HEAD reference mismatch
# --------------------------------------------------------------------
run_test "HEAD reference mismatch" \
    "docs/fst/operations/implementation-status.md" \
    'sed -i "s|52341a04d2ac095b64df1bcc8d11790dfc179950|DEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBEEF|" "$TEST_FILE"'

# --------------------------------------------------------------------
# Test 6: Decision classification mismatch
# --------------------------------------------------------------------
run_test "Decision classification mismatch" \
    "docs/fst/scope/scope-v0.1.md" \
    'sed -i "s/| Monetary policy | D-015 | APPROVED |/| Monetary policy | D-015 | OPEN RESEARCH |/" "$TEST_FILE"'

# --------------------------------------------------------------------
# Test 7: AGENTS.md tracked
# --------------------------------------------------------------------
echo
echo "--- Test 7: AGENTS.md tracked ---"
git add -f AGENTS.md 2>/dev/null || true
restore_output=$(bash "$VALIDATOR" 2>&1)
restore_exit=$?
if [[ "$restore_exit" -eq 0 ]]; then
    fail "Test 7: validator PASSED despite AGENTS.md being tracked"
else
    pass "Test 7: validator FAILED as expected on AGENTS.md tracked"
fi
git reset -q HEAD AGENTS.md 2>/dev/null || true
if bash "$VALIDATOR" >/dev/null 2>&1; then
    pass "Test 7: validator PASSED after restoration"
else
    fail "Test 7: validator FAILED after restoration"
fi

# --------------------------------------------------------------------
# Test 8: session-ses_*.md tracked
# --------------------------------------------------------------------
echo
echo "--- Test 8: session-ses_*.md tracked ---"
echo "test content" > session-ses_test.md
git add session-ses_test.md 2>/dev/null || true
restore_output=$(bash "$VALIDATOR" 2>&1)
restore_exit=$?
if [[ "$restore_exit" -eq 0 ]]; then
    fail "Test 8: validator PASSED despite session-ses_*.md being tracked"
else
    pass "Test 8: validator FAILED as expected on session-ses_*.md tracked"
fi
git reset -q HEAD session-ses_test.md 2>/dev/null || true
rm -f session-ses_test.md
if bash "$VALIDATOR" >/dev/null 2>&1; then
    pass "Test 8: validator PASSED after restoration"
else
    fail "Test 8: validator FAILED after restoration"
fi

# --------------------------------------------------------------------
# Summary
# --------------------------------------------------------------------
echo
echo "============================================================"
if [[ "$FAILURES" -eq 0 ]]; then
    echo "ALL VALIDATOR TESTS PASSED"
    exit 0
else
    echo "$FAILURES TEST(S) FAILED"
    exit 1
fi