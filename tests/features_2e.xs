-- Modules
module math {
    fn add(a, b) { a + b }
    fn mul(a, b) { a * b }
}
println(math.add(3, 4))
println(math.mul(3, 4))

-- Ok/Err Result type
fn divide(a, b) {
    if b == 0 { return Err("division by zero") }
    Ok(a / b)
}
let r = divide(10, 2)
println(r)
let e = divide(10, 0)
println(e)

-- Match on Result
let v = match r {
    Ok(n) => "success: {n}",
    Err(msg) => "error: {msg}",
}
println(v)

-- ? propagation
fn safe_double(a, b) {
    let v = divide(a, b)?
    Ok(v * 2)
}
let sd = safe_double(10, 2)
println(sd)
let se = safe_double(10, 0)
println(se)

println("2e features passed")
