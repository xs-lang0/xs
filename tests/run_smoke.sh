#!/bin/bash
# tests/run_smoke.sh — run smoke test and check output
set -e
cd "$(dirname "$0")/.."
OUTPUT=$(./xs tests/smoke.xs 2>&1)
if echo "$OUTPUT" | grep -q "smoke tests passed"; then
    echo "PASS: smoke test"
    exit 0
else
    echo "FAIL: smoke test"
    echo "$OUTPUT"
    exit 1
fi
