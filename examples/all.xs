// XS Language -- Comprehensive Feature Showcase
// Tests all working features of XS

import math
import string
import hash
import base64
import uuid
import time
import collections
import reflect

// =================================================================
// 1. Arithmetic
// =================================================================

fn demo_arithmetic() {
    println("=== 1. Arithmetic ===")

    // Integer arithmetic
    println("10 + 3 = {10 + 3}")
    println("10 - 3 = {10 - 3}")
    println("10 * 3 = {10 * 3}")
    println("10 / 3 = {10 / 3}")
    println("10 % 3 = {10 % 3}")

    // Float arithmetic
    println("3.14 * 2 = {3.14 * 2}")
    println("10.0 / 3.0 = {10.0 / 3.0}")

    // Negative numbers
    println("-5 + 3 = {-5 + 3}")
    println("-5 * -3 = {-5 * -3}")

    // Math module
    println("sqrt(144) = {math.sqrt(144)}")
    println("pow(2, 10) = {math.pow(2, 10)}")
    println("floor(3.7) = {math.floor(3.7)}")
    println("ceil(3.2) = {math.ceil(3.2)}")
    println("")
}

// =================================================================
// 2. Strings
// =================================================================

fn demo_strings() {
    println("=== 2. Strings ===")

    let greeting = "Hello, World!"
    println("greeting: {greeting}")
    println("length: {len(greeting)}")
    println("upper: {greeting.upper()}")
    println("lower: {greeting.lower()}")

    // String interpolation
    let name = "XS"
    let version = 1
    println("Welcome to {name} v{version}!")

    // String concatenation with ++
    let full = "Hello" ++ " " ++ "World"
    println("concat: {full}")

    // String module
    println("pad_left: '{string.pad_left("hi", 10)}'")
    println("pad_right: '{string.pad_right("hi", 10)}'")
    println("center: '{string.center("hi", 10)}'")
    println("truncate: '{string.truncate("hello world", 8)}'")
    println("escape_html: {string.escape_html("<b>bold</b>")}")
    println("is_numeric: {string.is_numeric("42")}")
    println("camel_to_snake: {string.camel_to_snake("myFunction")}")
    println("snake_to_camel: {string.snake_to_camel("my_function")}")

    // String contains and indexing
    let s = "hello"
    println("s[0] = {s[0]}")
    println("s[4] = {s[4]}")
    println("")
}

// =================================================================
// 3. Arrays
// =================================================================

fn demo_arrays() {
    println("=== 3. Arrays ===")

    let arr = [1, 2, 3, 4, 5]
    println("arr: {arr}")
    println("len: {len(arr)}")
    println("arr[0]: {arr[0]}")
    println("arr[4]: {arr[4]}")

    // Mutable operations
    var nums = [10, 20, 30]
    nums.push(40)
    println("after push(40): {nums}")
    let popped = nums.pop()
    println("popped: {popped}, arr: {nums}")

    // Nested arrays
    let matrix = [[1, 2, 3], [4, 5, 6], [7, 8, 9]]
    println("matrix[1][2] = {matrix[1][2]}")

    // Array of mixed types
    let mixed = [42, "hello", true, 3.14, null]
    println("mixed: {mixed}")

    // Iteration
    let fruits = ["apple", "banana", "cherry"]
    for fruit in fruits {
        println("  fruit: {fruit}")
    }
    println("")
}

// =================================================================
// 4. Maps
// =================================================================

fn demo_maps() {
    println("=== 4. Maps ===")

    let m = #{"name": "Alice", "age": 30, "city": "NYC"}
    println("map: {m}")
    println("name: {m.name}")
    println("age: {m.age}")

    // Keys and values
    println("keys: {m.keys()}")
    println("values: {m.values()}")

    // Map mutation
    var config = #{"debug": false, "verbose": true}
    config.debug = true
    config.level = 5
    println("config: {config}")

    // Nested maps
    let nested = #{"outer": #{"inner": "value"}}
    println("nested: {nested.outer.inner}")
    println("")
}

// =================================================================
// 5. Functions & Closures
// =================================================================

fn demo_functions() {
    println("=== 5. Functions & Closures ===")

    // Regular function
    fn add(a, b) {
        return a + b
    }
    println("add(3, 4) = {add(3, 4)}")

    // Recursive function
    fn factorial(n) {
        if n <= 1 { return 1 }
        return n * factorial(n - 1)
    }
    println("5! = {factorial(5)}")
    println("10! = {factorial(10)}")

    // Fibonacci
    fn fib(n) {
        if n <= 1 { return n }
        return fib(n - 1) + fib(n - 2)
    }
    println("fib(10) = {fib(10)}")

    // Higher-order functions
    fn apply(f, x) {
        return f(x)
    }
    let double = fn(x) { return x * 2 }
    println("apply(double, 21) = {apply(double, 21)}")

    // Closures
    fn make_counter() {
        var count = 0
        return fn() {
            count = count + 1
            return count
        }
    }
    let counter = make_counter()
    println("counter: {counter()}, {counter()}, {counter()}")

    // Closure factory
    fn make_adder(n) {
        return fn(x) { return x + n }
    }
    let add5 = make_adder(5)
    let add10 = make_adder(10)
    println("add5(7) = {add5(7)}")
    println("add10(7) = {add10(7)}")

    // Composition
    fn compose(f, g) {
        return fn(x) { return f(g(x)) }
    }
    let add15 = compose(add5, add10)
    println("add15(0) = {add15(0)}")
    println("")
}

