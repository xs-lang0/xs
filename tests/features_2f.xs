-- reduce
let arr = [1, 2, 3, 4, 5]
let sum = arr.reduce(fn(acc, x) { acc + x }, 0)
println(sum)

let product = arr.reduce(fn(acc, x) { acc * x }, 1)
println(product)

-- Generator
fn* range_gen(start, end) {
    var i = start
    while i < end {
        yield i
        i = i + 1
    }
}

for x in range_gen(0, 5) {
    print_no_nl("{x} ")
}
println("")

-- Generator in expression
let gen_vals = range_gen(1, 4)
println(gen_vals)

-- Fibonacci generator
fn* fib(n) {
    var a = 0
    var b = 1
    var count = 0
    while count < n {
        yield a
        let tmp = a + b
        a = b
        b = tmp
        count = count + 1
    }
}
let fibs = fib(8)
println(fibs)

println("2f features passed")
