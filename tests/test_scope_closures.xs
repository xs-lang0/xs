-- ===========================================
-- Test: Scope and Closure Behavior
-- ===========================================

var passed = 0
var failed = 0

fn check(cond, label) {
    if cond {
        passed = passed + 1
    } else {
        println("FAIL: " ++ label)
        failed = failed + 1
    }
}

-- === 1. Basic closure captures variable ===
fn make_adder(n) {
    |x| x + n
}
let add5 = make_adder(5)
let add10 = make_adder(10)
check(add5(3) == 8, "closure add5(3)")
check(add10(3) == 13, "closure add10(3)")

-- === 2. Closure captures mutable variable ===
fn make_counter() {
    var count = 0
    |delta| {
        count = count + delta
        count
    }
}
let counter = make_counter()
check(counter(1) == 1, "counter first call")
check(counter(1) == 2, "counter second call")
check(counter(5) == 7, "counter third call")

-- === 3. Two closures share the same captured variable ===
var shared = 10
let get_shared = || shared
let set_shared = |v| { shared = v }

check(get_shared() == 10, "shared initial value")
set_shared(20)
check(get_shared() == 20, "shared after set via closure")
check(shared == 20, "shared visible outside closure")

-- === 4. Closure captures let (immutable) ===
let immutable_val = 42
let get_imm = || immutable_val
check(get_imm() == 42, "closure captures let value")

-- === 5. Closure captures function parameter ===
fn make_multiplier(factor) {
    |x| x * factor
}
let triple = make_multiplier(3)
let quadruple = make_multiplier(4)
check(triple(5) == 15, "multiplier closure 3*5")
check(quadruple(5) == 20, "multiplier closure 4*5")

-- === 6. Nested closures ===
fn outer() {
    var x = 10
    fn middle() {
        var y = 20
        fn inner() {
            x + y
        }
        inner
    }
    middle
}
let mid = outer()
let inn = mid()
check(inn() == 30, "nested closure captures x+y")

-- === 7. Closure survives parent scope ===
fn create_greeter(name) {
    fn greet() {
        "Hello, " ++ name ++ "!"
    }
    greet
}
let g1 = create_greeter("Alice")
let g2 = create_greeter("Bob")
check(g1() == "Hello, Alice!", "surviving closure Alice")
check(g2() == "Hello, Bob!", "surviving closure Bob")

-- === 8. Closures in loops share loop variable ===
-- for-in with range: closures capture the same loop variable
var shared_closures = []
for i in range(5) {
    shared_closures.push(|| i)
}
-- After loop, i has the value from last iteration
-- The closures see the current value of i at call time
check(shared_closures[0]() == 0, "loop closure [0] sees 0")
check(shared_closures[4]() == 4, "loop closure [4] sees 4")

-- === 9. Closures in loops with let binding capture individual values ===
var captured_closures = []
for i in range(5) {
    let captured = i
    captured_closures.push(|| captured)
}
check(captured_closures[0]() == 0, "let-captured closure [0] is 0")
check(captured_closures[2]() == 2, "let-captured closure [2] is 2")
check(captured_closures[4]() == 4, "let-captured closure [4] is 4")

-- === 10. Closure as callback ===
fn apply(f, x) {
    f(x)
}
check(apply(|x| x * x, 5) == 25, "closure as callback")
check(apply(|x| x + 1, 10) == 11, "lambda callback")

-- === 11. Higher-order function returning closure ===
fn compose(f, g) {
    |x| f(g(x))
}
let inc = |x| x + 1
let dbl = |x| x * 2
let inc_then_dbl = compose(dbl, inc)
let dbl_then_inc = compose(inc, dbl)
check(inc_then_dbl(3) == 8, "compose dbl(inc(3)) = 8")
check(dbl_then_inc(3) == 7, "compose inc(dbl(3)) = 7")

-- === 12. Closure with multiple captured variables ===
fn make_range_checker(lo, hi) {
    |x| x >= lo && x <= hi
}
let in_range = make_range_checker(1, 10)
check(in_range(5) == true, "5 in range [1,10]")
check(in_range(0) == false, "0 not in range [1,10]")
check(in_range(10) == true, "10 in range [1,10]")
check(in_range(11) == false, "11 not in range [1,10]")

