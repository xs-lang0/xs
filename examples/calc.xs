// XS Language -- Calculator Demo
// Non-interactive version that demonstrates math module functions

import math

fn main() {
    println("=== XS Calculator Demo ===")
    println("")

    // Basic arithmetic
    println("--- Basic Arithmetic ---")
    println("10 + 3 = {10 + 3}")
    println("10 - 3 = {10 - 3}")
    println("10 * 3 = {10 * 3}")
    println("10 / 3 = {10 / 3}")
    println("10 % 3 = {10 % 3}")
    println("")

    // Math module functions
    println("--- Trigonometry ---")
    println("sin(0)    = {math.sin(0)}")
    println("cos(0)    = {math.cos(0)}")
    println("tan(0)    = {math.tan(0)}")
    println("asin(1)   = {math.asin(1)}")
    println("acos(1)   = {math.acos(1)}")
    println("atan(1)   = {math.atan(1)}")
    println("atan2(1, 1) = {math.atan2(1, 1)}")
    println("")

    println("--- Powers & Roots ---")
    println("sqrt(16)    = {math.sqrt(16)}")
    println("sqrt(2)     = {math.sqrt(2)}")
    println("pow(2, 10)  = {math.pow(2, 10)}")
    println("pow(3, 3)   = {math.pow(3, 3)}")
    println("exp(1)      = {math.exp(1)}")
    println("")

    println("--- Logarithms ---")
    println("log(1)      = {math.log(1)}")
    println("log(2.718)  = {math.log(2.718)}")
    println("log2(8)     = {math.log2(8)}")
    println("log10(100)  = {math.log10(100)}")
    println("")

    println("--- Rounding ---")
    println("floor(3.7)  = {math.floor(3.7)}")
    println("floor(-2.3) = {math.floor(-2.3)}")
    println("ceil(3.2)   = {math.ceil(3.2)}")
    println("ceil(-2.7)  = {math.ceil(-2.7)}")
    println("")

    println("--- Geometry ---")
    println("hypot(3, 4) = {math.hypot(3, 4)}")
    println("degrees(3.14159) = {math.degrees(3.14159)}")
    println("radians(180)     = {math.radians(180)}")
    println("")

    // Build a simple expression evaluator
    println("--- Expression Evaluator ---")

    fn eval_expr(op, a, b) {
        match op {
            "+" => { return a + b },
            "-" => { return a - b },
            "*" => { return a * b },
            "/" => { return a / b },
            "%" => { return a % b },
            _   => { return 0 }
        }
    }

    let expressions = [
        #{"op": "+", "a": 15, "b": 7},
        #{"op": "-", "a": 100, "b": 37},
        #{"op": "*", "a": 12, "b": 8},
        #{"op": "/", "a": 144, "b": 12},
        #{"op": "%", "a": 17, "b": 5}
    ]

    for expr in expressions {
        let result = eval_expr(expr.op, expr.a, expr.b)
        println("  {expr.a} {expr.op} {expr.b} = {result}")
    }
}
