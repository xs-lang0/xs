trait Greetable { fn greet(self) }
struct Person { name }
impl Greetable for Person {
    fn greet(self) { "Hi, {self.name}" }
}
let p = Person { name: "Alice" }
println(p.greet())

var found_at = -1
outer: for i in range(0, 5) {
    for j in range(0, 5) {
        if i * 5 + j == 13 {
            found_at = i * 5 + j
            break outer
        }
    }
}
println("found: {found_at}")

fn test_defer() {
    defer { println("cleanup") }
    println("work")
}
test_defer()

try {
    throw "oops"
} catch e {
    println("caught: {e}")
}

assert(1 + 1 == 2, "math works")
println("assert ok")

let arr = [1, 2, 3, 4, 5]
let doubled = arr.map(fn(x) { x * 2 })
println(doubled)
let evens = arr.filter(fn(x) { x % 2 == 0 })
println(evens)

println("2cd features passed")
