// XS Language -- Hello World Example
// Demonstrates basic syntax and output

fn main() {
    println("Hello, World!")
    println("Welcome to the XS Language!")

    // Variables
    let name = "XS"
    let version = 1
    let stable = false

    println("Language: {name}")
    println("Version: {version}")
    println("Stable: {stable}")

    // String concatenation
    let greeting = "Hello from " ++ name ++ " v" ++ str(version) ++ "!"
    println(greeting)

    // Math
    let pi = 3.14159
    let radius = 5.0
    let area = pi * radius * radius
    println("Circle area: {area}")

    // Conditional
    if area > 50.0 {
        println("That's a big circle!")
    } else {
        println("Small circle.")
    }
}
