let x: i32 = 42
let s: str = "hello"
let b: bool = true

fn add(a: i32, b: i32) -> i32 {
    a + b
}

let result: i32 = add(1, 2)
assert(result == 3, "type checked add")
println("type checking test passed")
