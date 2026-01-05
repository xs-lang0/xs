fn count_down(n) {
    if n == 0 { return "done" }
    return count_down(n - 1)
}
println(count_down(100000))
