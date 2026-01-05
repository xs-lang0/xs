fn double(x: i32) -> i32 { x * 2 }
fn triple(x: i32) -> i32 { x * 3 }

{- mutual recursion — both defined at top level -}
fn is_even(n: i32) -> bool {
    if n == 0 { return true }
    is_odd(n - 1)
}
fn is_odd(n: i32) -> bool {
    if n == 0 { return false }
    is_even(n - 1)
}

assert(double(5) == 10, "double works")
assert(triple(3) == 9, "triple works")
assert(is_even(4), "is_even 4")
assert(is_odd(3), "is_odd 3")
println("name resolution test passed")
