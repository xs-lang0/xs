@pure fn compute(x: i32) -> i32 {
    println("side effect!")
    x + 1
}
println(compute(3))
