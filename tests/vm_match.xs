fn classify(n) {
    match n {
        0 => "zero",
        1 => "one",
        _ => "other"
    }
}
println(classify(0))
println(classify(1))
println(classify(42))
