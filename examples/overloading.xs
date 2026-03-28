-- function overloading: same name, different arities

fn greet() {
    println("hello, world!")
}

fn greet(name) {
    println("hello, {name}!")
}

fn greet(name, greeting) {
    println("{greeting}, {name}!")
}

greet()
greet("Alice")
greet("Bob", "hey")

-- overloading with default params
fn calc(x) = x * 2
fn calc(x, y) = x + y
fn calc(x, y, z) = x + y + z

println(calc(5))
println(calc(3, 4))
println(calc(1, 2, 3))
