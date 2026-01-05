-- ===========================================
-- Test: Edge Cases and Boundary Conditions
-- ===========================================

var passed = 0
var failed = 0

fn check(cond, label) {
    if cond {
        passed = passed + 1
    } else {
        println("FAIL: " ++ label)
        failed = failed + 1
    }
}

-- === 1. Large integers ===
let big1 = 9007199254740992
check(big1 == 9007199254740992, "2^53 integer")

let big2 = 1000000000000
check(big2 == 1000000000000, "trillion")

let big3 = 999999999 * 999999999
check(big3 > 0, "large multiplication is positive")

-- === 2. Numeric separators ===
let sep = 1_000_000
check(sep == 1000000, "numeric separator underscore")

-- === 3. Integer arithmetic edge cases ===
check(0 * 0 == 0, "zero times zero")
check(1 * 0 == 0, "one times zero")
check(0 + 0 == 0, "zero plus zero")
check(0 - 0 == 0, "zero minus zero")
check((-1) * (-1) == 1, "neg times neg")

-- === 4. Division edge cases ===
check(0 / 1 == 0, "zero divided by one")
check(1 / 1 == 1, "one divided by one")
check((-1) / 1 == -1, "neg divided by pos")
check(1 / (-1) == -1, "pos divided by neg")
check((-1) / (-1) == 1, "neg divided by neg")

-- === 5. Modulo edge cases ===
check(0 % 1 == 0, "zero mod one")
check(5 % 5 == 0, "n mod n is 0")
check(1 % 3 == 1, "small mod large")

-- === 6. Float special values ===
let inf = 1.0 / 0.0
let neg_inf = -1.0 / 0.0
let nan_val = 0.0 / 0.0

check(type(inf) == "float", "infinity is float")
check(type(neg_inf) == "float", "neg infinity is float")
check(type(nan_val) == "float", "NaN is float")

-- NaN is not equal to itself
check(nan_val != nan_val, "NaN != NaN")

-- === 7. Negative zero ===
let neg_zero = -0.0
let pos_zero = 0.0
check(neg_zero == pos_zero, "-0.0 == 0.0")

-- === 8. Float precision ===
check(0.1 + 0.2 != 0.3, "float precision: 0.1+0.2 != 0.3 (IEEE 754)")
let diff = (0.1 + 0.2) - 0.3
check(diff > 0.0, "float precision difference > 0")

-- === 9. Empty array operations ===
let empty_arr = []
check(len(empty_arr) == 0, "empty array length 0")
check(empty_arr.len() == 0, "empty array .len() 0")
check(empty_arr.is_empty(), "empty array is_empty")
check(empty_arr.first() == null, "empty array first is null")
check(empty_arr.last() == null, "empty array last is null")

-- === 10. Empty array higher-order functions ===
let mapped_empty = [].map(|x| x * 2)
check(len(mapped_empty) == 0, "map empty array")

let filtered_empty = [].filter(|x| x > 0)
check(len(filtered_empty) == 0, "filter empty array")

let folded_empty = [].fold(0, |acc, x| acc + x)
check(folded_empty == 0, "fold empty array with init")

-- === 11. Single-element collections ===
let single = [42]
check(single.first() == 42, "single elem first")
check(single.last() == 42, "single elem last")
check(single.len() == 1, "single elem len")
check(!single.is_empty(), "single elem not empty")

-- === 12. Empty string ===
let empty_str = ""
check(empty_str.len() == 0, "empty string length")
check(empty_str == "", "empty string equality")
check("" ++ "" == "", "concat two empty strings")
check("" ++ "a" == "a", "concat empty with non-empty")
check("a" ++ "" == "a", "concat non-empty with empty")

-- === 13. String with special content ===
let newline_str = "hello\nworld"
check(newline_str.len() > 5, "string with newline has length")

let tab_str = "hello\tworld"
check(tab_str.len() > 5, "string with tab has length")

-- === 14. Very long string ===
var long_str = ""
for i in range(500) {
    long_str = long_str ++ "x"
}
check(long_str.len() == 500, "500 char string length")

-- === 15. Deeply nested arrays ===
var nested = [1]
for i in range(20) {
    nested = [nested]
}
check(type(nested) == "array", "deeply nested is still array")
check(nested.len() == 1, "deeply nested outer len is 1")

