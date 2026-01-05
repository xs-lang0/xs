-- Call function before its definition
let result = add(3, 4)
assert(result == 7, "function hoisting works")

fn add(a, b) { a + b }

-- Mutual recursion
assert(is_even(4) == true, "mutual recursion via hoisting")
assert(is_odd(3) == true, "mutual recursion via hoisting 2")

fn is_even(n) {
    if n == 0 { true } else { is_odd(n - 1) }
}

fn is_odd(n) {
    if n == 0 { false } else { is_even(n - 1) }
}

println("All hoisting tests passed!")
