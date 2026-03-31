#!/bin/bash
# test_cli.sh - test CLI flags, commands, and error handling
cd "$(dirname "$0")/.."
pass=0
fail=0

check() {
    local desc="$1"
    shift
    if "$@" > /dev/null 2>&1; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        echo "  FAIL  $desc"
    fi
}

check_fail() {
    local desc="$1"
    shift
    if "$@" > /dev/null 2>&1; then
        fail=$((fail + 1))
        echo "  FAIL  $desc (should have failed)"
    else
        pass=$((pass + 1))
    fi
}

check_output() {
    local desc="$1"
    local expected="$2"
    shift 2
    local actual
    actual=$("$@" 2>&1)
    if echo "$actual" | grep -q "$expected"; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        echo "  FAIL  $desc (expected '$expected', got '$(echo "$actual" | head -1)')"
    fi
}

# version and help
check_output "--version" "xs 0.3" ./xs --version
check_output "-V" "xs 0.3" ./xs -V
check_output "--help" "Usage:" ./xs --help
check_output "-h" "Usage:" ./xs -h

# -e eval
check_output "-e basic" "42" ./xs -e 'println(42)'
check_output "-e expr" "hello" ./xs -e 'println("hello")'
check_output "-e math" "7" ./xs -e 'println(3 + 4)'

# --check (should not execute, just type check)
echo 'let x: int = 42' > /tmp/xs_cli_check.xs
check "--check clean" ./xs --check /tmp/xs_cli_check.xs

echo 'let x: int = "bad"' > /tmp/xs_cli_bad.xs
check_fail "--check catches type error" ./xs --check /tmp/xs_cli_bad.xs

# --strict
echo 'let x = 42' > /tmp/xs_cli_strict.xs
check_fail "--strict requires annotations" ./xs --strict --check /tmp/xs_cli_strict.xs

echo 'let x: int = 42' > /tmp/xs_cli_strict_ok.xs
check "--strict with annotations" ./xs --strict --check /tmp/xs_cli_strict_ok.xs

# --vm
echo 'assert_eq(2 + 3, 5)' > /tmp/xs_cli_vm.xs
check "--vm runs" ./xs --vm /tmp/xs_cli_vm.xs

# --check + --vm (should check, not run)
echo 'println("should not print")' > /tmp/xs_cli_check_vm.xs
check_output "--check --vm" "No errors" ./xs --check --vm /tmp/xs_cli_check_vm.xs

# flags after filename
check_output "flags after file" "No errors" ./xs /tmp/xs_cli_check_vm.xs --check

# --emit ast
echo 'let x = 42' > /tmp/xs_cli_emit.xs
check_output "--emit ast" "PROGRAM" ./xs --emit ast /tmp/xs_cli_emit.xs
check_output "--emit bytecode" "proto" ./xs --emit bytecode /tmp/xs_cli_emit.xs

# --optimize
echo 'let x = 2 + 3; println(x)' > /tmp/xs_cli_opt.xs
check "--optimize" ./xs --optimize /tmp/xs_cli_opt.xs

# --optimize + --vm
check "--optimize --vm" ./xs --optimize --vm /tmp/xs_cli_opt.xs

# --record (tracer)
echo 'fn add(a, b) { return a + b }; let x = add(1, 2)' > /tmp/xs_cli_trace.xs
./xs --record /tmp/xs_cli_trace.xst /tmp/xs_cli_trace.xs > /dev/null 2>&1
if [ -s /tmp/xs_cli_trace.xst ]; then
    pass=$((pass + 1))
else
    fail=$((fail + 1))
    echo "  FAIL  --record produces non-empty file"
fi

# --no-color (just check it doesn't crash)
check "--no-color" ./xs --no-color -e 'println(1)'

# --lenient (sema errors become warnings, but runtime type checks still fire)
echo 'let x = 42; println(x)' > /tmp/xs_cli_lenient.xs
check "--lenient runs clean code" ./xs --lenient /tmp/xs_cli_lenient.xs

# build + run .xsc
echo 'assert_eq(1 + 1, 2)' > /tmp/xs_cli_build.xs
./xs build /tmp/xs_cli_build.xs -o /tmp/xs_cli_build.xsc > /dev/null 2>&1
if [ -f /tmp/xs_cli_build.xsc ]; then
    check "run .xsc" ./xs run /tmp/xs_cli_build.xsc
    pass=$((pass + 1))  # build succeeded
else
    fail=$((fail + 1))
    echo "  FAIL  build produces .xsc file"
fi

# explain
check_output "explain T0001" "mismatched types" ./xs explain T0001

# parse errors caught
echo 'let x =' > /tmp/xs_cli_parse_err.xs
check_fail "parse error exits nonzero" ./xs /tmp/xs_cli_parse_err.xs

# runtime errors caught
echo 'assert(false, "boom")' > /tmp/xs_cli_rt_err.xs
check_fail "runtime error exits nonzero" ./xs /tmp/xs_cli_rt_err.xs

# cleanup
rm -f /tmp/xs_cli_*.xs /tmp/xs_cli_*.xst /tmp/xs_cli_*.xsc

echo ""
echo "  cli: $pass passed, $fail failed"
if [ $fail -gt 0 ]; then
    exit 1
fi
