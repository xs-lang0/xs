#!/bin/bash
cd "$(dirname "$0")/.."
echo 'println("should not print")' > /tmp/xs_check_test.xs
OUTPUT=$(./xs --check /tmp/xs_check_test.xs 2>&1)
if [ $? -eq 0 ] && [ -z "$OUTPUT" ]; then
    echo "check flag test passed!"
else
    echo "FAIL: --check printed output or failed"
    exit 1
fi
