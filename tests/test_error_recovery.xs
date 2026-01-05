-- ===========================================
-- Test: Error Recovery via try/catch
-- ===========================================
-- Tests that the interpreter properly recovers from errors

var passed = 0
var failed = 0

fn check(cond, label) {
    if cond {
        passed = passed + 1
    } else {
        println("FAIL: " ++ label)
        failed = failed + 1
    }
}

-- === 1. Division by zero returns 0 (not an exception) ===
-- Integer division by zero produces 0 in this interpreter
let div0_result = 1 / 0
check(div0_result == 0, "int div by zero returns 0")

-- Float division by zero produces special values
let fdiv_pos = 1.0 / 0.0
let fdiv_neg = -1.0 / 0.0
let fdiv_nan = 0.0 / 0.0
check(type(fdiv_pos) == "float", "float div by zero is float")
check(type(fdiv_neg) == "float", "neg float div by zero is float")
check(type(fdiv_nan) == "float", "0/0 float is float")
-- NaN != NaN
check(fdiv_nan != fdiv_nan, "NaN != NaN")

-- === 2. Throw string is catchable ===
var caught_str = null
try {
    throw "something went wrong"
} catch e {
    caught_str = e
}
check(caught_str == "something went wrong", "catch thrown string")

-- === 3. Throw integer is catchable ===
var caught_int = null
try {
    throw 404
} catch e {
    caught_int = e
}
check(caught_int == 404, "catch thrown integer")

-- === 4. Throw map is catchable ===
var caught_map = null
try {
    throw {"code": 500, "msg": "internal error"}
} catch e {
    caught_map = e
}
check(type(caught_map) == "map", "catch thrown map")

-- === 5. Throw from function propagates to caller ===
fn failing_fn() {
    throw "function error"
}

var fn_err = null
try {
    failing_fn()
} catch e {
    fn_err = e
}
check(fn_err == "function error", "catch throw from function")

-- === 6. Nested try/catch - inner catch handles inner throw ===
var outer_ok = false
var inner_caught = null
try {
    try {
        throw "inner"
    } catch e {
        inner_caught = e
    }
    outer_ok = true
} catch e {
    outer_ok = false
}
check(inner_caught == "inner", "inner catch gets inner throw")
check(outer_ok == true, "outer try succeeds when inner catches")

-- === 7. Nested try/catch - rethrow propagates to outer ===
var rethrown = null
try {
    try {
        throw "original"
    } catch e {
        throw "rethrown: " ++ str(e)
    }
} catch e {
    rethrown = e
}
check(rethrown == "rethrown: original", "rethrow propagates to outer")

-- === 8. No throw means catch is skipped ===
var catch_ran = false
try {
    let x = 1 + 2
} catch e {
    catch_ran = true
}
check(catch_ran == false, "catch skipped when no throw")

-- === 9. Try with success sets variable in body ===
var try_result = 0
try {
    try_result = 42
} catch e {
    try_result = -1
}
check(try_result == 42, "try success sets variable")

-- === 10. Try with throw sets variable in catch ===
var catch_result = 0
try {
    throw "err"
    catch_result = -1
} catch e {
    catch_result = 99
}
check(catch_result == 99, "catch sets variable on throw")

-- === 11. Code after throw in try is not executed ===
var after_throw = false
try {
    throw "stop"
    after_throw = true
} catch e {
    -- do nothing
}
check(after_throw == false, "code after throw not executed")

-- === 12. Exception value accessible in catch ===
var exc_val = null
try {
    throw [1, 2, 3]
} catch e {
    exc_val = e
}
check(type(exc_val) == "array", "catch array exception")
check(len(exc_val) == 3, "catch array has 3 elements")

-- === 13. Try/catch inside a loop ===
var errors_caught = 0
for i in range(5) {
    try {
        if i % 2 == 0 {
            throw "even error"
        }
    } catch e {
        errors_caught = errors_caught + 1
    }
}
check(errors_caught == 3, "try/catch in loop catches 3 errors")

-- === 14. Try/catch inside a function called in a loop ===
fn may_fail(x) {
    if x < 0 { throw "negative" }
    return x * 2
}

var successes = 0
var failures = 0
let inputs = [1, -1, 2, -2, 3]
for inp in inputs {
    try {
        may_fail(inp)
        successes = successes + 1
    } catch e {
        failures = failures + 1
    }
}
check(successes == 3, "3 successes in loop")
check(failures == 2, "2 failures in loop")

-- === 15. Deeply nested throw propagation ===
fn level3() { throw "deep" }
fn level2() { level3() }
fn level1() { level2() }

var deep_err = null
try {
    level1()
} catch e {
    deep_err = e
}
check(deep_err == "deep", "deeply nested throw caught")

-- === 16. Finally block always runs (no throw) ===
var finally_ran = false
try {
    let x = 42
} catch e {
    -- skip
} finally {
    finally_ran = true
}
check(finally_ran == true, "finally runs without throw")

-- === 17. Finally block always runs (with throw) ===
var finally_ran2 = false
try {
    throw "err"
} catch e {
    -- caught
} finally {
    finally_ran2 = true
}
check(finally_ran2 == true, "finally runs after throw+catch")

-- === 18. Multiple sequential try/catch blocks ===
var first_caught = null
var second_caught = null
try { throw "first" } catch e { first_caught = e }
try { throw "second" } catch e { second_caught = e }
check(first_caught == "first", "first sequential try/catch")
check(second_caught == "second", "second sequential try/catch")

-- === 19. Throw null ===
var null_caught = false
var null_val = "not null"
try {
    throw null
} catch e {
    null_caught = true
    null_val = e
}
check(null_caught == true, "null throw is caught")
check(null_val == null, "caught null value is null")

-- === 20. Throw boolean ===
var bool_caught = null
try {
    throw false
} catch e {
    bool_caught = e
}
check(bool_caught == false, "catch boolean exception")

-- Summary
println("")
println("Error Recovery Test Results:")
println("  Passed: " ++ str(passed))
println("  Failed: " ++ str(failed))
if failed > 0 {
    println("SOME TESTS FAILED!")
} else {
    println("ALL ERROR RECOVERY TESTS PASSED!")
}
