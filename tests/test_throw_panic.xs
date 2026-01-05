-- Test: throw vs panic semantics
-- throw  -> sets CF_THROW, catchable by try/catch
-- panic  -> calls exit(1) immediately, NOT catchable

-- ── Test 1: throw is catchable with try/catch ──
var caught1 = false
try {
    throw "something went wrong"
} catch e {
    println("caught: " ++ str(e))
    caught1 = true
}
assert(caught1, "throw should be caught by try/catch")

-- ── Test 2: throw can carry any value (string, int, map) ──
var caught_val = null
try {
    throw 42
} catch e {
    caught_val = e
}
assert(caught_val == 42, "throw can carry an integer")

var caught_map = null
try {
    throw {"kind": "NotFound", "message": "item missing"}
} catch e {
    caught_map = e
    println("caught map: " ++ str(e))
}
assert(type(caught_map) == "map", "throw can carry a map")

-- ── Test 3: nested try/catch with re-throw ──
var outer_msg = null
try {
    try {
        throw "inner error"
    } catch e {
        println("inner catch: " ++ str(e))
        throw "rethrown: " ++ str(e)
    }
} catch e {
    outer_msg = e
    println("outer catch: " ++ str(e))
}
assert(type(outer_msg) == "str", "re-thrown error caught by outer handler")

-- ── Test 4: throw from a function propagates ──
fn divide(a, b) {
    if b == 0 { throw "division by zero" }
    return a / b
}

var div_err = null
try {
    divide(10, 0)
} catch e {
    div_err = e
    println("divide error: " ++ str(e))
}
assert(div_err == "division by zero", "throw from function is catchable")

-- ── Test 5: no throw means catch is skipped ──
var catch_ran = false
try {
    let x = 1 + 2
} catch e {
    catch_ran = true
}
assert(catch_ran == false, "catch should not run when no throw occurs")

-- ── Test 6: panic is NOT catchable (it calls exit(1)) ──
-- We cannot test panic inline because it terminates the process.
-- The builtin panic(msg) prints to stderr and calls exit(1).
-- It sets CF_PANIC but then immediately exits, so try/catch never
-- gets a chance to intercept it.
--
-- To verify: run `xs -c 'panic("boom")'` in a shell and observe
-- exit code 1 and stderr output "xs: panic: boom".

println("throw/panic tests passed!")
