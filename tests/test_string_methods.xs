-- Test string methods
-- Each test prints PASS or FAIL

var passed = 0
var failed = 0

fn assert_eq(actual, expected, label) {
    if actual == expected {
        passed = passed + 1
    } else {
        print("FAIL: " ++ label ++ " expected=" ++ str(expected) ++ " got=" ++ str(actual))
        failed = failed + 1
    }
}

fn assert_true(val, label) {
    assert_eq(val, true, label)
}

fn assert_false(val, label) {
    assert_eq(val, false, label)
}

-- len / length
assert_eq("hello".len(), 5, "len")
assert_eq("hello".length(), 5, "length alias")
assert_eq("".len(), 0, "len empty")

-- upper / to_upper
assert_eq("hello".upper(), "HELLO", "upper")
assert_eq("hello".to_upper(), "HELLO", "to_upper alias")

-- lower / to_lower
assert_eq("HELLO".lower(), "hello", "lower")
assert_eq("HELLO".to_lower(), "hello", "to_lower alias")

-- trim
assert_eq("  hello  ".trim(), "hello", "trim")
assert_eq("hello".trim(), "hello", "trim no-op")

-- trim_start / ltrim
assert_eq("  hello  ".trim_start(), "hello  ", "trim_start")
assert_eq("  hello  ".ltrim(), "hello  ", "ltrim alias")

-- trim_end / rtrim
assert_eq("  hello  ".trim_end(), "  hello", "trim_end")
assert_eq("  hello  ".rtrim(), "  hello", "rtrim alias")

-- starts_with
assert_true("hello world".starts_with("hello"), "starts_with true")
assert_false("hello world".starts_with("world"), "starts_with false")

-- ends_with
assert_true("hello world".ends_with("world"), "ends_with true")
assert_false("hello world".ends_with("hello"), "ends_with false")

-- contains / includes
assert_true("hello world".contains("lo wo"), "contains true")
assert_false("hello world".contains("xyz"), "contains false")
assert_true("hello world".includes("lo wo"), "includes alias true")
assert_false("hello world".includes("xyz"), "includes alias false")

-- split
assert_eq("a,b,c".split(",").len(), 3, "split count")
assert_eq("a,b,c".split(",")[0], "a", "split first")
assert_eq("a,b,c".split(",")[2], "c", "split last")
assert_eq("hello".split("").len(), 5, "split empty sep")

-- replace
assert_eq("hello world".replace("world", "xs"), "hello xs", "replace")
assert_eq("aaa".replace("a", "bb"), "bbbbbb", "replace all")
assert_eq("aaa".replace("a", "b", 2), "bba", "replace with count")
assert_eq("aaa".replace("a", "b", 1), "baa", "replace with count=1")

-- chars
assert_eq("abc".chars().len(), 3, "chars len")
assert_eq("abc".chars()[0], "a", "chars first")
assert_eq("abc".chars()[2], "c", "chars last")

-- bytes / to_bytes
assert_eq("A".bytes()[0], 65, "bytes A=65")
assert_eq("A".to_bytes()[0], 65, "to_bytes A=65")
assert_eq("abc".bytes().len(), 3, "bytes len")

-- parse_int
assert_eq("42".parse_int(), 42, "parse_int")
assert_eq("ff".parse_int(16), 255, "parse_int base 16")
assert_eq("101".parse_int(2), 5, "parse_int base 2")
assert_eq("77".parse_int(8), 63, "parse_int base 8")

-- parse_float
assert_eq("3.14".parse_float(), 3.14, "parse_float")

-- is_empty
assert_true("".is_empty(), "is_empty true")
assert_false("a".is_empty(), "is_empty false")

-- repeat
assert_eq("ab".repeat(3), "ababab", "repeat")
assert_eq("x".repeat(0), "", "repeat 0")
assert_eq("x".repeat(1), "x", "repeat 1")

-- join
assert_eq(", ".join(["a", "b", "c"]), "a, b, c", "join")
assert_eq("-".join(["x"]), "x", "join single")
assert_eq("".join(["a", "b"]), "ab", "join empty sep")

-- slice / substr / substring
assert_eq("hello".slice(1, 3), "el", "slice")
assert_eq("hello".substr(0, 2), "he", "substr")
assert_eq("hello".substring(2, 4), "ll", "substring alias")
assert_eq("hello".slice(-3), "llo", "slice negative start")

-- index_of / find
assert_eq("hello world".index_of("world"), 6, "index_of")
assert_eq("hello world".find("world"), 6, "find alias")
assert_eq("hello world".index_of("xyz"), -1, "index_of not found")

-- count
assert_eq("banana".count("a"), 3, "count")
assert_eq("banana".count("na"), 2, "count substr")
assert_eq("hello".count("z"), 0, "count zero")

