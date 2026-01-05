#!/bin/bash
set -e
cd "$(dirname "$0")/.."
PASS=0; FAIL=0

vm_ok() {
    local file="$1" expected="$2"
    local out rc=0
    out=$(./xs --vm "$file" 2>&1) || rc=$?
    if [ $rc -eq 0 ] && echo "$out" | grep -qF "$expected"; then
        echo "PASS (--vm): $file → '$expected'"
        PASS=$((PASS+1))
    else
        echo "FAIL (--vm): $file — expected '$expected'"
        echo "  exit=$rc, output: $out"
        FAIL=$((FAIL+1))
    fi
}

emit_ok() {
    local file="$1"
    local out rc=0
    out=$(./xs --emit bytecode "$file" 2>&1) || rc=$?
    if [ $rc -eq 0 ] && echo "$out" | grep -q "proto"; then
        echo "PASS (--emit bytecode): $file"
        PASS=$((PASS+1))
    else
        echo "FAIL (--emit bytecode): $file"
        echo "  exit=$rc, output: $out"
        FAIL=$((FAIL+1))
    fi
}

# Phase 4 regressions (VM must not break existing passes)
vm_ok  tests/vm_fib.xs       "55"
vm_ok  tests/vm_tailcall.xs  "done"
vm_ok  tests/vm_closures.xs  "8"
vm_ok  tests/vm_closures.xs  "15"

# Bytecode emission smoke tests
emit_ok tests/vm_fib.xs
emit_ok tests/vm_tailcall.xs
emit_ok tests/vm_closures.xs

# Arithmetic & literals
vm_ok  tests/vm_arith.xs     "42"
vm_ok  tests/vm_arith.xs     "3.14"
vm_ok  tests/vm_arith.xs     "hello world"

# Globals & while loop
vm_ok  tests/vm_while.xs     "10"

# For loops
vm_ok  tests/vm_for.xs       "15"
vm_ok  tests/vm_for.xs       "abc"
vm_ok  tests/vm_for.xs       "10"

# Ranges in for loops
vm_ok  tests/vm_range.xs     "15"
vm_ok  tests/vm_range.xs     "10"

# Break/continue
vm_ok  tests/vm_break.xs     "5"
vm_ok  tests/vm_break.xs     "30"

# Match expression
vm_ok  tests/vm_match.xs     "zero"
vm_ok  tests/vm_match.xs     "one"
vm_ok  tests/vm_match.xs     "other"

# Method calls
vm_ok  tests/vm_methods.xs   "hello world"
vm_ok  tests/vm_methods.xs   "HELLO WORLD"
vm_ok  tests/vm_methods.xs   "4"
vm_ok  tests/vm_methods.xs   "a-b-c"

echo ""
echo "Phase 5: $PASS passed, $FAIL failed"
[ $FAIL -eq 0 ] && echo "ALL PHASE 5 TESTS PASSED" || exit 1