// =================================================================
// 6. Control Flow
// =================================================================

fn demo_control_flow() {
    println("=== 6. Control Flow ===")

    // If/else
    let x = 42
    if x > 50 {
        println("big")
    } else if x > 20 {
        println("{x} is medium")
    } else {
        println("small")
    }

    // While loop
    var sum = 0
    var i = 1
    while i <= 10 {
        sum = sum + i
        i = i + 1
    }
    println("sum 1..10 = {sum}")

    // For loop with range
    var product = 1
    for n in 1..=5 {
        product = product * n
    }
    println("product 1..=5 = {product}")

    // Loop with break
    var count = 0
    loop {
        count = count + 1
        if count >= 5 { break }
    }
    println("loop count: {count}")

    // Continue
    var evens = []
    for i in 0..10 {
        if i % 2 != 0 { continue }
        evens.push(i)
    }
    println("evens: {evens}")

    // Nested loops
    println("Multiplication table (3x3):")
    for i in 1..=3 {
        var row = "  "
        for j in 1..=3 {
            let val = i * j
            row = row ++ str(val) ++ "\t"
        }
        println(row)
    }
    println("")
}

// =================================================================
// 7. Match
// =================================================================

fn demo_match() {
    println("=== 7. Match ===")

    // Basic match
    let day = 3
    match day {
        1 => println("Monday"),
        2 => println("Tuesday"),
        3 => println("Wednesday"),
        4 => println("Thursday"),
        5 => println("Friday"),
        _ => println("Weekend")
    }

    // OR patterns
    let n = 2
    match n {
        1 | 2 | 3 => println("{n} is small"),
        4 | 5     => println("{n} is medium"),
        _         => println("{n} is large")
    }

    // Match with strings
    let cmd = "help"
    match cmd {
        "help" | "h" | "?" => println("Showing help..."),
        "quit" | "q"       => println("Quitting..."),
        _                  => println("Unknown: {cmd}")
    }

    // Match for type dispatch
    fn type_name(v) {
        let t = type(v)
        match t {
            "int"   => { return "integer" },
            "float" => { return "floating point" },
            "str"   => { return "string" },
            "bool"  => { return "boolean" },
            "array" => { return "array" },
            "map"   => { return "map" },
            _       => { return t }
        }
    }
    println("{type_name(42)}, {type_name("hi")}, {type_name(true)}")
    println("")
}

// =================================================================
// 8. Structs
// =================================================================

fn demo_structs() {
    println("=== 8. Structs ===")

    struct Point { x, y }
    struct Rect { origin, width, height }

    let p = Point { x: 10, y: 20 }
    println("Point: ({p.x}, {p.y})")

    let r = Rect { origin: Point { x: 0, y: 0 }, width: 100, height: 50 }
    println("Rect: origin=({r.origin.x},{r.origin.y}) size={r.width}x{r.height}")

    // Struct as data container
    struct Color { r, g, b }
    let red = Color { r: 255, g: 0, b: 0 }
    let green = Color { r: 0, g: 255, b: 0 }
    println("Red:   ({red.r}, {red.g}, {red.b})")
    println("Green: ({green.r}, {green.g}, {green.b})")

    // Functions operating on structs
    fn point_distance(p) {
        return math.sqrt(p.x * p.x + p.y * p.y)
    }
    let p2 = Point { x: 3, y: 4 }
    println("distance from origin: {point_distance(p2)}")
    println("")
}

// =================================================================
// 9. Error Handling
// =================================================================

fn demo_errors() {
    println("=== 9. Error Handling ===")

    // Try/catch
    try {
        throw "something went wrong"
    } catch e {
        println("caught: {e}")
    }

    // Function that may throw
    fn divide(a, b) {
        if b == 0 {
            throw "division by zero"
        }
        return a / b
    }

    try {
        let result = divide(10, 3)
        println("10 / 3 = {result}")
    } catch e {
        println("error: {e}")
    }

    try {
        let result = divide(10, 0)
        println("should not reach here")
    } catch e {
        println("caught: {e}")
    }

    // Nested try/catch
    try {
        try {
            throw "inner error"
        } catch e {
            println("inner caught: {e}")
            throw "re-thrown: " ++ e
        }
    } catch e {
        println("outer caught: {e}")
    }
    println("")
}

// =================================================================
// 10. Modules
// =================================================================

