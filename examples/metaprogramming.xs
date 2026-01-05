// XS Language -- Metaprogramming & Reflection
// Demonstrates: higher-order functions, closures, type reflection,
//               code generation, dispatch tables
// (Macros are not yet implemented; this uses functions instead)

// ---------------------------------------------------------------
// Utility functions (replacing macros)
// ---------------------------------------------------------------

fn swap(arr) {
    // Swap two elements in an array [a, b] -> [b, a]
    let tmp = arr[0]
    arr[0] = arr[1]
    arr[1] = tmp
}

fn min_val(a, b) {
    if a < b { return a } else { return b }
}

fn max_val(a, b) {
    if a > b { return a } else { return b }
}

fn assert_true(cond, msg) {
    if !cond {
        throw "Assertion failed: " ++ msg
    }
}

fn repeat(n, body) {
    var i = 0
    while i < n {
        body(i)
        i = i + 1
    }
}

// ---------------------------------------------------------------
// Code generation via closures
// ---------------------------------------------------------------

fn make_adder(n) {
    return fn(x) { return x + n }
}

fn make_multiplier(n) {
    return fn(x) { return x * n }
}

// Generate dispatch table
fn make_op_table() {
    return #{"add": fn(a, b) { return a + b },
             "sub": fn(a, b) { return a - b },
             "mul": fn(a, b) { return a * b },
             "div": fn(a, b) { return a / b },
             "mod": fn(a, b) { return a % b }}
}

// Memoization decorator
fn memoize(f) {
    let cache = #{"_": null}
    return fn(x) {
        let key = str(x)
        let cached = cache[key]
        if cached != null {
            return cached
        }
        let result = f(x)
        cache[key] = result
        return result
    }
}

// ---------------------------------------------------------------
// Type reflection (runtime)
// ---------------------------------------------------------------

fn describe_value(v) {
    let t = type(v)
    match t {
        "int"    => { return "Int(" ++ str(v) ++ ")" },
        "float"  => { return "Float(" ++ str(v) ++ ")" },
        "bool"   => { return "Bool(" ++ str(v) ++ ")" },
        "str"    => { return "Str(\"" ++ v ++ "\")" },
        "array"  => { return "Array[" ++ str(len(v)) ++ "]" },
        "map"    => { return "Map" },
        "fn"     => { return "Function" },
        _        => { return t ++ "(" ++ str(v) ++ ")" }
    }
}

// ---------------------------------------------------------------
// Reflection with reflect module
// ---------------------------------------------------------------

import reflect

fn show_reflection() {
    println("--- Reflection ---")
    let values = [42, 3.14, true, "hello", [1, 2, 3], #{"key": "val"}]
    for v in values {
        let rt = reflect.type_of(v)
        println("  reflect.type_of({v}) = {rt}")
    }
}

// ---------------------------------------------------------------
// Main
// ---------------------------------------------------------------

fn main() {
    println("=== XS Metaprogramming Demo ===")
    println("")

    // Swap function
    println("--- Swap ---")
    var pair = [10, 20]
    println("Before swap: a={pair[0]}, b={pair[1]}")
    swap(pair)
    println("After swap:  a={pair[0]}, b={pair[1]}")
    println("")

    // Min/Max
    println("--- Min / Max ---")
    println("min(10, 20) = {min_val(10, 20)}")
    println("max(10, 20) = {max_val(10, 20)}")
    println("min(-5, 3) = {min_val(-5, 3)}")
    println("max(-5, 3) = {max_val(-5, 3)}")
    println("")

    // Assert
    println("--- Assert ---")
    assert_true(1 + 1 == 2, "basic arithmetic")
    println("assert(1 + 1 == 2) passed")
    try {
        assert_true(1 > 2, "1 > 2")
    } catch e {
        println("caught: {e}")
    }
    println("")

    // Repeat
    println("--- Repeat ---")
    print("repeat(5): ")
    repeat(5, fn(i) {
        print("{i} ")
    })
    println("")
    println("")

    // Generated functions
    println("--- Code Generation ---")
    let add5 = make_adder(5)
    let add10 = make_adder(10)
    let mul3 = make_multiplier(3)

    println("add5(7)  = {add5(7)}")
    println("add10(7) = {add10(7)}")
    println("mul3(7)  = {mul3(7)}")

    // Compose
    let add15 = fn(x) { return add5(add10(x)) }
    println("add15(0) = {add15(0)}")
    println("")

    // Operator dispatch table
    println("--- Op Dispatch Table ---")
    let ops = make_op_table()
    let op_names = ["add", "sub", "mul", "div", "mod"]
    let op_syms = ["+", "-", "*", "/", "%"]
    for i in 0..5 {
        let name = op_names[i]
        let sym = op_syms[i]
        let f = ops[name]
        let result = f(10, 3)
        println("  10 {sym} 3 = {result}")
    }
    println("")

    // Memoization
    println("--- Memoization ---")
    var call_count = 0
    let slow_fn = fn(n) {
        call_count = call_count + 1
        return n * n
    }
    let fast_fn = memoize(slow_fn)

    println("fast_fn(5) = {fast_fn(5)}  (calls: {call_count})")
    println("fast_fn(5) = {fast_fn(5)}  (calls: {call_count})")
    println("fast_fn(3) = {fast_fn(3)}  (calls: {call_count})")
    println("fast_fn(3) = {fast_fn(3)}  (calls: {call_count})")
    println("")

    // Type reflection
    println("--- Type Reflection ---")
    let values = [42, 3.14, true, "hello", [1, 2, 3], #{"key": "val"}, null]
    for v in values {
        println("  {describe_value(v)}")
    }
    println("")

    // Type checking with type()
    println("--- type() function ---")
    println("  type(42) = {type(42)}")
    println("  type(3.14) = {type(3.14)}")
    println("  type(true) = {type(true)}")
    println("  type(\"hi\") = {type("hi")}")
    println("  type([]) = {type([])}")
    println("  type(null) = {type(null)}")
    println("")

    // Reflection module
    show_reflection()
}
