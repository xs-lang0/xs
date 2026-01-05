-- Comprehensive VM vs interpreter comparison tests
-- Each group in a function to avoid local overflow

var passed = 0
var failed = 0

fn check(name: str, actual, expected) {
    if actual == expected {
        passed += 1
    } else {
        println("FAIL: " ++ name ++ ": got " ++ str(actual) ++ " expected " ++ str(expected))
        failed += 1
    }
}

fn test_logic() {
    check("and_false", true && false, false)
    check("and_short", false && true, false)
    check("or_true", true || false, true)
    check("or_fallback", false || true, true)
    check("and_true", true && true, true)
    check("null_coalesce", null ?? 42, 42)
    check("not_null_coalesce", 10 ?? 42, 10)
    check("if_expr", if true { "yes" } else { "no" }, "yes")
}

fn test_operators() {
    fn double_val(x: int) -> int { x * 2 }
    fn add_one(x: int) -> int { x + 1 }
    check("pipe_basic", 5 |> double_val, 10)
    check("pipe_chain", 5 |> double_val |> add_one, 11)
    check("floor_div_int", 7 // 2, 3)
    check("spaceship_lt", 1 <=> 2, -1)
    check("spaceship_eq", 2 <=> 2, 0)
    check("spaceship_gt", 3 <=> 2, 1)
    check("bit_and", 0xFF & 0x0F, 15)
    check("bit_or", 0xF0 | 0x0F, 255)
    check("bit_xor", 0xFF ^ 0x0F, 240)
    check("bit_shl", 1 << 8, 256)
    check("bit_shr", 256 >> 4, 16)
}

fn test_in_is() {
    check("in_array", 2 in [1, 2, 3], true)
    check("in_array_not", 5 in [1, 2, 3], false)
    check("in_string", "lo" in "hello", true)
    check("in_map", "x" in {"x": 1, "y": 2}, true)
    check("is_int", 42 is int, true)
    check("is_str", "hi" is str, true)
    check("is_bool", true is bool, true)
}

fn test_strings() {
    let greet_name = "world"
    check("interp_basic", "hello {greet_name}", "hello world")
    let xv = 42
    check("interp_expr", "val={xv + 1}", "val=43")
    let sv = "Hello World"
    check("str_upper", sv.upper(), "HELLO WORLD")
    check("str_lower", sv.lower(), "hello world")
    check("str_len", sv.len(), 11)
    check("str_contains", sv.contains("World"), true)
    check("str_starts_with", sv.starts_with("Hello"), true)
    check("str_ends_with", sv.ends_with("World"), true)
    check("str_trim", "  hi  ".trim(), "hi")
    check("str_replace", "abcabc".replace("b", "X"), "aXcaXc")
    check("str_repeat", "ab".repeat(3), "ababab")
    check("str_concat", "hello" ++ " " ++ "world", "hello world")
    check("chain_methods", "hello world".upper().lower(), "hello world")
}

fn test_arrays() {
    let arr = [3, 1, 2]
    check("arr_len", arr.len(), 3)
    check("arr_contains_yes", arr.contains(2), true)
    check("arr_contains_no", arr.contains(99), false)
    check("arr_map", str([1, 2, 3].map(fn(x) { x * 2 })), "[2, 4, 6]")
    check("arr_filter", str([1, 2, 3, 4, 5].filter(fn(x) { x > 3 })), "[4, 5]")
    check("arr_reduce", [1, 2, 3, 4].reduce(fn(a, b) { a + b }), 10)
    check("arr_join", ["a", "b", "c"].join("-"), "a-b-c")
    let ai = [10, 20, 30]
    check("arr_idx_0", ai[0], 10)
    check("arr_idx_2", ai[2], 30)
    check("neg_idx", [1,2,3][-1], 3)
}

fn test_maps() {
    let mp = {"x": 1, "y": 2}
    check("map_has_yes", mp.has("x"), true)
    check("map_has_no", mp.has("z"), false)
    let mi = {"key": "val"}
    check("map_idx", mi["key"], "val")
}

fn test_closures() {
    fn make_counter() {
        var count = 0
        fn inc() { count += 1; count }
        inc
    }
    let counter = make_counter()
    check("closure_1", counter(), 1)
    check("closure_2", counter(), 2)
    check("closure_3", counter(), 3)

    fn make_adder(x: int) {
        fn add(y: int) { x + y }
        add
    }
    let add5 = make_adder(5)
    check("nested_closure", add5(10), 15)
}

fn test_functions() {
    fn factorial(n: int) -> int {
        if n <= 1 { 1 } else { n * factorial(n - 1) }
    }
    check("recursion", factorial(10), 3628800)

    fn apply(f, x) { f(x) }
    check("higher_order", apply(fn(x) { x * x }, 7), 49)
}

fn test_match() {
    fn classify(x: int) -> str {
        match x {
            0 => "zero",
            n if n > 0 => "positive",
            _ => "negative"
        }
    }
    check("match_guard_pos", classify(5), "positive")
    check("match_guard_zero", classify(0), "zero")
    check("match_guard_neg", classify(-3), "negative")
}

fn test_try_catch() {
    fn safe_div(a: int, b: int) {
        try {
            if b == 0 { throw "div by zero" }
            a / b
        } catch _e {
            -1
        }
    }
    check("try_ok", safe_div(10, 2), 5)
    check("try_err", safe_div(10, 0), -1)
}

fn test_defer() {
    -- defer causes segfault with many nested inner protos
    -- tested separately in vm_try_catch.xs
}

fn test_classes() {
    class Point {
        var x = 0
        var y = 0
        fn init(self, x: int, y: int) {
            self.x = x
            self.y = y
        }
        fn sum(self) -> int { self.x + self.y }
    }
    let pt = Point(3, 4)
    check("class_x", pt.x, 3)
    check("class_y", pt.y, 4)
    check("class_method", pt.sum(), 7)
}

fn test_enums() {
    enum Shape {
        Circle(r),
        Rect(w, h)
    }
    let circ = Shape::Circle(5)
    let area = match circ {
        Shape::Circle(r) => r * r,
        _ => 0
    }
    check("enum_match", area, 25)
}

fn test_comprehensions() {
    let squares = [x * x for x in range(1, 6)]
    check("list_comp", str(squares), "[1, 4, 9, 16, 25]")
    let evens = [x for x in range(1, 11) if x % 2 == 0]
    check("list_comp_filter", str(evens), "[2, 4, 6, 8, 10]")
}

fn test_spread() {
    let base_arr = [1, 2, 3]
    let ext_arr = [0, ...base_arr, 4]
    check("spread_array", str(ext_arr), "[0, 1, 2, 3, 4]")
    -- map spread tested separately in vm_spread.xs
}

fn test_loops() {
    var rsum = 0
    for i in range(1, 6) { rsum += i }
    check("range_for", rsum, 15)

    var nsum = 0
    for i in range(1, 4) {
        for j in range(1, 4) { nsum += i * j }
    }
    check("nested_for", nsum, 36)

    var wc = 0
    var wi = 1
    while wi <= 10 { wc += wi; wi += 1 }
    check("while_sum", wc, 55)

    var ca = 10
    ca += 5; check("compound_add", ca, 15)
    ca -= 3; check("compound_sub", ca, 12)
    ca *= 2; check("compound_mul", ca, 24)
}

fn test_modules() {
    -- module tested separately
}

fn test_break_value() {
    -- tested in vm_break.xs
}

fn main() {
    test_logic()
    test_operators()
    test_in_is()
    test_strings()
    test_arrays()
    test_maps()
    test_closures()
    test_functions()
    test_match()
    test_try_catch()
    test_defer()
    test_classes()
    test_enums()
    test_comprehensions()
    test_spread()
    test_loops()
    test_modules()
    test_break_value()

    println("---")
    println("Passed: " ++ str(passed))
    println("Failed: " ++ str(failed))
    if failed > 0 {
        println("SOME TESTS FAILED")
    } else {
        println("ALL TESTS PASSED")
    }
}
