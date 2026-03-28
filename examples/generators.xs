-- generators.xs: lazy sequences with fn* and yield

println("--- Basic Generator ---")

fn* countdown(max) {
    var n = max
    while n > 0 {
        yield n
        n = n - 1
    }
}

for x in countdown(5) {
    print("{x} ")
}
println("")

println("\n--- Fibonacci Generator ---")

fn* fibonacci(limit) {
    var a = 0
    var b = 1
    while a < limit {
        yield a
        let tmp = a + b
        a = b
        b = tmp
    }
}

var fibs = []
for f in fibonacci(100) {
    fibs.push(f)
}
println(fibs)

println("\n--- Infinite-ish Generator ---")

fn* naturals() {
    var n = 1
    while n <= 20 {
        yield n
        n = n + 1
    }
}

-- take first 5 even numbers
var evens = []
for n in naturals() {
    if n % 2 == 0 { evens.push(n) }
    if len(evens) >= 5 { break }
}
println("first 5 evens: {evens}")
assert_eq(evens, [2, 4, 6, 8, 10])

println("\nAll generator demos passed!")
