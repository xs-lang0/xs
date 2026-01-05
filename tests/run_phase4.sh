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

check_warn() {
    local file="$1" pattern="$2"
    local out rc=0
    out=$(./xs --lenient "$file" 2>&1) || rc=$?
    if [ $rc -eq 0 ] && echo "$out" | grep -qi "$pattern"; then
        echo "PASS (--lenient warned '$pattern'): $file"
        PASS=$((PASS+1))
    else
        echo "FAIL (--lenient should warn '$pattern'): $file"
        echo "  exit=$rc, output: $out"
        FAIL=$((FAIL+1))
    fi
}

# Phase 3 regression tests
run_ok   tests/type_annotations.xs  "type annotation parse test passed"
run_ok   tests/iterator_protocol.xs "iterator protocol test passed"
run_ok   tests/exhaustiveness.xs    "exhaustiveness test passed"
run_ok   tests/traits_assoc.xs      "traits assoc test passed"

# Phase 4: name resolution
run_ok   tests/name_resolution.xs   "name resolution test passed"
check_ok tests/name_resolution.xs
check_fail tests/type_errors/undefined_name.xs  "undefined"

# Phase 4: type checking
run_ok   tests/type_checking.xs     "type checking test passed"
check_ok tests/type_checking.xs
check_fail tests/type_errors/type_mismatch.xs   "mismatch"

# Phase 4: unused variable warning (lenient = warning, not error)
check_warn tests/type_errors/unused_var.xs "unused"

echo ""
echo "Phase 4: $PASS passed, $FAIL failed"
[ $FAIL -eq 0 ] && echo "ALL PHASE 4 TESTS PASSED" || exit 1
