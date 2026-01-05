-- Test default parameters
fn greet(name, greeting = "Hello") {
    "{greeting}, {name}!"
}
assert(greet("Alice") == "Hello, Alice!", "default param")
assert(greet("Bob", "Hi") == "Hi, Bob!", "override default")

-- Test variadic functions
fn sum(...args) {
    args.reduce(|acc, x| acc + x, 0)
}
assert(sum(1, 2, 3) == 6, "variadic sum")
assert(sum() == 0, "variadic empty")

-- Decorators: only @pure and @deprecated are supported (built-in attributes),
-- not user-defined decorator functions. Test @pure attribute.
@pure
fn add(a, b) { a + b }
assert(add(2, 3) == 5, "@pure decorated fn")

println("Function features test passed!")
