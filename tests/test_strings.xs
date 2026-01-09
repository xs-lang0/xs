-- strings: literals, interpolation, methods, escape sequences

-- basic
assert_eq("hello", "hello")
assert_eq('hello', "hello")
assert_eq(len("hello"), 5)
assert_eq("" ++ "hi", "hi")

-- interpolation
let name = "world"
assert_eq("hello {name}", "hello world")
assert_eq("{1 + 2}", "3")
assert_eq("{name} is {len(name)} chars", "world is 5 chars")

-- escape sequences
assert_eq("\n".len(), 1)
assert_eq("\t".len(), 1)
assert_eq("\\".len(), 1)
assert_eq("\"".len(), 1)

-- concatenation
assert_eq("foo" ++ "bar", "foobar")
assert_eq("a" ++ "b" ++ "c", "abc")

-- methods
assert_eq("hello".upper(), "HELLO")
assert_eq("HELLO".lower(), "hello")
assert_eq("  hello  ".trim(), "hello")
assert_eq("hello world".split(" "), ["hello", "world"])
assert_eq("hello".contains("ell"), true)
assert_eq("hello".contains("xyz"), false)
assert_eq("hello".starts_with("hel"), true)
assert_eq("hello".ends_with("llo"), true)
assert_eq("hello".replace("l", "r"), "herro")
assert_eq("hello".len(), 5)
assert_eq("hello".reverse(), "olleh")
assert_eq(",".join(["a", "b", "c"]), "a,b,c")
assert_eq("hello".slice(1, 3), "el")
assert_eq("hello".index_of("ll"), 2)
assert_eq("ha".repeat(3), "hahaha")

-- str() conversion
assert_eq(str(42), "42")
assert_eq(str(3.14), "3.14")
assert_eq(str(true), "true")
assert_eq(str(null), "null")

-- triple-quoted strings
let multi = """hello
world"""
assert(multi.contains("\n"), "triple-quoted newline")

println("test_strings: all passed")
