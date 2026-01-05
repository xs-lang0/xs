-- ============================================================
-- Comprehensive String Features Test Suite
-- ============================================================

var pass_count = 0
var fail_count = 0

fn check(cond, msg) {
    if cond {
        pass_count = pass_count + 1
    } else {
        fail_count = fail_count + 1
        println("  FAIL: {msg}")
    }
}

-- ============================================================
-- 1. Basic string features
-- ============================================================
println("--- 1. Basic Strings ---")

check("hello" == "hello", "basic string equality")
check("hello".len() == 5, "string length")
check("" == "", "empty string")
check("".len() == 0, "empty string length")

-- Escape sequences
check("\n".len() == 1, "newline escape")
check("\t".len() == 1, "tab escape")
check("\\".len() == 1, "backslash escape")
check("\"".len() == 1, "quote escape")

-- String concatenation
check("hello" ++ " " ++ "world" == "hello world", "string concat")

-- ============================================================
-- 2. String Interpolation
-- ============================================================
println("--- 2. String Interpolation ---")

-- Simple variable interpolation
let name = "World"
check("Hello, {name}!" == "Hello, World!", "simple var interpolation")

-- Expression interpolation
let x = 3
let y = 4
check("{x + y}" == "7", "expression interpolation")
check("{x * y}" == "12", "multiply interpolation")

-- Function call in interpolation
fn double(n) { n * 2 }
check("{double(5)}" == "10", "fn call interpolation")
check("{double(x)}" == "6", "fn call with var interpolation")

-- Method call in interpolation
let arr = [1, 2, 3]
check("{arr.len()}" == "3", "method call interpolation")

-- Array indexing in interpolation
let colors = ["red", "green", "blue"]
check("{colors[0]}" == "red", "array index interpolation")

-- Nested string in interpolation
fn greet(who) { "Hello, " ++ who }
check("{greet("World")}" == "Hello, World", "nested string in interpolation")

-- Multiple interpolations in one string
check("{x} + {y} = {x + y}" == "3 + 4 = 7", "multiple interpolations")

-- Empty interpolation (literal braces)
check("{}" == "{}", "empty braces as literal")

-- Boolean/null in interpolation
let b = true
check("{b}" == "true", "bool interpolation")
let n = null
check("{n}" == "null", "null interpolation")

-- If expression in interpolation
check("{if x > 2 { "big" } else { "small" }}" == "big", "if-expr in interpolation")

-- Complex expression: map access
let m = {"key": "value"}
check("{m["key"]}" == "value", "map access in interpolation")

-- ============================================================
-- 3. Multi-line Strings (Triple-Quoted)
-- ============================================================
println("--- 3. Multi-line Strings ---")

-- Basic triple-quoted string with dedent
let ml1 = """
    Hello
    World
    """
check(ml1 == "Hello\nWorld\n", "triple-quote dedent")

-- Triple-quoted with varying indentation
let ml2 = """
    line1
        line2 (extra indent)
    line3
    """
check(ml2 == "line1\n    line2 (extra indent)\nline3\n", "varying indent preserved")

-- Triple-quoted with empty lines
let ml3 = """
    Hello

    World
    """
check(ml3 == "Hello\n\nWorld\n", "empty line preserved in triple-quote")

-- Triple-quoted with interpolation
let lang = "XS"
let ml4 = """
    Hello, {lang}!
    Welcome.
    """
check(ml4 == "Hello, XS!\nWelcome.\n", "interpolation in triple-quote")

-- Triple-quoted with escape sequences
let ml5 = """
    Tab:\there
    """
check(ml5 == "Tab:\there\n", "escape in triple-quote")

-- Triple single-quoted string
let ml6 = '''
    Single quoted
    Multi-line
    '''
check(ml6 == "Single quoted\nMulti-line\n", "triple single-quote")

-- ============================================================
-- 4. Raw Strings (Backtick)
-- ============================================================
println("--- 4. Raw Strings ---")

