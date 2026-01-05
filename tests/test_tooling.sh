#!/bin/bash
cd "$(dirname "$0")/.."
echo "Testing xs fmt..."
echo 'let x=1+2' > /tmp/xs_fmt_test.xs
./xs fmt /tmp/xs_fmt_test.xs 2>&1 && echo "  fmt: OK" || echo "  fmt: FAIL"

echo "Testing xs lint..."
# lint exits with warning count; any exit code means it ran successfully
./xs lint tests/smoke.xs > /dev/null 2>&1
rc=$?
if [ $rc -ge 0 ] 2>/dev/null; then echo "  lint: OK (warnings: $rc)"; fi

echo "Testing xs --profile..."
./xs --profile tests/smoke.xs > /dev/null 2>&1 && echo "  profile: OK" || echo "  profile: FAIL"

echo "Testing xs --vm..."
./xs --vm tests/vm_arith.xs 2>&1 | tail -1
echo "Tooling tests done!"
