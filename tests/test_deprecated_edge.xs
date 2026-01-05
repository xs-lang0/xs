@deprecated
fn bare_deprecated() { 1 }

@deprecated()
fn empty_parens() { 2 }

let a = bare_deprecated()
let b = empty_parens()
assert(a == 1, "bare works")
assert(b == 2, "empty parens works")
println("Edge case tests passed!")
