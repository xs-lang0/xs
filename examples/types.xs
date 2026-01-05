// XS Language -- Types Example
// Demonstrates type annotations and the type() builtin

fn main() {
    // Type annotations (parsed but not enforced at runtime)
    let x: i32 = 42
    var name: str = "hello"
    let pi: f64 = 3.14159
    let flag: bool = true
    let items: array = [1, 2, 3]

    println("=== Type Inspection ===")
    println("")

    println("x = {x}, type = {type(x)}")
    println("name = {name}, type = {type(name)}")
    println("pi = {pi}, type = {type(pi)}")
    println("flag = {flag}, type = {type(flag)}")
    println("items = {items}, type = {type(items)}")
    println("null type = {type(null)}")
    println("")

    // Functions have a type too
    fn add(a, b) { return a + b }
    println("add type = {type(add)}")
    println("")

    // Dynamic typing: same variable, different types
    var dynamic = 42
    println("dynamic = {dynamic}, type = {type(dynamic)}")
    dynamic = "now a string"
    println("dynamic = {dynamic}, type = {type(dynamic)}")
    dynamic = [1, 2, 3]
    println("dynamic = {dynamic}, type = {type(dynamic)}")
    dynamic = true
    println("dynamic = {dynamic}, type = {type(dynamic)}")
    println("")

    // Type checking in conditionals
    let val = 42
    if type(val) == "int" {
        println("{val} is an integer")
    }

    let s = "hello"
    if type(s) == "str" {
        println("{s} is a string")
    }

    // Conversions
    println("")
    println("=== Conversions ===")
    println("str(42) = {str(42)}")
    println("str(3.14) = {str(3.14)}")
    println("str(true) = {str(true)}")
}
