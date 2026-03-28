#!/bin/bash
# run all tests
cd "$(dirname "$0")/.."
pass=0
fail=0
fails=""

# language tests (.xs files)
for f in tests/test_*.xs; do
    name=$(basename "$f" .xs)

    if [ "$name" = "test_vm" ]; then
        output=$(./xs --vm "$f" 2>&1)
    else
        output=$(./xs "$f" 2>&1)
    fi

    rc=$?
    if [ $rc -ne 0 ]; then
        fail=$((fail + 1))
        fails="$fails\n  FAIL: $name"
        echo "  FAIL  $name"
        echo "$output" | grep -E "assert|error" | head -3
    else
        pass=$((pass + 1))
        echo "  ok    $name"
    fi
done

# CLI flag tests
if [ -f tests/test_cli.sh ]; then
    cli_output=$(bash tests/test_cli.sh 2>&1)
    cli_rc=$?
    cli_pass=$(echo "$cli_output" | grep -oP '\d+ passed' | grep -oP '\d+')
    cli_fail=$(echo "$cli_output" | grep -oP '\d+ failed' | grep -oP '\d+')
    if [ "$cli_rc" -eq 0 ]; then
        pass=$((pass + 1))
        echo "  ok    test_cli (${cli_pass:-0} checks)"
    else
        fail=$((fail + 1))
        fails="$fails\n  FAIL: test_cli"
        echo "  FAIL  test_cli"
        echo "$cli_output" | grep "FAIL" | head -5
    fi
fi

echo ""
echo "results: $pass passed, $fail failed"
if [ -n "$fails" ]; then
    echo -e "\nfailed tests:$fails"
    exit 1
fi
