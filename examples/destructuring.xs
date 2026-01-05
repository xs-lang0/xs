// XS Language -- Destructuring Examples
// Pattern destructuring (let [a,b] = ...) is not yet supported
// Instead, use explicit indexing and field access

fn main() {
    println("=== XS Destructuring (Manual) ===")
    println("")

    // -- Array "destructuring" via indexing --
    println("--- Array Destructuring ---")
    var arr = [10, 20, 30]
    var a = arr[0]
    var b = arr[1]
    var c = arr[2]
    println("a = {a}")
    println("b = {b}")
    println("c = {c}")
    println("")

    // Rest element: use slice
    let full = [1, 2, 3, 4, 5]
    let head = full[0]
    var tail = []
    for i in 1..len(full) {
        tail.push(full[i])
    }
    println("head = {head}")
    println("tail = {tail}")
    println("")

    // Nested arrays
    let nested = [[1, 2], 3]
    let inner = nested[0]
    let x = inner[0]
    let y = inner[1]
    let z = nested[1]
    println("x = {x}, y = {y}, z = {z}")
    println("")

    // Swap variables
    println("--- Swap ---")
    var p = 100
    var q = 200
    println("Before: p = {p}, q = {q}")
    let tmp = p
    p = q
    q = tmp
    println("After:  p = {p}, q = {q}")
    println("")

    // -- Struct field access --
    println("--- Struct Field Access ---")
    struct Point { x, y }
    let pt = Point { x: 5, y: 9 }
    let px = pt.x
    let py = pt.y
    println("Point: x = {px}, y = {py}")
    println("")

    // -- Map field access --
    println("--- Map Field Access ---")
    let person = #{"name": "Alice", "age": 30, "city": "NYC"}
    let name = person.name
    let age = person.age
    let city = person.city
    println("name = {name}")
    println("age  = {age}")
    println("city = {city}")
    println("")

    // -- In function parameters --
    println("--- Function Parameters ---")
    fn sum_first_three(arr) {
        return arr[0] + arr[1] + arr[2]
    }
    println("sum_first_three([5, 10, 15]) = {sum_first_three([5, 10, 15])}")

    fn greet(info) {
        println("Hello {info.name} from {info.city}!")
    }
    greet(#{"name": "Bob", "city": "London"})
    println("")

    // -- Multiple return values via array --
    println("--- Multiple Returns ---")
    fn divmod(a, b) {
        return [a / b, a % b]
    }
    let result = divmod(17, 5)
    let quot = result[0]
    let rem = result[1]
    println("17 / 5 = {quot} remainder {rem}")
    println("")

    // -- Iteration with index --
    println("--- Indexed Iteration ---")
    let items = ["apple", "banana", "cherry"]
    for i in 0..len(items) {
        println("  [{i}] {items[i]}")
    }
}
