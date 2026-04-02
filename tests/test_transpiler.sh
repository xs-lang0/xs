#!/bin/bash
# transpiler integration tests: emit C, compile, and run
cd "$(dirname "$0")/.."
pass=0
fail=0

test_c_transpile() {
    local desc="$1"
    local input="$2"
    local expected="$3"
    local tmpxs="/tmp/xs_trans_$$.xs"
    local tmpc="/tmp/xs_trans_$$.c"
    local tmpbin="/tmp/xs_trans_$$"

    echo "$input" > "$tmpxs"

    # transpile to C
    if ! ./xs --emit c "$tmpxs" > "$tmpc" 2>/dev/null; then
        fail=$((fail + 1))
        echo "  FAIL  $desc (transpile failed)"
        rm -f "$tmpxs" "$tmpc" "$tmpbin"
        return
    fi

    # compile C
    if ! gcc -o "$tmpbin" "$tmpc" -lm 2>/dev/null; then
        fail=$((fail + 1))
        echo "  FAIL  $desc (C compile failed)"
        rm -f "$tmpxs" "$tmpc" "$tmpbin"
        return
    fi

    # run and check output
    local output
    output=$("$tmpbin" 2>&1)
    if [ "$output" = "$expected" ]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        echo "  FAIL  $desc"
        echo "        expected: $expected"
        echo "        got:      $output"
    fi
    rm -f "$tmpxs" "$tmpc" "$tmpbin"
}

# basic arithmetic
test_c_transpile "arithmetic" \
    'println(2 + 3 * 4)' \
    "14"

# string interpolation
test_c_transpile "string interp" \
    'let name = "world"
println("hello {name}")' \
    "hello world"

# if/else
test_c_transpile "if/else" \
    'let x = 10
if x > 5 { println("big") } else { println("small") }' \
    "big"

# functions
test_c_transpile "functions" \
    'fn add(a, b) { return a + b }
println(add(3, 4))' \
    "7"

# arrays
test_c_transpile "arrays" \
    'let arr = [1, 2, 3]
println(arr.len())' \
    "3"

# while loop
test_c_transpile "while loop" \
    'var sum = 0
var i = 1
while i <= 10 { sum = sum + i; i = i + 1 }
println(sum)' \
    "55"

echo ""
echo "transpiler tests: $pass passed, $fail failed"
[ $fail -eq 0 ] && exit 0 || exit 1
