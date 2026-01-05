-- Default args
fn greet(name, greeting = "Hello") {
    println("{greeting}, {name}!")
}
greet("Alice")
greet("Bob", "Hi")

-- Enum constructors
enum Option { Some(val), None }
let x = Option::Some(42)
println(x)
let v = match x {
    Option::Some(n) => "got {n}",
    Option::None => "none",
}
println(v)

-- Variadic
fn sum(...args) {
    var s = 0
    for a in args { s = s + a }
    s
}
let total = sum(1, 2, 3, 4, 5)
println(total)

-- Comprehension
let squares = [x * x for x in range(1, 6)]
println(squares)

-- Map literal with #{}
let m = #{"a": 1, "b": 2}
println(m)

println("2c features passed")
