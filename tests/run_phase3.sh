#!/bin/bash
set -e
cd "$(dirname "$0")/.."
PASS=0; FAIL=0

run_ok() {
    local file="$1" expected="$2"
    local out
    out=$(./xs "$file" 2>&1) || true
    if echo "$out" | grep -q "$expected"; then
        echo "PASS (run): $file"
        PASS=$((PASS+1))
    else
        echo "FAIL (run): $file — expected '$expected'"
        echo "  output: $out"
        FAIL=$((FAIL+1))
    fi
}

check_ok() {
    local file="$1"
    local out rc=0
    out=$(./xs --check "$file" 2>&1) || rc=$?
    if [ $rc -eq 0 ]; then
        echo "PASS (--check ok): $file"
        PASS=$((PASS+1))
    else
        echo "FAIL (--check should pass): $file"
        echo "  output: $out"
        FAIL=$((FAIL+1))
    fi
}

check_fail() {
    local file="$1" pattern="$2"
    local out rc=0
    out=$(./xs --check "$file" 2>&1) || rc=$?
    if [ $rc -ne 0 ] && echo "$out" | grep -qi "$pattern"; then
        echo "PASS (--check caught '$pattern'): $file"
        PASS=$((PASS+1))
    else
        echo "FAIL (--check should fail with '$pattern'): $file"
        echo "  exit=$rc, output: $out"
        FAIL=$((FAIL+1))
    fi
}

run_ok   tests/type_annotations.xs  "type annotation parse test passed"
run_ok   tests/iterator_protocol.xs "iterator protocol test passed"
run_ok   tests/exhaustiveness.xs    "exhaustiveness test passed"
run_ok   tests/traits_assoc.xs      "traits assoc test passed"
check_ok tests/type_annotations.xs
check_ok tests/exhaustiveness.xs
check_ok tests/traits_assoc.xs
check_fail tests/type_errors/mutability.xs     "immutable"
check_fail tests/type_errors/pure_violation.xs "impure"

echo ""
echo "Phase 3: $PASS passed, $FAIL failed"
[ $FAIL -eq 0 ] && echo "ALL PHASE 3 TESTS PASSED" || exit 1
