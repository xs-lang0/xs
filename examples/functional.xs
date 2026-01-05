// XS Language -- Functional Programming Example
// Demonstrates: higher-order functions, closures, currying, composition

// --- Pure Functions ---
fn double(x) { return x * 2 }
fn square(x) { return x * x }
fn add(a, b) { return a + b }
fn is_even(x) { return x % 2 == 0 }

// --- Helper: fold (since .fold() isn't available as method) ---
fn fold(arr, init, f) {
    var acc = init
    for x in arr {
        acc = f(acc, x)
    }
    return acc
}

// --- Function Composition ---
fn compose(f, g) {
    return fn(x) { return f(g(x)) }
}

// --- Currying ---
fn curry_add(a) {
    return fn(b) { return a + b }
}

fn curry_mul(a) {
    return fn(b) { return a * b }
}

// --- Take while / Drop while ---
fn take_while(arr, pred) {
    var result = []
    for x in arr {
        if pred(x) {
            result.push(x)
        } else {
            break
        }
    }
    return result
}

fn drop_while(arr, pred) {
    var found = false
    var result = []
    for x in arr {
        if !found {
            if pred(x) {
                continue
            }
            found = true
        }
        result.push(x)
    }
    return result
}

// --- Zip with ---
fn zip_with(f, a, b) {
    var result = []
    var n = len(a)
    if len(b) < n { n = len(b) }
    var i = 0
    while i < n {
        result.push(f(a[i], b[i]))
        i = i + 1
    }
    return result
}

// --- Scan left (running accumulation) ---
fn scan_left(arr, init, f) {
    var acc = init
    var result = [init]
    for x in arr {
        acc = f(acc, x)
        result.push(acc)
    }
    return result
}

// --- Sieve of Eratosthenes ---
fn sieve(limit) {
    // Build list 2..limit
    var nums = []
    var i = 2
    while i <= limit {
        nums.push(i)
        i = i + 1
    }

    var primes = []
    while len(nums) > 0 {
        let p = nums[0]
        primes.push(p)
        // Filter out multiples of p
        var remaining = []
        for n in nums {
            if n % p != 0 {
                remaining.push(n)
            }
        }
        nums = remaining
    }
    return primes
}

fn main() {
    println("=== XS Functional Programming Demo ===")
    println("")

    // Basic higher-order functions
    let nums = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
    println("Numbers: {nums}")
    println("Doubled: {nums.map(double)}")
    println("Squared: {nums.map(square)}")
    println("Evens: {nums.filter(is_even)}")
    println("Sum: {fold(nums, 0, add)}")
    println("")

    // Function composition
    let double_then_square = compose(square, double)
    println("double_then_square(3) = {double_then_square(3)}")
    let square_then_double = compose(double, square)
    println("square_then_double(3) = {square_then_double(3)}")
    println("")

    // Currying
    let add5 = curry_add(5)
    let triple = curry_mul(3)
    println("add5(10) = {add5(10)}")
    println("triple(7) = {triple(7)}")
    println("map add5: {nums.map(add5)}")
    println("")

    // Pipeline using variables
    let step1 = nums.filter(is_even)
    let step2 = step1.map(square)
    let step3 = fold(step2, 0, add)
    println("Pipeline (even -> square -> sum): {step3}")
    println("")

    // Scan (running sum)
    let running_sum = scan_left(nums, 0, add)
    println("Running sum: {running_sum}")
    println("")

    // Zip with
    let a = [1, 2, 3, 4, 5]
    let b = [10, 20, 30, 40, 50]
    let zipped = zip_with(add, a, b)
    println("zip_with(add, [1..5], [10..50]): {zipped}")
    println("")

    // Primes via sieve
    let primes = sieve(50)
    println("Primes up to 50: {primes}")
    println("Count: {len(primes)}")
    println("Sum: {fold(primes, 0, add)}")
}
