-- tests/smoke.xs — exercises core interpreter features
let x = 42
let y = 3.14
let s = "hello"
let arr = [1, 2, 3]
let m = {"a": 1, "b": 2}

fn add(a, b) { a + b }
let result = add(x, 8)
println(result)

-- loops
var sum = 0
for i in range(1, 11) { sum = sum + i }
println(sum)

-- match
let val = match x {
    42 => "found it",
    _  => "nope",
}
println(val)

-- struct
struct Point { x, y }
let p = Point { x: 1, y: 2 }
println(p.x)

-- enum
enum Color { Red, Green, Blue }
let c = Color::Red
println(c)

println("smoke tests passed")
