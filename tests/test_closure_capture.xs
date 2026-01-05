-- Test 1: Counter closure (capture by reference)
fn make_counter() {
    var count = 0
    |delta| {
        count = count + delta
        count
    }
}
let counter = make_counter()
assert(counter(1) == 1, "counter 1")
assert(counter(1) == 2, "counter 2")
assert(counter(5) == 7, "counter 7")

-- Test 2: Mutation visibility
var x = 10
let get_x = || x
let set_x = |v| { x = v }
assert(get_x() == 10, "initial x")
set_x(20)
assert(get_x() == 20, "mutated x visible in closure")
assert(x == 20, "mutated x visible outside")

-- Test 3: let vs var in closure
let immutable = 42
let get_imm = || immutable
assert(get_imm() == 42, "let captured")

-- Test 4: Function parameter capture
fn make_adder(n) {
    |x| x + n
}
let add5 = make_adder(5)
assert(add5(10) == 15, "param captured")

println("Closure capture test passed!")
