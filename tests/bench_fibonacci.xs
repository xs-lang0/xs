-- Fibonacci benchmark: compare recursive vs iterative

fn fib_recursive(n) {
    if n <= 1 { return n }
    fib_recursive(n - 1) + fib_recursive(n - 2)
}

fn fib_iterative(n) {
    var a = 0
    var b = 1
    for i in range(n) {
        let temp = b
        b = a + b
        a = temp
    }
    a
}

fn fib_tail(n, a, b) {
    if n == 0 { return a }
    if n == 1 { return b }
    return fib_tail(n - 1, b, a + b)
}

-- Warmup and run
let n = 25
let r1 = fib_recursive(n)
let r2 = fib_iterative(n)
let r3 = fib_tail(n, 0, 1)

assert r1 == 75025
assert r2 == 75025
assert r3 == 75025
