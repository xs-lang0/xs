-- VM backend tests (run with: xs --vm tests/test_vm.xs)

-- arithmetic
assert_eq(6 * 7, 42)
assert_eq(10 + 20, 30)
assert_eq(100 - 37, 63)
assert_eq(10 / 2, 5)
assert_eq(10 % 3, 1)
assert_eq(2 ** 10, 1024)

-- strings
assert_eq("hello" ++ " " ++ "world", "hello world")
let name = "vm"
assert_eq("hi {name}", "hi vm")

-- variables
let x = 42
assert_eq(x, 42)
var y = 0
y = y + 1
assert_eq(y, 1)

-- control flow
var sum = 0
for i in 0..10 { sum = sum + i }
assert_eq(sum, 45)

var w = 0
while w < 5 { w = w + 1 }
assert_eq(w, 5)

-- functions and closures
fn fib(n) {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}
assert_eq(fib(15), 610)

fn make_adder(n) {
    return fn(x) { return n + x }
}
let add10 = make_adder(10)
assert_eq(add10(5), 15)

-- arrays
let arr = [1, 2, 3, 4, 5]
assert_eq(len(arr), 5)
assert_eq(arr[0], 1)

-- match
let r = match 3 {
    1 => "one"
    2 => "two"
    3 => "three"
    _ => "other"
}
assert_eq(r, "three")

-- structs
struct Vec2 { x, y }
let v = Vec2 { x: 1, y: 2 }
assert_eq(v.x, 1)

-- enums
enum Dir { North, South, East, West }
let d = Dir::North
assert(str(d).contains("North"), "enum")

-- try/catch
var caught = null
try {
    throw "vm error"
} catch e {
    caught = e
}
assert_eq(caught, "vm error")

-- compound assignment
var n = 10
n += 5
assert_eq(n, 15)
n -= 3
assert_eq(n, 12)
n *= 2
assert_eq(n, 24)

-- destructuring
let (a, b) = (10, 20)
assert_eq(a, 10)
assert_eq(b, 20)

println("test_vm: all passed")