fn demo_modules() {
    println("=== 10. Modules ===")

    // math
    println("math.sqrt(2) = {math.sqrt(2)}")
    println("math.pow(2, 8) = {math.pow(2, 8)}")

    // string
    println("string.words('hello world') = {string.words("hello world")}")

    // hash
    println("hash.md5('test') = {hash.md5("test")}")

    // base64
    let encoded = base64.encode("XS Language")
    println("base64.encode = {encoded}")
    println("base64.decode = {base64.decode(encoded)}")

    // uuid
    let id = uuid.v4()
    println("uuid.v4 = {id}")
    println("uuid.is_valid = {uuid.is_valid(id)}")

    // time
    println("time.now = {time.now()}")

    // collections
    let stack = collections.Stack()
    stack.push("a")
    stack.push("b")
    stack.push("c")
    println("stack.pop = {stack.pop()}")
    println("stack.peek = {stack.peek()}")

    // reflect
    println("reflect.type_of(42) = {reflect.type_of(42)}")
    println("reflect.type_of([]) = {reflect.type_of([])}")
    println("")
}

// =================================================================
// 11. Ranges
// =================================================================

fn demo_ranges() {
    println("=== 11. Ranges ===")

    // Exclusive range
    var exclusive = []
    for i in 0..5 {
        exclusive.push(i)
    }
    println("0..5 = {exclusive}")

    // Inclusive range
    var inclusive = []
    for i in 1..=5 {
        inclusive.push(i)
    }
    println("1..=5 = {inclusive}")

    // Range for iteration
    var squares = []
    for i in 1..=5 {
        squares.push(i * i)
    }
    println("squares: {squares}")

    // Countdown
    var countdown = []
    var n = 5
    while n > 0 {
        countdown.push(n)
        n = n - 1
    }
    println("countdown: {countdown}")
    println("")
}

// =================================================================
// 12. String Interpolation
// =================================================================

fn demo_interpolation() {
    println("=== 12. String Interpolation ===")

    let name = "World"
    let age = 25
    let pi = 3.14159

    println("Hello, {name}!")
    println("Age: {age}, Pi: {pi}")

    // Expressions in interpolation
    println("2 + 3 = {2 + 3}")
    println("10 > 5 is {10 > 5}")

    // Function calls in interpolation
    println("sqrt(25) = {math.sqrt(25)}")
    println("len([1,2,3]) = {len([1, 2, 3])}")

    // Nested interpolation
    let items = ["a", "b", "c"]
    println("items has {len(items)} elements: {items}")
    println("")
}

// =================================================================
// 13. Bitwise Operations
// =================================================================

fn demo_bitwise() {
    println("=== 13. Bitwise Operations ===")

    println("0xFF & 0x0F = {0xFF & 0x0F}")
    println("0xF0 | 0x0F = {0xF0 | 0x0F}")
    println("0xFF ^ 0xAA = {0xFF ^ 0xAA}")
    println("1 << 8 = {1 << 8}")
    println("256 >> 4 = {256 >> 4}")

    // Bit flags
    let READ = 1
    let WRITE = 2
    let EXEC = 4
    let perms = READ | WRITE
    println("perms = {perms}")
    println("has READ: {(perms & READ) != 0}")
    println("has EXEC: {(perms & EXEC) != 0}")
    println("")
}

// =================================================================
// 14. Comparison & Boolean
// =================================================================

fn demo_booleans() {
    println("=== 14. Comparison & Boolean ===")

    println("5 > 3: {5 > 3}")
    println("5 < 3: {5 < 3}")
    println("5 >= 5: {5 >= 5}")
    println("5 <= 4: {5 <= 4}")
    println("5 == 5: {5 == 5}")
    println("5 != 3: {5 != 3}")

    println("true && false: {true && false}")
    println("true || false: {true || false}")
    println("!true: {!true}")
    println("!false: {!false}")
    println("")
}

// =================================================================
// 15. Type System
// =================================================================

fn demo_types() {
    println("=== 15. Type System ===")

    println("type(42) = {type(42)}")
    println("type(3.14) = {type(3.14)}")
    println("type(true) = {type(true)}")
    println("type(\"hi\") = {type("hi")}")
    println("type([1,2]) = {type([1, 2])}")
    println("type(null) = {type(null)}")

    let m = #{"a": 1}
    println("type(map) = {type(m)}")

    fn foo() { return 1 }
    println("type(fn) = {type(foo)}")

    // reflect module
    println("reflect.type_of(42) = {reflect.type_of(42)}")
    println("reflect.type_of(3.14) = {reflect.type_of(3.14)}")
    println("")
}

// =================================================================
// Main -- run all demos
// =================================================================

fn main() {
    println("=== XS Language -- Complete Feature Showcase ===")
    println("")

    demo_arithmetic()
    demo_strings()
    demo_arrays()
    demo_maps()
    demo_functions()
    demo_control_flow()
    demo_match()
    demo_structs()
    demo_errors()
    demo_modules()
    demo_ranges()
    demo_interpolation()
    demo_bitwise()
    demo_booleans()
    demo_types()

    println("=== All demos complete! ===")
}
