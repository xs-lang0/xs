// XS Language -- Fibonacci Example
// Demonstrates recursion, iteration, and sequences

// Recursive (simple)
fn fib_rec(n) {
    if n <= 1 { return n }
    return fib_rec(n - 1) + fib_rec(n - 2)
}

// Iterative (efficient)
fn fib_iter(n) {
    if n <= 1 { return n }
    var a = 0
    var b = 1
    var i = 0
    while i < n - 1 {
        let tmp = b
        b = a + b
        a = tmp
        i = i + 1
    }
    return b
}

// Sequence generator -- produces first `count` fibonacci numbers
fn fib_sequence(count) {
    var result = []
    var a = 0
    var b = 1
    var i = 0
    while i < count {
        result.push(a)
        let tmp = b
        b = a + b
        a = tmp
        i = i + 1
    }
    return result
}

fn main() {
    println("=== Fibonacci Sequences ===")
    println("")

    // Recursive
    println("Recursive fib(10) = {fib_rec(10)}")

    // Iterative
    println("Iterative fib(30) = {fib_iter(30)}")

    // First 15 fibonacci numbers
    let seq = fib_sequence(15)
    println("First 15: {seq}")

    // Sum of even fibonacci numbers from the sequence
    var even_sum = 0
    for x in seq {
        if x % 2 == 0 {
            even_sum = even_sum + x
        }
    }
    println("Sum of evens: {even_sum}")

    // Find first fib > 1000
    var a = 0
    var b = 1
    while b <= 1000 {
        let tmp = b
        b = a + b
        a = tmp
    }
    println("First fib > 1000: {b}")
}
