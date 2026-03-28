-- advanced language features: generators, traits, regex, signals, patterns

-- generators
fn* range_gen(start, end) {
    var i = start
    while i < end {
        yield i
        i = i + 1
    }
}
var gen_sum = 0
for x in range_gen(1, 6) { gen_sum = gen_sum + x }
assert_eq(gen_sum, 15)

fn* fib_gen(n) {
    var a = 0
    var b = 1
    var i = 0
    while i < n {
        yield a
        let tmp = a + b
        a = b
        b = tmp
        i = i + 1
    }
}
var fibs = []
for f in fib_gen(7) { fibs.push(f) }
assert_eq(fibs, [0, 1, 1, 2, 3, 5, 8])

-- trait default methods
trait Greetable {
    fn greet(self) -> str {
        return "hello from {self.name}"
    }
    fn shout(self) -> str
}
struct Person { name }
impl Greetable for Person {
    fn shout(self) -> str { return "HEY {self.name}!" }
}
let p = Person { name: "Alice" }
assert_eq(p.shout(), "HEY Alice!")
-- default method:
assert_eq(p.greet(), "hello from Alice")

-- trait method signature checking (param count, return type)
trait Stringify {
    fn to_s(self) -> str
}
struct Num { val }
impl Stringify for Num {
    fn to_s(self) -> str { return "{self.val}" }
}
let n = Num { val: 42 }
assert_eq(n.to_s(), "42")

-- operator overloading (all operators)
struct Vec2 { x, y }
impl Vec2 {
    fn +(self, other) { return Vec2 { x: self.x + other.x, y: self.y + other.y } }
    fn -(self, other) { return Vec2 { x: self.x - other.x, y: self.y - other.y } }
    fn *(self, s) { return Vec2 { x: self.x * s, y: self.y * s } }
}
let va = Vec2 { x: 1, y: 2 }
let vb = Vec2 { x: 3, y: 4 }
assert_eq((va + vb).x, 4)
assert_eq((va + vb).y, 6)
assert_eq((vb - va).x, 2)
assert_eq((va * 3).x, 3)
assert_eq((va * 3).y, 6)

-- regex type and methods
let digits = /[0-9]+/
assert_eq(type(digits), "re")
assert(digits.test("abc 42 def"), "regex test")
assert_eq(digits.match("abc 42 def"), "42")
assert_eq(digits.replace("abc 42 def", "XX"), "abc XX def")
assert_eq(digits.source(), "[0-9]+")

-- regex in match (auto-anchored)
fn classify(s) {
    return match s {
        /[0-9]+/ => "number"
        /[a-zA-Z_][a-zA-Z0-9_]*/ => "ident"
        _ => "other"
    }
}
assert_eq(classify("42"), "number")
assert_eq(classify("hello"), "ident")
assert_eq(classify("???"), "other")

-- string prefix patterns in match
fn check_proto(url) {
    return match url {
        "https://" ++ rest => "secure"
        "http://" ++ rest => "insecure"
        _ => "unknown"
    }
}
assert_eq(check_proto("https://example.com"), "secure")
assert_eq(check_proto("http://example.com"), "insecure")
assert_eq(check_proto("ftp://files.com"), "unknown")

-- for (k, v) in map
let m = #{"a": 1, "b": 2, "c": 3}
var pairs = []
for (k, v) in m { pairs.push("{k}={v}") }
assert_eq(pairs.sort(), ["a=1", "b=2", "c=3"])

-- for k in map (backward compat)
var ks = []
for k in m { ks.push(k) }
assert_eq(ks.sort(), ["a", "b", "c"])

-- map methods
let doubled = m.map(fn(k, v) { v * 2 })
assert_eq(doubled["a"], 2)
let big = m.filter(fn(k, v) { v > 1 })
assert(big["a"] == null || !big.has("a"), "filter removes a")
assert_eq(big["b"], 2)

-- signals
let count = signal(0)
assert_eq(count.get(), 0)
count.set(5)
assert_eq(count.get(), 5)
let doubled_sig = derived(fn() { count.get() * 2 })
assert_eq(doubled_sig.get(), 10)
count.set(10)
assert_eq(doubled_sig.get(), 20)

-- @ capture pattern
fn describe(val) {
    return match val {
        n @ 1..=10 => "small: {n}"
        n => "other: {n}"
    }
}
assert_eq(describe(5), "small: 5")
assert_eq(describe(50), "other: 50")

-- or patterns
fn vowel(ch) {
    return match ch {
        "a" | "e" | "i" | "o" | "u" => true
        _ => false
    }
}
assert(vowel("a"), "a is vowel")
assert(!vowel("x"), "x is not vowel")

-- slice patterns
fn head_tail(arr) {
    return match arr {
        [first, ..rest] => "head={first} rest={rest}"
        [] => "empty"
    }
}
assert_eq(head_tail([1, 2, 3]), "head=1 rest=[2, 3]")
assert_eq(head_tail([]), "empty")

-- nested struct destructuring in let
struct Point { x, y }
let Point { x: px, y: py } = Point { x: 100, y: 200 }
assert_eq(px, 100)
assert_eq(py, 200)

-- tuple destructuring
let (a, b, c) = (10, 20, 30)
assert_eq(a, 10)
assert_eq(b, 20)
assert_eq(c, 30)

-- struct spread
let p1 = Point { x: 1, y: 2 }
let p2 = Point { ...p1, y: 99 }
assert_eq(p2.x, 1)
assert_eq(p2.y, 99)

-- labeled break with value
let result = loop {
    break 42
}
assert_eq(result, 42)
