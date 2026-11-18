#!/usr/bin/env bash
set -Eeuo pipefail

# FST Project Record Validation Script
# Verifies consistency of project records, requirement tracking, defect register,
# documentation links, and publication safety.
#
# Exit codes: 0 = all checks pass, 1 = any check fails.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

FAILURES=0

fail() {
    echo "FAIL: $*" >&2
    FAILURES=$((FAILURES + 1))
}

pass() {
    echo "PASS: $*"
}

echo "============================================================"
echo "FST Project Record Validation"
echo "Root: $ROOT"
echo "============================================================"

# --------------------------------------------------------------------
# Check 1: All 74 original FST requirement IDs present exactly once
# in each applicable authoritative requirements table.
# --------------------------------------------------------------------
echo
echo "--- Check 1: Requirement ID presence and uniqueness ---"

REQUIREMENT_TABLE_FILES=(
    "docs/fst/operations/project-plan.md"
)

for table_file in "${REQUIREMENT_TABLE_FILES[@]}"; do
    if [[ ! -f "$table_file" ]]; then
        fail "Requirements table not found: $table_file"
        continue
    fi

    # Extract all FST-REQ-XXXX IDs from table rows (lines starting with | FST-REQ-)
    mapfile -t req_ids < <(grep '^| FST-REQ-' "$table_file" | sed -E 's/^\| *[Ff][Ss][Tt]-REQ-([0-9]+).*$/\1/' | sort -n)

    expected_count=74
    actual_count=${#req_ids[@]}

    if [[ "$actual_count" -ne "$expected_count" ]]; then
        fail "$table_file: expected $expected_count requirement IDs, found $actual_count"
    else
        pass "$table_file: found $actual_count requirement IDs"
    fi

    # Check for duplicates
    mapfile -t dup_ids < <(printf '%s\n' "${req_ids[@]}" | sort | uniq -d)
    if [[ ${#dup_ids[@]} -gt 0 ]]; then
        fail "$table_file: duplicate requirement IDs found: ${dup_ids[*]}"
    else
        pass "$table_file: no duplicate requirement IDs"
    fi

    # Check for missing IDs (1-74) - strip leading zeros for numeric comparison
    missing=()
    for i in $(seq 1 74); do
        padded=$(printf '%04d' "$i")
        found=0
        for rid in "${req_ids[@]}"; do
            if [[ "$rid" == "$padded" ]]; then
                found=1
                break
            fi
        done
        if [[ "$found" -eq 0 ]]; then
            missing+=("FST-REQ-$padded")
        fi
    done
    if [[ ${#missing[@]} -gt 0 ]]; then
        fail "$table_file: missing requirement IDs: ${missing[*]}"
    else
        pass "$table_file: all 74 requirement IDs (0001-0074) present"
    fi

    # Check for out-of-range IDs (strip leading zeros for base-10 comparison)
    out_of_range=()
    for rid in "${req_ids[@]}"; do
        # Force base-10 interpretation to avoid octal issues with leading zeros
        num=$((10#$rid))
        if [[ "$num" -lt 1 || "$num" -gt 74 ]]; then
            out_of_range+=("FST-REQ-$rid")
        fi
    done
    if [[ ${#out_of_range[@]} -gt 0 ]]; then
        fail "$table_file: out-of-range requirement IDs: ${out_of_range[*]}"
    else
        pass "$table_file: all requirement IDs in range 0001-0074"
    fi
done

# --------------------------------------------------------------------
# Check 2: Phase and requirement summaries agree with underlying entries
# --------------------------------------------------------------------
echo
echo "--- Check 2: Phase summary vs underlying entries ---"

if [[ -f "docs/fst/operations/project-plan.md" ]]; then
    # Extract phase summary rows from the Current Status Summary table
    mapfile -t phase_lines < <(grep '^| *[0-9] | FST-PHASE-' docs/fst/operations/project-plan.md)

    for line in "${phase_lines[@]}"; do
        # Parse the table row: | 0 | FST-PHASE-00 | Title | STATUS | 9 | 9 | 0 | 0 | 0 | 0 | 0 |
        phase_id=$(echo "$line" | awk -F'|' '{gsub(/^ +| +$/, "", $2); print $2}')
        phase_full_id=$(echo "$line" | awk -F'|' '{gsub(/^ +| +$/, "", $3); print $3}')
        req_count=$(echo "$line" | awk -F'|' '{gsub(/^ +| +$/, "", $5); print $5}')
        implemented=$(echo "$line" | awk -F'|' '{gsub(/^ +| +$/, "", $6); print $6}')
        partial=$(echo "$line" | awk -F'|' '{gsub(/^ +| +$/, "", $7); print $7}')
        in_progress=$(echo "$line" | awk -F'|' '{gsub(/^ +| +$/, "", $8); print $8}')
        not_started=$(echo "$line" | awk -F'|' '{gsub(/^ +| +$/, "", $9); print $9}')
        blocked=$(echo "$line" | awk -F'|' '{gsub(/^ +| +$/, "", $10); print $10}')
        deferred=$(echo "$line" | awk -F'|' '{gsub(/^ +| +$/, "", $11); print $11}')

        # Skip header/separator lines
        if [[ "$phase_id" =~ ^[0-9]+$ ]] && [[ "$req_count" =~ ^[0-9]+$ ]]; then
            summary_total=$((implemented + partial + in_progress + not_started + blocked + deferred))
            if [[ "$summary_total" -ne "$req_count" ]]; then
                fail "Phase $phase_full_id summary: total $req_count != sum of statuses $summary_total (IMPLEMENTED=$implemented PARTIAL=$partial IN_PROGRESS=$in_progress NOT_STARTED=$not_started BLOCKED=$blocked DEFERRED=$deferred)"
            else
                pass "Phase $phase_full_id summary: total $req_count == sum of statuses $summary_total"
            fi
        fi
    done
fi

# --------------------------------------------------------------------
# Check 3: Defect IDs are unique
# --------------------------------------------------------------------
echo
echo "--- Check 3: Defect ID uniqueness ---"

if [[ -f "docs/fst/operations/defect-register.md" ]]; then
    mapfile -t defect_ids < <(grep '^### FST-BUG-' docs/fst/operations/defect-register.md | sed -E 's/^### (FST-BUG-[0-9]+).*$/\1/' | sort)

    defect_count=${#defect_ids[@]}
    pass "Defect register: found $defect_count defect entries"

    mapfile -t dup_defects < <(printf '%s\n' "${defect_ids[@]}" | sort | uniq -d)
    if [[ ${#dup_defects[@]} -gt 0 ]]; then
        fail "Defect register: duplicate defect IDs found: ${dup_defects[*]}"
    else
        pass "Defect register: no duplicate defect IDs"
    fi

    # Check defect ID format
    bad_format=()
    for did in "${defect_ids[@]}"; do
        if [[ ! "$did" =~ ^FST-BUG-[0-9]{4}$ ]]; then
            bad_format+=("$did")
        fi
    done
    if [[ ${#bad_format[@]} -gt 0 ]]; then
        fail "Defect register: non-conforming defect IDs: ${bad_format[*]}"
    else
        pass "Defect register: all defect IDs conform to FST-BUG-XXXX format"
    fi
fi

# --------------------------------------------------------------------
# Check 4: VERIFIED_FIXED defects have a fix reference and verification evidence
# --------------------------------------------------------------------
echo
echo "--- Check 4: VERIFIED_FIXED defects have fix reference and evidence ---"

if [[ -f "docs/fst/operations/defect-register.md" ]]; then
    # Find each defect section header line number
    mapfile -t defect_section_lines < <(grep -n '^### FST-BUG-' docs/fst/operations/defect-register.md | sed -E 's/^([0-9]+):.*/\1/')

    for i in "${!defect_section_lines[@]}"; do
        start_line=${defect_section_lines[$i]}
        if [[ $i -lt $(( ${#defect_section_lines[@]} - 1 )) ]]; then
            end_line=$(( ${defect_section_lines[$((i + 1))]} - 1 ))
        else
            end_line=999999
        fi

        section=$(sed -n "${start_line},${end_line}p" docs/fst/operations/defect-register.md)
        defect_id=$(echo "$section" | head -1 | sed -E 's/^### (FST-BUG-[0-9]+).*$/\1/')

        if echo "$section" | grep -q '\|\ **Status**\ \|\ VERIFIED_FIXED\ |'; then
            fix_sha_line=$(echo "$section" | grep '\|\ **Fix Commit SHA**\ |' || true)
            verification_line=$(echo "$section" | grep '\|\ **Verification Result**\ |' || true)

            if [[ -z "$fix_sha_line" ]]; then
                fail "Defect $defect_id: VERIFIED_FIXED but Fix Commit SHA field is missing"
            else
                fix_sha=$(echo "$fix_sha_line" | sed -E 's/^\| *\*\*Fix Commit SHA\*\* *\| *(.*) *\|$/\1/')
                if [[ -z "$fix_sha" || "$fix_sha" == "—" || "$fix_sha" == "(pending)" || "$fix_sha" == "Working tree (pending commit)" ]]; then
                    fail "Defect $defect_id: VERIFIED_FIXED but Fix Commit SHA is missing or pending: '$fix_sha'"
                else
                    pass "Defect $defect_id: VERIFIED_FIXED with fix reference '$fix_sha'"
                fi
            fi

            if [[ -z "$verification_line" ]]; then
                fail "Defect $defect_id: VERIFIED_FIXED but Verification Result field is missing"
            else
                verification=$(echo "$verification_line" | sed -E 's/^\| *\*\*Verification Result\*\* *\| *(.*) *\|$/\1/')
                if [[ -z "$verification" || "$verification" == "—" || "$verification" == "(pending)" ]]; then
                    fail "Defect $defect_id: VERIFIED_FIXED but Verification Result is missing or pending: '$verification'"
                else
                    pass "Defect $defect_id: VERIFIED_FIXED with verification evidence"
                fi
            fi
        fi
    done
fi

# --------------------------------------------------------------------
# Check 5: Referenced local documentation paths exist
# --------------------------------------------------------------------
echo
echo "--- Check 5: Referenced local documentation paths exist ---"

DOC_REF_FILES=(
    "docs/fst/scope/scope-v0.1.md"
    "docs/fst/principles/engineering-principles.md"
    "docs/fst/decisions/decision-register.md"
    "docs/fst/architecture/feinstat-baseline.md"
    "docs/fst/research/research-register.md"
    "docs/fst/roadmap/development-roadmap.md"
    "docs/fst/operations/project-plan.md"
    "docs/fst/operations/implementation-status.md"
    "docs/fst/operations/verification-status.md"
    "docs/fst/operations/defect-register.md"
)

for doc_ref in "${DOC_REF_FILES[@]}"; do
    if [[ -f "$ROOT/$doc_ref" ]]; then
        pass "Documentation path exists: $doc_ref"
    else
        fail "Documentation path missing: $doc_ref"
    fi
done

# Check ADR paths referenced in decision register
if [[ -f "docs/fst/decisions/decision-register.md" ]]; then
    mapfile -t adr_paths < <(grep -oP '\[.*?\]\(../adr/[^)]*\)' docs/fst/decisions/decision-register.md | sed -E 's/.*\((.*)\)/\1/' | sort -u)
    for adr_rel in "${adr_paths[@]}"; do
        # Resolve relative to docs/fst/decisions/ directory (location of decision-register.md)
        # ../adr/ from docs/fst/decisions/ → docs/fst/adr/
        adr_dir="$ROOT/docs/fst/decisions"
        # Walk up one level for .., then append the rest
        if [[ "$adr_rel" == "../"* ]]; then
            adr_dir=$(dirname "$adr_dir")
            adr_rel="${adr_rel#../}"
        fi
        adr_path="$adr_dir/$adr_rel"
        # Remove trailing slash for directory check
        adr_path="${adr_path%/}"
        if [[ -e "$adr_path" ]]; then
            pass "ADR path exists: $adr_rel"
        else
            fail "ADR path missing: $adr_rel (resolved: $adr_path)"
        fi
    done
fi

# --------------------------------------------------------------------
# Check 6: Critical internal Markdown links resolve
# --------------------------------------------------------------------
echo
echo "--- Check 6: Critical internal Markdown links resolve ---"

KEY_DOC_FILES=(
    "docs/fst/scope/scope-v0.1.md"
    "docs/fst/decisions/decision-register.md"
    "docs/fst/principles/engineering-principles.md"
    "docs/fst/operations/project-plan.md"
)

for doc_file in "${KEY_DOC_FILES[@]}"; do
    if [[ ! -f "$doc_file" ]]; then
        continue
    fi
    doc_dir=$(dirname "$doc_file")
    while IFS= read -r link_line; do
        link_path=$(echo "$link_line" | sed -E 's/.*\]\(([^)]+)\).*/\1/')
        # Skip external URLs and anchors
        if [[ "$link_path" =~ ^https?:// ]] || [[ "$link_path" =~ ^# ]]; then
            continue
        fi
        # Resolve relative to the doc file's directory, normalizing ..
        resolved_dir="$doc_dir"
        link_remainder="$link_path"
        while [[ "$link_remainder" == "../"* ]]; do
            resolved_dir=$(dirname "$resolved_dir")
            link_remainder="${link_remainder#../}"
        done
        resolved_path="$ROOT/$resolved_dir/$link_remainder"
        # Remove trailing slash
        resolved_path="${resolved_path%/}"
        if [[ -e "$resolved_path" ]]; then
            : # link OK
        else
            fail "Broken link in $doc_file: $link_path (resolved: $resolved_path)"
        fi
    done < <(grep -oP '\[[^\]]+\]\([^)]+\)' "$doc_file" | grep -v 'https?://')
done

# --------------------------------------------------------------------
# Check 7: No root AGENTS.md is tracked or staged
# --------------------------------------------------------------------
echo
echo "--- Check 7: No root AGENTS.md tracked or staged ---"

if git ls-files --error-unmatch AGENTS.md >/dev/null 2>&1; then
    fail "AGENTS.md is tracked in Git (must remain local-only)"
else
    pass "AGENTS.md is not tracked in Git"
fi

if git diff --cached --name-only | grep -q '^AGENTS\.md$'; then
    fail "AGENTS.md is staged for commit"
else
    pass "AGENTS.md is not staged"
fi

if grep -q '^/AGENTS\.md$' .gitignore; then
    pass ".gitignore excludes /AGENTS.md"
else
    fail ".gitignore does not exclude /AGENTS.md"
fi

# --------------------------------------------------------------------
# Check 8: No session-ses_*.md file is tracked or staged
# --------------------------------------------------------------------
echo
echo "--- Check 8: No session-ses_*.md tracked or staged ---"

session_files=$(git ls-files | grep 'session-ses_.*\.md$' || true)
if [[ -n "$session_files" ]]; then
    fail "session-ses_*.md files tracked in Git: $session_files"
else
    pass "No session-ses_*.md files tracked in Git"
fi

staged_session=$(git diff --cached --name-only | grep 'session-ses_.*\.md$' || true)
if [[ -n "$staged_session" ]]; then
    fail "session-ses_*.md files staged for commit: $staged_session"
else
    pass "No session-ses_*.md files staged"
fi

# --------------------------------------------------------------------
# Check 9: No tracked files match .gitignore (publication safety)
# --------------------------------------------------------------------
echo
echo "--- Check 9: No tracked files match .gitignore ---"

ignored_but_tracked=()
while IFS= read -r f; do
    if git check-ignore -q "$f" 2>/dev/null; then
        ignored_but_tracked+=("$f")
    fi
done < <(git ls-files)

if [[ ${#ignored_but_tracked[@]} -gt 0 ]]; then
    fail "Tracked files that match .gitignore (should be removed from tracking): ${ignored_but_tracked[*]}"
else
    pass "No tracked files match .gitignore patterns"
fi

# --------------------------------------------------------------------
# Summary
# --------------------------------------------------------------------
echo
echo "============================================================"
if [[ "$FAILURES" -eq 0 ]]; then
    echo "VALIDATION PASSED: all checks successful"
    exit 0
else
    echo "VALIDATION FAILED: $FAILURES check(s) failed"
    exit 1
fi