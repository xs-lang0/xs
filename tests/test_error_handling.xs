-- try/catch/finally, throw, defer, assert

-- basic try/catch
var caught = null
try {
    throw "oops"
} catch e {
    caught = e
}
assert_eq(caught, "oops")

-- catch preserves type
var caught_int = null
try {
    throw 42
} catch e {
    caught_int = e
}
assert_eq(caught_int, 42)

-- try/catch/finally
var order = []
try {
    order.push("try")
    throw "err"
} catch e {
    order.push("catch")
} finally {
    order.push("finally")
}
assert_eq(order, ["try", "catch", "finally"])

-- finally runs even without throw
var order2 = []
try {
    order2.push("try")
} catch e {
    order2.push("catch")
} finally {
    order2.push("finally")
}
assert_eq(order2, ["try", "finally"])

-- nested try/catch
var inner_caught = null
var outer_caught = null
try {
    try {
        throw "inner"
    } catch e {
        inner_caught = e
        throw "outer"
    }
} catch e {
    outer_caught = e
}
assert_eq(inner_caught, "inner")
assert_eq(outer_caught, "outer")

-- defer
var defer_order = []
fn test_defer() {
    defer { defer_order.push("deferred") }
    defer_order.push("first")
    defer_order.push("second")
}
test_defer()
assert_eq(defer_order, ["first", "second", "deferred"])

-- multiple defers (LIFO order)
var lifo = []
fn test_multi_defer() {
    defer { lifo.push("a") }
    defer { lifo.push("b") }
    defer { lifo.push("c") }
    lifo.push("body")
}
test_multi_defer()
assert_eq(lifo, ["body", "c", "b", "a"])

-- assert_eq itself
assert_eq(1, 1)
assert_eq("hello", "hello")
assert_eq([1, 2], [1, 2])

-- division by zero gives null, not crash
let d = 10 / 0
assert_eq(d, null)

println("test_error_handling: all passed")
