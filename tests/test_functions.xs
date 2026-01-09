-- functions, closures, lambdas, recursion, variadic, default args

-- basic function
fn add(a, b) { return a + b }
assert_eq(add(3, 4), 7)

-- implicit return (last expression)
fn double(x) { x * 2 }
assert_eq(double(5), 10)

-- recursion
fn fib(n) {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}
assert_eq(fib(10), 55)

-- lambda
let square = fn(x) { x * x }
assert_eq(square(5), 25)

-- arrow lambda
let inc = (x) => x + 1
assert_eq(inc(5), 6)

-- closure
fn make_counter() {
    var n = 0
    return fn() { n = n + 1; return n }
}
let c = make_counter()
assert_eq(c(), 1)
assert_eq(c(), 2)
assert_eq(c(), 3)

-- closure captures
fn make_adder(n) {
    return fn(x) { n + x }
}
let add5 = make_adder(5)
assert_eq(add5(10), 15)
assert_eq(add5(20), 25)

-- nested closures
fn outer() {
    var x = 10
    fn middle() {
        var y = 20
        fn inner() { return x + y }
        return inner()
    }
    return middle()
}
assert_eq(outer(), 30)

-- default parameters
fn greet(name, greeting = "hello") {
    return "{greeting}, {name}"
}
assert_eq(greet("world"), "hello, world")
assert_eq(greet("world", "hi"), "hi, world")

-- variadic
fn sum(...args) {
    var total = 0
    for a in args { total = total + a }
    return total
}
assert_eq(sum(1, 2, 3), 6)
assert_eq(sum(), 0)

-- functions as arguments
fn apply(f, x) { return f(x) }
assert_eq(apply(fn(x) { x * 3 }, 5), 15)

-- returning functions
fn multiplier(n) {
    return fn(x) { x * n }
}
let triple = multiplier(3)
assert_eq(triple(4), 12)

-- mutual recursion
fn is_even(n) {
    if n == 0 { return true }
    return is_odd(n - 1)
}
fn is_odd(n) {
    if n == 0 { return false }
    return is_even(n - 1)
}
assert(is_even(10), "10 is even")
assert(is_odd(7), "7 is odd")

println("test_functions: all passed")
