@deprecated("use new_fn instead")
fn old_fn() { 42 }

let x = old_fn()
assert(x == 42, "deprecated function still works")

println("Deprecated test passed!")
