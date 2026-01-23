-- gradual_typing.xs: showing how code works without types,
-- then adding types for safety

println("--- No Types (Just Works) ---")

-- you can write XS like a dynamic language, no types needed
fn add(a, b) { return a + b }
fn greet(name) { return "hello, {name}" }

println(add(3, 4))
println(greet("world"))
assert_eq(add(3, 4), 7)

-- everything is inferred, nothing is annotated
let items = [1, 2, 3, 4, 5]
let total = items.reduce(fn(acc, x) { acc + x }, 0)
println("total: {total}")

-- type() tells you what you've got at runtime
println("type of 42: {type(42)}")
println("type of 3.14: {type(3.14)}")
println("type of 'hi': {type("hi")}")
println("type of [1,2]: {type([1,2])}")
println("type of true: {type(true)}")
println("type of null: {type(null)}")

println("\n--- Adding Types Gradually ---")

-- now let's annotate the same functions
-- the compiler checks these and catches mismatches
fn typed_add(a: i32, b: i32) -> i32 {
    return a + b
}

fn typed_greet(name: str) -> str {
    return "hello, {name}"
}

println(typed_add(3, 4))
println(typed_greet("world"))
assert_eq(typed_add(10, 20), 30)

-- annotate variables when you want to be explicit
let count: i32 = 42
let name: str = "XS"
let ratio: f64 = 3.14
let active: bool = true

println("count={count} name={name} ratio={ratio} active={active}")

println("\n--- Type Checks with 'is' ---")

-- 'is' lets you check types at runtime
fn describe(val) {
    if val is int { return "integer: {val}" }
    elif val is str { return "string: {val}" }
    elif val is bool { return "boolean: {val}" }
    elif val is array { return "array with {len(val)} items" }
    elif val is null { return "null" }
    else { return "something else" }
}

println(describe(42))
println(describe("hello"))
println(describe(true))
println(describe([1, 2, 3]))
println(describe(null))

assert_eq(describe(42), "integer: 42")
assert_eq(describe("hello"), "string: hello")

println("\n--- Type Casting with 'as' ---")

-- 'as' converts between types
let n = 42
let f = n as float
let s = n as str
println("{n} as float -> {f} (type: {type(f)})")
println("{n} as str -> {s} (type: {type(s)})")

let pi_str = "3.14"
let pi_val = pi_str.parse_float()
println("parsed '{pi_str}' -> {pi_val} (type: {type(pi_val)})")

-- int() truncates toward zero (not rounding)
println("int(3.9) = {int(3.9)}")
println("int(-3.9) = {int(-3.9)}")
assert_eq(int(3.9), 3)
assert_eq(int(-3.9), -3)

println("\n--- Structs: Typed vs Untyped ---")

-- untyped struct: works like a plain record
struct Point { x, y }

let p1 = Point { x: 10, y: 20 }
println("untyped point: ({p1.x}, {p1.y})")

-- typed struct: documents intent, compiler can check
struct TypedPoint {
    x: f64,
    y: f64
}

let p2 = TypedPoint { x: 1.5, y: 2.5 }
println("typed point: ({p2.x}, {p2.y})")

-- both work the same at runtime, but typed versions
-- catch mistakes earlier (like passing a string for x)

println("\n--- Typed Function Signatures ---")

-- types serve as documentation AND safety net
fn distance(p1: TypedPoint, p2: TypedPoint) -> f64 {
    let dx = p2.x - p1.x
    let dy = p2.y - p1.y
    return sqrt(dx * dx + dy * dy)
}

let a = TypedPoint { x: 0.0, y: 0.0 }
let b = TypedPoint { x: 3.0, y: 4.0 }
println("distance: {distance(a, b)}")
assert_eq(distance(a, b), 5.0)

-- the idea: start untyped, add types where it matters
-- prototyping -> untyped
-- library API -> typed
-- hot path -> typed
-- tests -> who cares

println("\n--- Null Safety with ?? ---")

-- null coalesce is your friend
fn find_user(id) {
    if id == 1 { return #{"name": "Alice"} }
    return null
}

let user1 = find_user(1)
let user2 = find_user(999)

let name1 = user1["name"] ?? "unknown"
let name2 = user2 ?? #{"name": "guest"}

println("user 1: {name1}")
println("user 999: {name2}")
assert_eq(name1, "Alice")

println("\nAll good!")