-- === 13. Accumulator closure ===
fn make_accumulator() {
    var items = []
    fn add(item) {
        items.push(item)
        items
    }
    add
}
let acc = make_accumulator()
acc("a")
acc("b")
let result = acc("c")
check(result.len() == 3, "accumulator has 3 items")
check(result[0] == "a", "accumulator first item")
check(result[2] == "c", "accumulator last item")

-- === 14. Variable shadowing in nested scopes ===
let x = 10
fn shadow_test() {
    let x = 20
    x
}
check(shadow_test() == 20, "inner x shadows outer")
check(x == 10, "outer x unchanged")

-- === 15. Block scoping with if ===
var outer_val = "original"
if true {
    var inner_val = "inside"
    outer_val = "modified"
}
check(outer_val == "modified", "if block can modify outer var")

-- === 16. Function scope isolation ===
var global_counter = 0

fn increment() {
    global_counter = global_counter + 1
}

fn get_count() {
    global_counter
}

increment()
increment()
increment()
check(get_count() == 3, "function scope shares global")

-- === 17. Recursive closure ===
-- Use a var to hold the recursive function
var fib = null
fib = fn(n) {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}
check(fib(10) == 55, "recursive closure fib(10)")

-- === 18. Closure factory pattern ===
fn make_operations() {
    var state = 0
    let ops = #{
        "inc": || { state = state + 1; state },
        "dec": || { state = state - 1; state },
        "get": || state
    }
    ops
}
let ops = make_operations()
ops["inc"]()
ops["inc"]()
ops["inc"]()
ops["dec"]()
check(ops["get"]() == 2, "closure factory state is 2")

-- === 19. Closures with different arities ===
fn make_funcs() {
    let zero_arg = || 42
    let one_arg = |x| x + 1
    let two_arg = |x, y| x + y
    [zero_arg, one_arg, two_arg]
}
let funcs = make_funcs()
check(funcs[0]() == 42, "zero arg closure")
check(funcs[1](5) == 6, "one arg closure")
check(funcs[2](3, 4) == 7, "two arg closure")

-- === 20. Closure capturing struct field ===
struct Config { value: int }
fn make_config_reader(cfg) {
    || cfg.value
}
let cfg = Config { value: 99 }
let reader = make_config_reader(cfg)
check(reader() == 99, "closure captures struct field")

-- === 21. Multiple independent counters ===
let c1 = make_counter()
let c2 = make_counter()
c1(1)
c1(1)
c1(1)
c2(10)
check(c1(0) == 3, "counter c1 is independent at 3")
check(c2(0) == 10, "counter c2 is independent at 10")

-- === 22. Closure in array map ===
var factor = 3
let nums = [1, 2, 3, 4, 5]
let result2 = nums.map(|x| x * factor)
check(result2[0] == 3, "map closure captures factor [0]")
check(result2[4] == 15, "map closure captures factor [4]")

-- === 23. Closure in filter ===
var threshold = 3
let filtered = [1, 2, 3, 4, 5].filter(|x| x > threshold)
check(filtered.len() == 2, "filter closure captures threshold")
check(filtered[0] == 4, "filter result[0]")

-- === 24. Deeply nested function calls with closures ===
fn level1(a) {
    fn level2(b) {
        fn level3(c) {
            a + b + c
        }
        level3
    }
    level2
}
let l2 = level1(100)
let l3 = l2(20)
check(l3(3) == 123, "deeply nested closures sum to 123")

-- === 25. Closure and string interpolation ===
fn make_formatter(prefix) {
    |msg| "{prefix}: {msg}"
}
let fmt_info = make_formatter("INFO")
let fmt_err = make_formatter("ERROR")
check(fmt_info("test") == "INFO: test", "formatter closure INFO")
check(fmt_err("fail") == "ERROR: fail", "formatter closure ERROR")

-- Summary
println("")
println("Scope and Closure Test Results:")
println("  Passed: " ++ str(passed))
println("  Failed: " ++ str(failed))
if failed > 0 {
    println("SOME TESTS FAILED!")
} else {
    println("ALL SCOPE AND CLOSURE TESTS PASSED!")
}
