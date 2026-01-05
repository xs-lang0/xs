// XS Language -- Error Handling Example
// Demonstrates: try/catch/finally, throw, struct errors

// Basic throw and catch
try {
    throw "something went wrong"
} catch e {
    println("Caught string: {e}")
}

println("")

// Catch with finally
fn risky(x) {
    if x < 0 {
        throw "negative value"
    }
    return x * 2
}

try {
    let result = risky(-1)
    println("result: {result}")
} catch e {
    println("Error: {e}")
} finally {
    println("cleanup done")
}

println("")

// Throw and catch struct errors
struct AppError { code, msg }

fn parse_int(s) {
    if s == "" {
        throw AppError { code: 400, msg: "empty input" }
    }
    return 42
}

try {
    let n = parse_int("")
    println("parsed: {n}")
} catch e {
    println("Error {e.code}: {e.msg}")
}

println("")

// Nested try/catch
try {
    try {
        throw "inner error"
    } catch e {
        println("Inner caught: {e}")
        throw "re-thrown"
    }
} catch e {
    println("Outer caught: {e}")
}

println("")

// Closures with mutable state
fn make_counter(start) {
    var count = start
    return fn() {
        count = count + 1
        return count
    }
}

let counter = make_counter(0)
println(counter())
println(counter())
println(counter())

println("")

// Two independent counters
let c1 = make_counter(10)
let c2 = make_counter(100)
println(c1())
println(c2())
println(c1())
println(c2())
