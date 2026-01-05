-- Type annotations are enforced at runtime
let x: int = 42
assert(x == 42, "int annotation")

let s: str = "hello"
assert(s == "hello", "str annotation")

let b: bool = true
assert(b == true, "bool annotation")

let f: float = 3.14
assert(f > 3.0, "float annotation")

let arr: array = [1, 2, 3]
assert(len(arr) == 3, "array annotation")

let m: map = {"a": 1}
assert(m["a"] == 1, "map annotation")

-- 'any' type allows anything
var anything: any = 42
anything = "now a string"
assert(anything == "now a string", "any type")

-- Function with typed parameters and return type
fn add(a: int, b: int) -> int {
    a + b
}
assert(add(1, 2) == 3, "typed function")

-- Function with string return type
fn greet(name: str) -> str {
    "Hello, " + name
}
assert(greet("world") == "Hello, world", "str return type")

-- const with type annotation
const PI: float = 3.14159
assert(PI > 3.0, "const float annotation")

println("Type enforcement tests passed!")
