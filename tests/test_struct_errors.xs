struct Point { x: int, y: int }
let p = Point { x: 1, y: 2 }
assert(p.x == 1, "struct works")
assert(p.y == 2, "struct field y works")
println("Struct error message test passed!")
