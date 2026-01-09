#!/bin/bash
# run all tests
cd "$(dirname "$0")/.."
pass=0
fail=0
fails=""

for f in tests/test_*.xs; do
    name=$(basename "$f" .xs)

    if [ "$name" = "test_vm" ]; then
        # vm tests run with --vm flag
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

echo ""
echo "results: $pass passed, $fail failed"
if [ -n "$fails" ]; then
    echo -e "\nfailed tests:$fails"
    exit 1
fi
