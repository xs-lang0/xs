-- Test try/catch/throw in VM mode

-- Basic try/catch
fn test_basic_catch() {
    var result = "none"
    try {
        throw "error!"
        result = "unreachable"
    } catch e {
        result = e
    }
    assert(result == "error!", "basic catch should capture thrown value")
}

-- Try block with no throw completes normally
fn test_no_throw() {
    var result = 0
    try {
        result = 42
    } catch e {
        result = 0
    }
    assert(result == 42, "try without throw should keep body value")
}

-- Throw with integer value
fn test_throw_int() {
    var result = 0
    try {
        throw 404
        result = 0
    } catch e {
        result = e
    }
    assert(result == 404, "should catch integer exception")
}

-- Nested try/catch
fn test_nested_try() {
    var inner_result = "none"
    var outer_result = "none"
    try {
        try {
            throw "inner_err"
        } catch e {
            inner_result = e
        }
        assert(inner_result == "inner_err", "inner catch works")
        outer_result = "outer_ok"
    } catch e {
        outer_result = "outer_err"
    }
    assert(outer_result == "outer_ok", "outer try should succeed")
}

-- Throw from called function
fn thrower() {
    throw "from_fn"
}

fn test_throw_from_fn() {
    var result = "none"
    try {
        thrower()
        result = "unreachable"
    } catch e {
        result = e
    }
    assert(result == "from_fn", "should catch throw from called function")
}

-- Finally block
fn test_finally() {
    var flag = false
    var result = 0
    try {
        result = 42
    } catch e {
        result = 0
    } finally {
        flag = true
    }
    assert(result == 42, "try result should be 42")
    assert(flag == true, "finally should have run")
}

-- Finally with throw
fn test_finally_with_throw() {
    var flag = false
    var result = "none"
    try {
        throw "err"
    } catch e {
        result = e
    } finally {
        flag = true
    }
    assert(result == "err", "catch result")
    assert(flag == true, "finally should run after catch")
}

fn main() {
    test_basic_catch()
    test_no_throw()
    test_throw_int()
    test_nested_try()
    test_throw_from_fn()
    test_finally()
    test_finally_with_throw()
    println("vm_try_catch: all tests passed")
}
