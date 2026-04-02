-- fibonacci benchmark

fn fib(n) {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}

let result = fib(30)
assert_eq(result, 832040)
println("fib(30) = {result}")
