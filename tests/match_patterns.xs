-- Range patterns
let age = 25
let v1 = match age {
    0..18 => "minor",
    18..65 => "adult",
    _ => "senior",
}
println(v1)

-- Inclusive range
let x = 10
let v2 = match x {
    1..=10 => "in range",
    _ => "out",
}
println(v2)

-- Capture with @
let n = 42
let v3 = match n {
    x @ 40..50 => "captured {x}",
    _ => "out of range",
}
println(v3)

-- Type checking (via guard)
let val = 42
let v4 = match val {
    n if typeof(n) == "int" => "is int",
    _ => "other",
}
println(v4)

-- Combined: everything together
let score = 85
let grade = match score {
    s @ 90..=100 => "A (score={s})",
    s @ 80..90 => "B (score={s})",
    s @ 70..80 => "C (score={s})",
    _ => "F",
}
println(grade)

println("match pattern tests passed")