-- to_str / to_string
assert_eq("hello".to_str(), "hello", "to_str")
assert_eq("hello".to_string(), "hello", "to_string")

-- title
assert_eq("hello world".title(), "Hello World", "title")
assert_eq("HELLO WORLD".title(), "Hello World", "title from upper")

-- capitalize
assert_eq("hello world".capitalize(), "Hello world", "capitalize")
assert_eq("HELLO".capitalize(), "Hello", "capitalize upper")

-- center
assert_eq("hi".center(6), "  hi  ", "center")
assert_eq("hi".center(7, "*"), "**hi***", "center with fill")
assert_eq("hello".center(3), "hello", "center no-op")

-- pad_left / pad_start
assert_eq("42".pad_left(5, "0"), "00042", "pad_left")
assert_eq("42".pad_start(5, "0"), "00042", "pad_start alias")
assert_eq("hello".pad_left(3), "hello", "pad_left no-op")

-- pad_right / pad_end
assert_eq("42".pad_right(5, "0"), "42000", "pad_right")
assert_eq("42".pad_end(5, "0"), "42000", "pad_end alias")
assert_eq("hello".pad_right(3), "hello", "pad_right no-op")

-- remove_prefix
assert_eq("hello world".remove_prefix("hello "), "world", "remove_prefix")
assert_eq("hello".remove_prefix("xyz"), "hello", "remove_prefix no match")

-- remove_suffix
assert_eq("hello world".remove_suffix(" world"), "hello", "remove_suffix")
assert_eq("hello".remove_suffix("xyz"), "hello", "remove_suffix no match")

-- is_ascii
assert_true("hello".is_ascii(), "is_ascii true")
assert_true("123 ABC".is_ascii(), "is_ascii mixed")

-- is_digit / is_numeric
assert_true("12345".is_digit(), "is_digit true")
assert_false("12a45".is_digit(), "is_digit false")
assert_false("".is_digit(), "is_digit empty")
assert_true("999".is_numeric(), "is_numeric alias")

-- is_alpha
assert_true("hello".is_alpha(), "is_alpha true")
assert_false("hello1".is_alpha(), "is_alpha false")
assert_false("".is_alpha(), "is_alpha empty")

-- is_alnum
assert_true("hello123".is_alnum(), "is_alnum true")
assert_false("hello 123".is_alnum(), "is_alnum false space")
assert_false("".is_alnum(), "is_alnum empty")

-- is_upper
assert_true("HELLO".is_upper(), "is_upper true")
assert_false("Hello".is_upper(), "is_upper false")
assert_true("HELLO 123".is_upper(), "is_upper with non-alpha")

-- is_lower
assert_true("hello".is_lower(), "is_lower true")
assert_false("Hello".is_lower(), "is_lower false")
assert_true("hello 123".is_lower(), "is_lower with non-alpha")

-- reverse
assert_eq("hello".reverse(), "olleh", "reverse")
assert_eq("".reverse(), "", "reverse empty")
assert_eq("a".reverse(), "a", "reverse single")

-- lines
assert_eq("a\nb\nc".lines().len(), 3, "lines count")
assert_eq("a\nb\nc".lines()[0], "a", "lines first")
assert_eq("a\nb\nc".lines()[2], "c", "lines last")

-- char_at
assert_eq("hello".char_at(0), "h", "char_at 0")
assert_eq("hello".char_at(4), "o", "char_at 4")
assert_eq("hello".char_at(-1), "o", "char_at negative")

-- format
assert_eq("Hello {}!".format("world"), "Hello world!", "format single")
assert_eq("{} + {} = {}".format(1, 2, 3), "1 + 2 = 3", "format multi")
assert_eq("no placeholders".format(), "no placeholders", "format none")

-- from_chars
assert_eq("".from_chars(["h", "i"]), "hi", "from_chars")
assert_eq("".from_chars(["a", "b", "c"]), "abc", "from_chars abc")

-- rfind
assert_eq("banana".rfind("a"), 5, "rfind")
assert_eq("banana".rfind("na"), 4, "rfind substr")
assert_eq("banana".rfind("z"), -1, "rfind not found")

-- truncate
assert_eq("hello world".truncate(8), "hello...", "truncate")
assert_eq("hi".truncate(10), "hi", "truncate no-op")

-- split_at
let parts = "hello".split_at(2)
assert_eq(parts[0], "he", "split_at left")
assert_eq(parts[1], "llo", "split_at right")

-- Summary
print("")
print("String methods test results:")
print("  Passed: " ++ str(passed))
print("  Failed: " ++ str(failed))
if failed == 0 {
    print("  ALL TESTS PASSED!")
} else {
    print("  SOME TESTS FAILED!")
}
