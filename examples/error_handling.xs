-- XS error handling and recovery demo
-- run this file to see how XS handles errors gracefully

-- 1. try/catch basics
try {
    throw "something broke"
} catch e {
    println("caught: {e}")
}

-- 2. typed errors
try {
    throw #{ kind: "NotFound", message: "file not found", path: "/tmp/nope" }
} catch e {
    println("error: {e["message"]} at {e["path"]}")
}

-- 3. nested try/catch
try {
    try {
        throw "inner"
    } catch e {
        println("inner caught: {e}")
        throw "rethrown"
    }
} catch e {
    println("outer caught: {e}")
}

-- 4. finally always runs
var cleanup_ran = false
try {
    println("doing work...")
    throw "oops"
} catch e {
    println("handling error")
} finally {
    cleanup_ran = true
    println("cleanup ran: {cleanup_ran}")
}

-- 5. defer for cleanup (runs when function exits)
fn read_config() {
    defer { println("  (closed config file)") }
    println("  reading config...")
    return #{ debug: true, port: 8080 }
}
let config = read_config()
println("config: {config}")

-- 6. multiple defers run in reverse order
fn setup() {
    defer { println("  3. release lock") }
    defer { println("  2. close connection") }
    defer { println("  1. free memory") }
    println("  doing setup...")
}
println("setup order:")
setup()

-- 7. division by zero is a runtime error, not a crash
let bad = 10 / 0
println("div by zero gives: {bad}")

-- 8. type annotations catch mismatches at runtime
fn typed_add(a: int, b: int) -> int {
    return a + b
}
println("typed_add(3, 4) = {typed_add(3, 4)}")

-- to see static type checking, run:
--   xs --check examples/error_handling.xs
-- to see strict mode (requires all types), run:
--   xs --strict examples/error_handling.xs

println("\nall done!")
