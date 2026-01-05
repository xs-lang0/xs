// XS Language -- Chaining Example
// Demonstrates function chaining and method calls

fn add(a, b) { return a + b }
fn sub(a, b) { return a - b }
fn mult(a, b) { return a * b }
fn div(a, b) { return a / b }

fn main() {
    let x = 10
    let y = 5

    // Chaining operations using intermediate variables
    let step1 = add(x, y)      // 15
    let step2 = sub(step1, 3)   // 12
    let step3 = mult(step2, 2)  // 24
    let result = div(step3, 4)  // 6
    println("Result of chained operations: {result}")

    // Method chaining on arrays
    let nums = [5, 3, 8, 1, 9, 2, 7, 4, 6]
    println("Original: {nums}")

    // Chain map and filter
    fn double(x) { return x * 2 }
    fn is_big(x) { return x > 10 }
    let big_doubled = nums.map(double).filter(is_big)
    println("Doubled then filtered (> 10): {big_doubled}")

    // String method chaining
    let greeting = "  Hello, World!  "
    println("Original: '{greeting}'")
    println("Length: {len(greeting)}")

    // Build a string by chaining concatenations
    let msg = "XS" ++ " " ++ "is" ++ " " ++ "fun!"
    println(msg)
}
