-- Test the ? (try/propagate) operator
--
-- The ? operator:
--   - If the value is Err(...), early-returns it from the current function
--   - If the value is Ok(v), unwraps and yields the inner value v
--   - If the value is neither Ok nor Err, passes it through unchanged

-- --- Basic Ok/Err with match (manual pattern) ---

fn might_fail(x) {
    if x > 0 { Ok(x * 2) } else { Err("negative") }
}

fn process(x) {
    let val = might_fail(x)
    match val {
        Ok(v) => v + 1,
        Err(e) => e
    }
}

assert(process(5) == 11, "ok path")
assert(process(-1) == "negative", "err path")

-- --- The ? operator: auto-propagation ---

fn divide(a, b) {
    if b == 0 { return Err("division by zero") }
    Ok(a / b)
}

fn double_divide(a, b) {
    let v = divide(a, b)?
    Ok(v * 2)
}

-- Ok path: divide(10, 2) => Ok(5), ? unwraps to 5, returns Ok(10)
let ok_res = double_divide(10, 2)
assert(ok_res == Ok(10), "? unwraps Ok: {ok_res}")

-- Err path: divide(10, 0) => Err("division by zero"), ? propagates it
let err_res = double_divide(10, 0)
assert(err_res == Err("division by zero"), "? propagates Err: {err_res}")

-- --- Chained ? operators ---

fn safe_sqrt(x) {
    if x < 0 { return Err("negative sqrt") }
    Ok(x)
}

fn compute(a, b) {
    let d = divide(a, b)?
    let s = safe_sqrt(d)?
    Ok(s + 1)
}

let c1 = compute(10, 2)
assert(c1 == Ok(6), "chained ? ok: {c1}")

let c2 = compute(10, 0)
assert(c2 == Err("division by zero"), "chained ? err from divide: {c2}")

let c3 = compute(-4, 1)
assert(c3 == Err("negative sqrt"), "chained ? err from sqrt: {c3}")

-- --- ? on non-Result values passes through ---

fn identity(x) {
    let v = x?
    v + 1
}

assert(identity(10) == 11, "? on plain value passes through")

println("Try operator test passed!")
