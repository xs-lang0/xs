-- VM backend tests (run with: xs --vm tests/test_vm.xs)

-- arithmetic
assert(6 * 7 == 42, "mul")
assert(10 + 20 == 30, "add")
assert(100 - 37 == 63, "sub")
assert(10 / 2 == 5, "div")
assert(10 % 3 == 1, "mod")

-- strings
assert("hello" ++ " " ++ "world" == "hello world", "concat")
let name = "vm"
assert("hi {name}" == "hi vm", "interp")

-- variables
let x = 42
assert(x == 42, "let")
var y = 0
y = y + 1
assert(y == 1, "var mut")

-- control flow
var sum = 0
for i in 0..10 { sum = sum + i }
assert(sum == 45, "for loop")

var w = 0
while w < 5 { w = w + 1 }
assert(w == 5, "while")

-- functions and closures
fn fib(n) {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}
assert(fib(15) == 610, "fib")

fn make_adder(n) {
    return fn(x) { return n + x }
}
let add10 = make_adder(10)
assert(add10(5) == 15, "closure")

-- arrays
let arr = [1, 2, 3, 4, 5]
assert(len(arr) == 5, "array len")
assert(arr[0] == 1, "array index")

-- match
let r = match 3 {
    1 => "one"
    2 => "two"
    3 => "three"
    _ => "other"
}
assert(r == "three", "match")

-- structs
struct Vec2 { x, y }
let v = Vec2 { x: 1, y: 2 }
assert(v.x == 1, "struct field")

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
assert(caught == "vm error", "try/catch")

-- compound assignment
var n = 10
n += 5
assert(n == 15, "+=")
n -= 3
assert(n == 12, "-=")
n *= 2
assert(n == 24, "*=")

-- destructuring
let (a, b) = (10, 20)
assert(a == 10, "destr a")
assert(b == 20, "destr b")

println("test_vm: all passed")
