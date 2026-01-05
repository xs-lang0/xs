-- Test advanced pattern matching features

var passed = 0
var failed = 0

fn assert_eq(actual, expected, label) {
    if actual == expected {
        passed = passed + 1
    } else {
        print("FAIL: " ++ label ++ " - expected " ++ str(expected) ++ ", got " ++ str(actual))
        failed = failed + 1
    }
}

-- ============================================================
-- 1. OR patterns: x | y => ...
-- ============================================================
fn test_or_patterns() {
    let r1 = match 1 {
        1 | 2 | 3 => "small"
        _ => "other"
    }
    assert_eq(r1, "small", "or pattern matches first")

    let r2 = match 3 {
        1 | 2 | 3 => "small"
        _ => "other"
    }
    assert_eq(r2, "small", "or pattern matches last")

    let r3 = match 5 {
        1 | 2 | 3 => "small"
        _ => "other"
    }
    assert_eq(r3, "other", "or pattern falls through to wildcard")

    -- OR with string patterns
    let r4 = match "hello" {
        "hi" | "hello" | "hey" => "greeting"
        _ => "other"
    }
    assert_eq(r4, "greeting", "or pattern with strings")

    -- OR with boolean patterns
    let r5 = match true {
        true | false => "boolean"
    }
    assert_eq(r5, "boolean", "or pattern with booleans")
}

-- ============================================================
-- 2. Guard patterns: x if x > 0 => ...
-- ============================================================
fn test_guard_patterns() {
    let r1 = match 42 {
        n if n < 0 => "negative"
        0 => "zero"
        n if n > 100 => "large"
        _ => "other"
    }
    assert_eq(r1, "other", "guard pattern: 42 is other")

    let r2 = match -5 {
        n if n < 0 => "negative"
        0 => "zero"
        n if n > 100 => "large"
        _ => "other"
    }
    assert_eq(r2, "negative", "guard pattern: -5 is negative")

    let r3 = match 0 {
        n if n < 0 => "negative"
        0 => "zero"
        n if n > 100 => "large"
        _ => "other"
    }
    assert_eq(r3, "zero", "guard pattern: 0 is zero")

    let r4 = match 200 {
        n if n < 0 => "negative"
        0 => "zero"
        n if n > 100 => "large"
        _ => "other"
    }
    assert_eq(r4, "large", "guard pattern: 200 is large")

    -- Guard with binding used in body
    let r5 = match 7 {
        n if n % 2 == 0 => "even: " ++ str(n)
        n => "odd: " ++ str(n)
    }
    assert_eq(r5, "odd: 7", "guard with binding in body")

    -- Guard with even check
    let r6 = match 8 {
        n if n % 2 == 0 => "even: " ++ str(n)
        n => "odd: " ++ str(n)
    }
    assert_eq(r6, "even: 8", "guard even check")
}

-- ============================================================
-- 3. Nested destructuring: tuples with arrays
-- ============================================================
fn test_nested_destructuring() {
    -- Tuple of arrays
    let pair = ([1, 2], [3, 4])
    let r1 = match pair {
        ([a, b], [c, d]) => a + b + c + d
        _ => -1
    }
    assert_eq(r1, 10, "nested tuple of arrays")

    -- Array of tuples (nested arrays)
    let data = [(1, "a"), (2, "b")]
    let r2 = match data {
        [(x, s), _] => str(x) ++ s
        _ => "none"
    }
    assert_eq(r2, "1a", "array of tuples destructuring")

    -- Deeply nested
    let deep = (1, (2, (3, 4)))
    let r3 = match deep {
        (a, (b, (c, d))) => a + b + c + d
        _ => -1
    }
    assert_eq(r3, 10, "deeply nested tuple destructuring")

    -- Mixed: tuple with wildcard
    let mix = (10, [20, 30])
    let r4 = match mix {
        (x, [_, y]) => x + y
        _ => -1
    }
    assert_eq(r4, 40, "tuple with array and wildcard")
}