-- === 16. Array with mixed types ===
let mixed = [1, "two", 3.0, true, null, [4, 5]]
check(mixed.len() == 6, "mixed type array length")
check(type(mixed[0]) == "int", "mixed[0] is int")
check(type(mixed[1]) == "str", "mixed[1] is str")
check(type(mixed[2]) == "float", "mixed[2] is float")
check(type(mixed[3]) == "bool", "mixed[3] is bool")
check(mixed[4] == null, "mixed[4] is null")
check(type(mixed[5]) == "array", "mixed[5] is array")

-- === 17. Null operations ===
check(null == null, "null equals null")
check(null is null, "null is null")
check(type(null) == "null", "type of null")

-- === 18. Null coalesce ===
let nc1 = null ?? "default"
check(nc1 == "default", "null coalesce with null")

let nc2 = "value" ?? "default"
check(nc2 == "value", "null coalesce with non-null")

let nc3 = 0 ?? "default"
check(nc3 == 0, "null coalesce with zero (not null)")

let nc4 = false ?? "default"
check(nc4 == false, "null coalesce with false (not null)")

-- === 19. Boolean edge cases ===
check(true == true, "true == true")
check(false == false, "false == false")
check(true != false, "true != false")
check(!false == true, "not false is true")
check(!true == false, "not true is false")

-- === 20. Comparison operators ===
check(1 < 2, "1 < 2")
check(2 > 1, "2 > 1")
check(1 <= 1, "1 <= 1")
check(1 >= 1, "1 >= 1")
check(1 <= 2, "1 <= 2")
check(2 >= 1, "2 >= 1")

-- === 21. String comparison ===
check("abc" == "abc", "string equality")
check("abc" != "def", "string inequality")
check("" == "", "empty string equality")
check("a" != "b", "different strings not equal")

-- === 22. Type coercion via str() ===
check(str(42) == "42", "str(int)")
check(str(3.14) == "3.14" || str(3.14) == "3.140000", "str(float)")
check(str(true) == "true", "str(true)")
check(str(false) == "false", "str(false)")
check(str(null) == "null", "str(null)")

-- === 23. Array of arrays ===
let matrix = [[1, 2, 3], [4, 5, 6], [7, 8, 9]]
check(matrix.len() == 3, "matrix rows")
check(matrix[0].len() == 3, "matrix cols")
check(matrix[1][1] == 5, "matrix[1][1]")
check(matrix[2][2] == 9, "matrix[2][2]")

-- === 24. Negative array indexing ===
let arr = [10, 20, 30, 40, 50]
check(arr[-1] == 50, "negative index -1")
check(arr[-2] == 40, "negative index -2")
check(arr[-5] == 10, "negative index -5")

-- === 25. Map with various key types ===
let m = {"key1": 1, "key2": 2, "key3": 3}
check(m["key1"] == 1, "map string key access")
check(m["key2"] == 2, "map second key")
check(len(m) == 3, "map length")

-- === 26. Empty map ===
let empty_map = #{}
check(type(empty_map) == "map", "empty map type")
check(len(empty_map) == 0, "empty map length")

-- === 27. Range edge cases ===
let r0 = range(0)
var r0_count = 0
for x in r0 { r0_count = r0_count + 1 }
check(r0_count == 0, "range(0) is empty")

let r1 = range(1)
var r1_items = []
for x in r1 { r1_items.push(x) }
check(r1_items.len() == 1, "range(1) has 1 element")
check(r1_items[0] == 0, "range(1) starts at 0")

-- === 28. Chained string operations ===
let chain_str = "  Hello, World!  "
let trimmed = chain_str.trim()
check(trimmed == "Hello, World!", "trim whitespace")
let upper = trimmed.upper()
check(upper == "HELLO, WORLD!", "chained upper")

-- === 29. Struct with default-like values ===
struct SimplePoint { x: int, y: int }
let sp = SimplePoint { x: 0, y: 0 }
check(sp.x == 0, "struct zero field x")
check(sp.y == 0, "struct zero field y")

-- === 30. Type checking with is ===
check(42 is int, "42 is int")
check(3.14 is float, "3.14 is float")
check("hello" is str, "hello is str")
check(true is bool, "true is bool")
check(null is null, "null is null check")

-- Summary
println("")
println("Edge Case Test Results:")
println("  Passed: " ++ str(passed))
println("  Failed: " ++ str(failed))
if failed > 0 {
    println("SOME TESTS FAILED!")
} else {
    println("ALL EDGE CASE TESTS PASSED!")
}
