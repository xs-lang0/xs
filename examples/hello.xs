-- hello.xs: quick tour of XS basics
-- variables, functions, control flow, types

println("--- Variables ---")

let name = "XS"              -- immutable
var count = 0                -- mutable
const MAX = 10               -- constant

println("language: {name}")
println("count starts at {count}")

-- basic types
let n = 42
let pi = 3.14
let yes = true
let nothing = null
let nums = [1, 2, 3]
let pair = ("hello", 42)
let config = #{"debug": true, "port": 8080}

println("int: {n}, float: {pi}, bool: {yes}, null: {nothing}")
println("array: {nums}, tuple: {pair}")
println("map: {config}")

-- strings have interpolation baked in
let x = 7
println("{x} squared is {x * x}")

println("\n--- Functions ---")

fn greet(who) {
    println("hey {who}!")
}
greet("world")

-- expression-body shorthand
fn double(x) = x * 2

-- implicit return (last expression)
fn factorial(n) {
    if n <= 1 { return 1 }
    n * factorial(n - 1)
}

println("double(5) = {double(5)}")
println("5! = {factorial(5)}")

-- default params
fn connect(host, port = 8080) {
    return "{host}:{port}"
}
println("default port: {connect("localhost")}")
println("custom port: {connect("localhost", 3000)}")

println("\n--- Control Flow ---")

-- if/elif/else
fn classify(n) {
    if n < 0 { return "negative" }
    elif n == 0 { return "zero" }
    elif n < 10 { return "small" }
    else { return "big" }
}
println("classify(7) = {classify(7)}")
println("classify(-3) = {classify(-3)}")

-- if as expression
let label = if count == 0 { "empty" } else { "has items" }
println("label: {label}")

-- for loops with ranges
var sum = 0
for i in 1..=10 { sum = sum + i }
println("sum of 1..10 = {sum}")

-- match expressions
fn http_status(code) {
    return match code {
        200 => "OK"
        404 => "Not Found"
        500 => "Internal Server Error"
        c if c >= 400 => "Client Error"
        _ => "Unknown"
    }
}
println("200 -> {http_status(200)}")
println("404 -> {http_status(404)}")
println("418 -> {http_status(418)}")

-- quick sanity checks
assert_eq(double(21), 42)
assert_eq(factorial(6), 720)
assert_eq(http_status(200), "OK")
assert(sum == 55, "gauss would be proud")

println("\nAll good!")
