-- Test generator iterator protocol

fn* count(n) {
    var i = 0
    while i < n {
        yield i
        i += 1
    }
}

-- Test next() method
let gen = count(3)
println(gen.next())
println(gen.next())
println(gen.next())
println(gen.next())

-- Test for-in loop over generator
println("--- for-in loop ---")
for x in count(5) {
    println(x)
}

-- Test empty generator
fn* empty() {}
let eg = empty()
println(eg.next())

-- Test generator with values
fn* fibonacci(n) {
    var a = 0
    var b = 1
    var i = 0
    while i < n {
        yield a
        let temp = a + b
        a = b
        b = temp
        i += 1
    }
}

println("--- fibonacci ---")
let fib = fibonacci(7)
var r = fib.next()
while !r.done {
    println(r.value)
    r = fib.next()
}
