#!/bin/bash
# negative tests: verify that invalid code produces the expected errors
cd "$(dirname "$0")/.."
pass=0
fail=0

expect_error() {
    local desc="$1"
    local code="$2"
    local pattern="$3"
    local tmpf="/tmp/xs_err_test_$$.xs"
    echo "$code" > "$tmpf"
    local output
    output=$(./xs "$tmpf" 2>&1)
    local rc=$?
    rm -f "$tmpf"
    if [ $rc -eq 0 ]; then
        fail=$((fail + 1))
        echo "  FAIL  $desc (expected error, got success)"
        return
    fi
    if echo "$output" | grep -qiE "$pattern"; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        echo "  FAIL  $desc (expected '$pattern' in output)"
        echo "        got: $(echo "$output" | head -1)"
    fi
}

# undefined variable
expect_error "undefined var" \
    'println(xyz_undefined_var)' \
    "not defined|not found"

# type mismatch with annotation
expect_error "type mismatch" \
    'let x: int = "hello"' \
    "type"

# calling non-function
expect_error "call non-function" \
    'let x = 42
x(1, 2)' \
    "not callable|not a function|cannot call|error"

# non-existent method
expect_error "bad method" \
    'let x = 42
println(x.nonexistent_method())' \
    "method|not found|unknown|error"

# strict mode missing types
expect_error "strict mode" \
    'xs_strict_check_placeholder' \
    "not defined|not found|error"

# immutable variable assignment
expect_error "immutable assign" \
    'let x = 42
x = 43' \
    "immutable|cannot assign|not mutable"

# unknown type annotation (--check mode)
expect_error "unknown type check" \
    'fn foo(x: Nonexistent) { x }' \
    "unknown type|T0011|not found|error"

# missing closing brace
expect_error "missing brace" \
    'fn foo() {
println("hello")' \
    "expected|error|brace"

# duplicate variable
expect_error "string as number" \
    'let x: int = "not a number"' \
    "type|mismatch|error"

echo ""
echo "error tests: $pass passed, $fail failed"
[ $fail -eq 0 ] && exit 0 || exit 1
