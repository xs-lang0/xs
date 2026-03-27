-- type system: bigints, char type, type annotations, --check behavior

-- bigint auto-promotion on overflow
let big = 9223372036854775807 + 1
assert(big > 0, "bigint should be positive")
assert_eq(type(big), "int")
assert(is_int(big), "is_int should be true for bigint")

-- bigint arithmetic
assert_eq(2 ** 64, 18446744073709551616)
assert_eq(2 ** 100 / 2 ** 99, 2)
let huge = 10 ** 30
assert(huge > 10 ** 29, "big exponent comparison")

-- bigint to float conversion
let bf = float(2 ** 53)
assert_eq(type(bf), "float")

-- bigint equality with int
let max = 9223372036854775807
assert(max == max, "i64 max equals itself")
assert(max + 1 != max, "overflow value differs")

-- char literals
assert_eq(type('a'), "char")
assert_eq(type('z'), "char")
assert_eq('a', 'a')
assert('a' != 'b', "different chars not equal")

-- multi-char single-quoted strings are still strings
assert_eq(type('hello'), "str")

-- type annotations on variables
let x: int = 42
assert_eq(x, 42)
var name: str = "test"
assert_eq(name, "test")

-- struct field type annotations
struct TypedPoint { x: int, y: int }
let tp = TypedPoint { x: 10, y: 20 }
assert_eq(tp.x, 10)
assert_eq(tp.y, 20)

-- struct with defaults and types
struct Config {
    host: str = "localhost",
    port: int = 8080
}
let c = Config {}
assert_eq(c.host, "localhost")
assert_eq(c.port, 8080)
let c2 = Config { port: 3000 }
assert_eq(c2.port, 3000)

-- function type annotations
fn add(a: int, b: int) -> int {
    return a + b
}
assert_eq(add(3, 4), 7)

-- type() for all types
assert_eq(type(42), "int")
assert_eq(type(3.14), "float")
assert_eq(type("hi"), "str")
assert_eq(type(true), "bool")
assert_eq(type(null), "null")
assert_eq(type([1,2]), "array")
assert_eq(type(#{"a": 1}), "map")
assert_eq(type((1,2)), "tuple")
assert_eq(type(0..5), "range")
assert_eq(type(fn(x) { x }), "fn")

-- is_* checks
assert(is_int(42), "is_int")
assert(is_float(3.14), "is_float")
assert(is_str("hi"), "is_str")
assert(is_bool(true), "is_bool")
assert(is_null(null), "is_null")
assert(is_array([1]), "is_array")
assert(is_fn(fn() {}), "is_fn")

-- conversions
assert_eq(int(3.9), 3)
assert_eq(int(-3.9), -3)
assert_eq(float(42), 42.0)
assert_eq(str(42), "42")
assert_eq(bool(1), true)
assert_eq(bool(0), false)
assert_eq(bool(""), false)
assert_eq(bool("x"), true)

-- cast operator
assert_eq(42 as str, "42")
assert_eq(42 as float, 42.0)
assert_eq("42" as int, 42)

-- bigint number methods
assert((2 ** 100).is_even(), "big is_even")
assert((2 ** 100 + 1).is_odd(), "big is_odd")
assert_eq(((-5)).abs(), 5)

-- operator overloading
struct Vec2 { x, y }
impl Vec2 {
    fn +(self, other) {
        return Vec2 { x: self.x + other.x, y: self.y + other.y }
    }
}
let va = Vec2 { x: 1, y: 2 }
let vb = Vec2 { x: 3, y: 4 }
let vc = va + vb
assert_eq(vc.x, 4)
assert_eq(vc.y, 6)

-- string prefix pattern in match
fn check_url(url) {
    return match url {
        "https://" ++ rest => "secure"
        "http://" ++ rest => "insecure"
        _ => "unknown"
    }
}
assert_eq(check_url("https://example.com"), "secure")
assert_eq(check_url("http://example.com"), "insecure")
assert_eq(check_url("ftp://example.com"), "unknown")

-- for (k, v) in map
let m = #{"x": 10, "y": 20}
var kv_pairs = []
for (k, v) in m {
    kv_pairs.push("{k}:{v}")
}
assert_eq(kv_pairs.sort(), ["x:10", "y:20"])

-- for k in map (backward compat)
var just_keys = []
for k in m { just_keys.push(k) }
assert_eq(just_keys.sort(), ["x", "y"])

-- struct field defaults
struct Defaults { a = 1, b = 2, c = 3 }
let d = Defaults {}
assert_eq(d.a, 1)
assert_eq(d.b, 2)
let d2 = Defaults { b: 99 }
assert_eq(d2.b, 99)

-- type aliases (just parsing, no enforcement yet)
type UserId = i64