-- Basic raw string
let raw1 = `hello\nworld`
check(raw1.len() == 12, "raw string no escape processing")
check(raw1 == "hello\\nworld", "raw string literal backslash-n")

-- Raw string with special chars
let raw2 = `path\to\file`
check(raw2 == "path\\to\\file", "raw string with backslashes")

-- Raw string with braces (no interpolation)
let raw3 = `{hello_raw}`
check(raw3 == "\{hello_raw\}", "raw string no interpolation")

-- Triple backtick raw string with dedent
let raw4 = ```
    Hello
    World
    ```
check(raw4 == "Hello\nWorld\n", "triple backtick dedent")

-- Triple backtick with varying indentation
let raw5 = ```
    line1
        line2
    line3
    ```
check(raw5 == "line1\n    line2\nline3\n", "triple backtick varying indent")

-- Triple backtick preserves backslashes (no escape processing)
let raw6 = ```
    Hello\tWorld
    No\nEscape
    ```
check(raw6 == "Hello\\tWorld\nNo\\nEscape\n", "triple backtick no escapes")

-- ============================================================
-- 5. Colored Strings
-- ============================================================
println("--- 5. Colored Strings ---")

-- Color strings use format: c"style;style;...;text"
-- The ANSI escape codes are embedded in the string
let red = c"red;hello"
check(red.len() > 5, "color string adds ANSI codes")
-- Check it contains the text
check(red.contains("hello"), "color string contains text")

-- Multiple styles
let bold_green = c"green;bold;world"
check(bold_green.contains("world"), "multi-style color string contains text")

-- Color with interpolation
let val = 42
let colored_interp = c"cyan;Value: {val}"
check(colored_interp.contains("42"), "color string with interpolation")

-- No style (plain text)
let plain = c"Just text"
check(plain == "Just text", "color string no style is plain")

-- ============================================================
-- 6. r"..." Raw Strings (Double-Quote Syntax)
-- ============================================================
println("--- 6. r-prefix Raw Strings ---")

let rstr = r"hello\nworld"
check(rstr.len() == 12, "r-string no escape")
check(rstr == "hello\\nworld", "r-string literal content")

-- Triple r-string
let rml = r"""
hello\t
world\n
"""
-- r""" strips leading newline, keeps content raw
check(rml.contains("hello\\t"), "r-triple contains raw tab escape")
check(rml.contains("world\\n"), "r-triple contains raw newline escape")

-- ============================================================
-- 7. String Methods with Interpolation
-- ============================================================
println("--- 7. String Methods in Interpolation ---")

let s = "Hello, World!"
check("{s.len()}" == "13", "len() in interpolation")
check("{s.upper()}" == "HELLO, WORLD!", "upper() in interpolation")
check("{s.lower()}" == "hello, world!", "lower() in interpolation")

let words = "one two three"
check("{words.split(" ").len()}" == "3", "split().len() in interpolation")

-- Chained method calls
check("{"hello".upper()}" == "HELLO", "chained method in interpolation")

-- ============================================================
-- 8. Edge Cases
-- ============================================================
println("--- 8. Edge Cases ---")

-- Adjacent interpolations
check("{x}{y}" == "34", "adjacent interpolations")

-- Interpolation at start and end
check("{x} hello {y}" == "3 hello 4", "interp at start/end")

-- String with only interpolation
check("{name}" == "World", "string is only interpolation")

-- Deeply nested braces in interpolation
let nested_map = {"a": {"b": 1}}
check("{nested_map["a"]["b"]}" == "1", "nested map access in interpolation")

-- Arithmetic in interpolation
check("{(10 + 20) * 3}" == "90", "complex arithmetic in interpolation")

-- ============================================================
-- Report
-- ============================================================
println("\n=== String Features Test Results ===")
println("  Passed: {pass_count}")
println("  Failed: {fail_count}")
if fail_count == 0 {
    println("  ALL TESTS PASSED!")
} else {
    println("  SOME TESTS FAILED")
}