-- ============================================================
-- 4. String prefix/suffix patterns: "hello" ++ rest => ...
-- ============================================================
fn test_string_patterns() {
    let s1 = "hello world"
    let r1 = match s1 {
        "hello" ++ rest => "prefix matched, rest=" ++ rest
        _ => "no match"
    }
    assert_eq(r1, "prefix matched, rest= world", "string prefix pattern")

    -- Exact prefix match (rest is empty)
    let r2 = match "hello" {
        "hello" ++ rest => "rest=" ++ rest
        _ => "no match"
    }
    assert_eq(r2, "rest=", "string prefix exact match")

    -- No match
    let r3 = match "goodbye" {
        "hello" ++ rest => "matched"
        _ => "no match"
    }
    assert_eq(r3, "no match", "string prefix no match")

    -- Multiple prefix attempts
    let r4 = match "https://example.com" {
        "http://" ++ url => "http: " ++ url
        "https://" ++ url => "https: " ++ url
        _ => "unknown protocol"
    }
    assert_eq(r4, "https: example.com", "string prefix protocol parsing")

    -- Empty prefix matches everything
    let r5 = match "anything" {
        "" ++ rest => rest
        _ => "no match"
    }
    assert_eq(r5, "anything", "empty prefix matches all")
}

-- ============================================================
-- 5. Wildcard in arrays: [_, x, _] captures middle
-- ============================================================
fn test_wildcard_in_arrays() {
    let arr = [10, 20, 30]
    let r1 = match arr {
        [_, x, _] => x
        _ => -1
    }
    assert_eq(r1, 20, "wildcard in array captures middle element")

    let r2 = match [1, 2, 3, 4, 5] {
        [_, _, mid, ..rest] => mid
        _ => -1
    }
    assert_eq(r2, 3, "wildcard with rest in array")

    -- Empty array
    let r3 = match [] {
        [] => "empty"
        _ => "not empty"
    }
    assert_eq(r3, "empty", "empty array pattern")

    -- Single element
    let r4 = match [42] {
        [x] => x
        _ => -1
    }
    assert_eq(r4, 42, "single element array pattern")

    -- All wildcards
    let r5 = match [1, 2, 3] {
        [_, _, _] => "three"
        _ => "other"
    }
    assert_eq(r5, "three", "all wildcards in array")
}

-- ============================================================
-- 6. Slice patterns with rest
-- ============================================================
fn test_slice_rest() {
    let arr = [1, 2, 3, 4, 5]
    let r1 = match arr {
        [a, b, ..rest] => str(a) ++ "," ++ str(b) ++ " rest=" ++ str(len(rest))
        _ => "none"
    }
    assert_eq(r1, "1,2 rest=3", "slice pattern with rest")

    let r2 = match [1] {
        [a, b, ..rest] => "two+"
        [a] => "one"
        [] => "empty"
        _ => "other"
    }
    assert_eq(r2, "one", "slice pattern single element")

    -- Rest captures remaining elements
    let r3 = match [10, 20, 30, 40] {
        [first, ..tail] => first + len(tail)
        _ => -1
    }
    assert_eq(r3, 13, "slice pattern rest capture")
}

-- ============================================================
-- 7. Range patterns
-- ============================================================
fn test_range_patterns() {
    let r1 = match 5 {
        0..=3 => "low"
        4..=7 => "mid"
        8..=10 => "high"
        _ => "out"
    }
    assert_eq(r1, "mid", "range pattern inclusive")

    let r2 = match 0 {
        0..=3 => "low"
        _ => "other"
    }
    assert_eq(r2, "low", "range pattern lower bound")

    let r3 = match 3 {
        0..=3 => "low"
        _ => "other"
    }
    assert_eq(r3, "low", "range pattern upper bound inclusive")
}

-- ============================================================
-- 8. Negative number patterns
-- ============================================================
fn test_negative_patterns() {
    let r1 = match -1 {
        -1 => "neg one"
        0 => "zero"
        1 => "one"
        _ => "other"
    }
    assert_eq(r1, "neg one", "negative int literal pattern")

    let r2 = match -42 {
        -1 => "neg one"
        -42 => "neg forty-two"
        _ => "other"
    }
    assert_eq(r2, "neg forty-two", "negative int -42 pattern")

    let r3 = match 0 {
        -1 => "neg one"
        0 => "zero"
        1 => "one"
        _ => "other"
    }
    assert_eq(r3, "zero", "zero after negative pattern")

    -- Negative in range
    let r4 = match -3 {
        -10..=0 => "negative range"
        _ => "other"
    }
    assert_eq(r4, "negative range", "negative number in range pattern")
}

