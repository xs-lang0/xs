-- XS Language Tour
-- a quick walkthrough of all the major features

-- variables: let (immutable), var (mutable)
let name = "XS"
var count = 0

-- types: int, float, str, bool, null, char, array, map, tuple, range
let age = 25
let pi = 3.14159
let greeting = "hello {name}"
let active = true
let items = [1, 2, 3, 4, 5]
let config = #{"host": "localhost", "port": 8080}
let point = (10, 20)
let nums = 1..10

-- control flow
if age >= 18 {
    println("adult")
}

-- for loops
var sum = 0
for x in 1..=5 {
    sum = sum + x
}
assert_eq(sum, 15)

-- while loops
var i = 0
while i < 3 { i = i + 1 }

-- loop with break
loop {
    count = count + 1
    if count == 5 { break }
}
assert_eq(count, 5)

-- functions
fn add(a, b) { return a + b }
fn square(x) = x * x

assert_eq(add(3, 4), 7)
assert_eq(square(5), 25)

-- closures
let multiplier = fn(factor) {
    return fn(x) { x * factor }
}
let double = multiplier(2)
assert_eq(double(21), 42)

-- default params and named args
fn connect(host, port) {
    return "{host}:{port}"
}
assert_eq(connect(host: "localhost", port: 3000), "localhost:3000")

-- pattern matching
fn describe(val) {
    return match val {
        0 => "zero"
        1..=9 => "small"
        n if n > 100 => "big"
        _ => "medium"
    }
}
assert_eq(describe(0), "zero")
assert_eq(describe(5), "small")
assert_eq(describe(200), "big")
assert_eq(describe(50), "medium")

-- structs and methods
struct Vec2 { x, y }
impl Vec2 {
    fn magnitude(self) {
        return (self.x ** 2 + self.y ** 2) ** 0.5
    }
    fn +(self, other) {
        return Vec2 { x: self.x + other.x, y: self.y + other.y }
    }
}
let v = Vec2 { x: 3, y: 4 }
assert_eq(v.magnitude(), 5.0)

-- traits
trait Printable {
    fn display(self) -> str
}
struct Color { r, g, b }
impl Printable for Color {
    fn display(self) -> str {
        return "rgb({self.r}, {self.g}, {self.b})"
    }
}
let red = Color { r: 255, g: 0, b: 0 }
assert_eq(red.display(), "rgb(255, 0, 0)")

-- enums
enum Direction { North, South, East, West }
fn is_vertical(d) {
    return match d {
        Direction.North => true
        Direction.South => true
        _ => false
    }
}
assert(is_vertical(Direction.North), "north is vertical")

-- classes and inheritance
class Animal {
    var name
    fn speak(self) { return "{self.name} says ..." }
}
class Dog : Animal {
    fn speak(self) { return "{self.name} says woof!" }
}
let dog = Dog { name: "Rex" }
assert_eq(dog.speak(), "Rex says woof!")

-- generators
fn* fibonacci(n) {
    var a = 0
    var b = 1
    var k = 0
    while k < n {
        yield a
        let tmp = a + b
        a = b
        b = tmp
        k = k + 1
    }
}
var fibs = []
for f in fibonacci(6) { fibs.push(f) }
assert_eq(fibs, [0, 1, 1, 2, 3, 5])

-- error handling
fn safe_divide(a, b) {
    if b == 0 { throw "division by zero" }
    return a / b
}
let result = try {
    safe_divide(10, 0)
} catch e {
    -1
}
assert_eq(result, -1)

-- do expressions
let classification = do {
    let score = 85
    if score >= 90 { "A" }
    elif score >= 80 { "B" }
    else { "C" }
}
assert_eq(classification, "B")

-- concurrency
var tasks = []
nursery {
    spawn { tasks.push("a") }
    spawn { tasks.push("b") }
}
assert_eq(tasks.sort(), ["a", "b"])

-- channels
let ch = channel()
ch.send("hello")
assert_eq(ch.recv(), "hello")

-- pipe operator
let result2 = [1, 2, 3, 4, 5]
    |> fn(a) { a.filter(fn(x) { x > 2 }) }
    |> fn(a) { a.map(fn(x) { x * 10 }) }
assert_eq(result2, [30, 40, 50])

-- comprehensions
let squares = [x * x for x in 1..=5]
assert_eq(squares, [1, 4, 9, 16, 25])

-- regex
let email = /[a-zA-Z0-9.]+@[a-zA-Z0-9.]+/
assert(email.test("user@example.com"), "email match")

-- string methods
assert_eq("hello world".upper(), "HELLO WORLD")
assert_eq("  trim me  ".trim(), "trim me")
assert_eq("hello".chars(), ["h", "e", "l", "l", "o"])

-- map methods
let m = #{"a": 1, "b": 2, "c": 3}
assert_eq(m.keys().sort(), ["a", "b", "c"])
assert_eq(m.values().sort(), [1, 2, 3])

-- type system (optional)
fn typed_add(a: int, b: int) -> int {
    return a + b
}
assert_eq(typed_add(1, 2), 3)

println("tour complete - all assertions passed!")
