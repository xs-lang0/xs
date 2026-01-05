-- Struct update syntax
struct Point { x: int, y: int }
let p1 = Point { x: 1, y: 2 }
let p2 = Point { ...p1, y: 10 }
assert(p2.x == 1, "spread keeps x")
assert(p2.y == 10, "spread overrides y")

-- Break with value from loop
let result = loop {
    break 42
}
assert(result == 42, "break with value")

-- Break with value from while
var i = 0
let found = loop {
    if i == 5 { break i * 10 }
    i = i + 1
}
assert(found == 50, "break with computed value")

println("Spread and break-value tests passed!")
