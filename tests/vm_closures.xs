fn make_adder(x) {
    fn add(y) { return x + y }
    return add
}
let add5 = make_adder(5)
println(add5(3))
println(add5(10))