-- ============================================================
-- 9. Enum / variant patterns
-- ============================================================
fn test_enum_patterns() {
    enum Option {
        Some(value)
        None
    }

    let val = Option::Some(42)
    let r1 = match val {
        Option::Some(x) => x
        Option::None => -1
    }
    assert_eq(r1, 42, "enum Some pattern")

    let none_val = Option::None
    let r2 = match none_val {
        Option::Some(x) => x
        Option::None => -1
    }
    assert_eq(r2, -1, "enum None pattern")

    -- Nested enum in tuple
    let s1 = Option::Some(10)
    let s2 = Option::Some(20)
    let pair = (s1, s2)
    let r3 = match pair {
        (Option::Some(a), Option::Some(b)) => a + b
        _ => -1
    }
    assert_eq(r3, 30, "nested enum in tuple")
}

-- ============================================================
-- 10. Struct destructuring patterns
-- ============================================================
fn test_struct_patterns() {
    struct Point {
        x: i64
        y: i64
    }

    let p = Point { x: 3, y: 4 }
    let r1 = match p {
        Point { x, y } => x + y
        _ => -1
    }
    assert_eq(r1, 7, "struct destructuring in match")

    -- Struct with nested pattern on field
    let r2 = match p {
        Point { x: 3, y } => "x is 3, y=" ++ str(y)
        _ => "other"
    }
    assert_eq(r2, "x is 3, y=4", "struct field sub-pattern")
}

-- ============================================================
-- 11. Match as expression
-- ============================================================
fn test_match_expression() {
    let x = 5
    let result = match x {
        1 => "one"
        2 => "two"
        n if n > 3 => "big"
        _ => "other"
    }
    assert_eq(result, "big", "match as expression")
}

-- ============================================================
-- 12. Combined patterns (OR + guard, nested + wildcard, capture + range)
-- ============================================================
fn test_combined_patterns() {
    -- OR pattern on strings with guard
    let val = 15
    let r1 = match val {
        n if n > 10 => "big"
        1 | 2 | 3 => "small"
        _ => "medium"
    }
    assert_eq(r1, "big", "guard before or pattern")

    -- Wildcard in tuple
    let t = (1, 2, 3)
    let r2 = match t {
        (_, x, _) => x
        _ => -1
    }
    assert_eq(r2, 2, "wildcard in tuple")

    -- Capture pattern: x @ pat
    let r3 = match 5 {
        x @ 1..=10 => "in range: " ++ str(x)
        _ => "out"
    }
    assert_eq(r3, "in range: 5", "capture with range pattern")

    -- OR pattern with negative numbers
    let r4 = match -2 {
        -1 | -2 | -3 => "small negative"
        _ => "other"
    }
    assert_eq(r4, "small negative", "or pattern with negative numbers")

    -- Nested wildcard in tuple of arrays
    let nested = ([1, 2], [3, 4])
    let r5 = match nested {
        ([_, b], [c, _]) => b + c
        _ => -1
    }
    assert_eq(r5, 5, "nested wildcard in tuple of arrays")
}

-- ============================================================
-- Run all tests
-- ============================================================
test_or_patterns()
test_guard_patterns()
test_nested_destructuring()
test_wildcard_in_arrays()
test_string_patterns()
test_slice_rest()
test_range_patterns()
test_negative_patterns()
test_enum_patterns()
test_struct_patterns()
test_match_expression()
test_combined_patterns()

print("")
print("=== Advanced Pattern Matching Tests ===")
print("Passed: " ++ str(passed))
print("Failed: " ++ str(failed))

if failed > 0 {
    print("SOME TESTS FAILED!")
} else {
    print("ALL TESTS PASSED!")
}
