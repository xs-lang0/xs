let x: i32 = 42
let name: str = "hello"

fn add(a: i32, b: i32) -> i32 {
    a + b
}
fn greet(s: str) -> str {
    "hello " ++ s
}

let result: i32 = add(1, 2)
assert(result == 3, "type annotated add works")
assert(greet("world") == "hello world", "type annotated greet works")
println("type annotation parse test passed")
