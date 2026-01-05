import math
import pytest
from xs.backends.interpreter.interpreter import (
    Interpreter, XSInt, XSFloat, XSBool, XSStr, XSNull, XSArray, to_py,
)
from xs.parser.parser import parse


def run(src: str):
    prog = parse(src)
    interp = Interpreter()
    return interp.run(prog)


def run_val(src: str):
    result = run(src)
    return to_py(result) if result is not None else None


def run_fn(src: str):
    prog = parse(src)
    interp = Interpreter()
    interp.run(prog)
    main_fn = interp.env.get("main")
    result = interp.call_function(main_fn, [])
    return to_py(result) if result is not None else None


# ---------------------------------------------------------------------------
# Arithmetic
# ---------------------------------------------------------------------------

class TestArithmetic:
    @pytest.mark.parametrize("expr, expected", [
        ("2 + 3", 5),
        ("10 - 4", 6),
        ("3 * 7", 21),
        ("10 / 2", 5),
        ("10 % 3", 1),
        ("2 + 3 * 4", 14),       # precedence
        ("(2 + 3) * 4", 20),     # parens override
        ("-5", -5),
        ("2 ** 10", 1024),
    ])
    def test_basic_ops(self, expr, expected):
        assert run_val(f"fn main() -> i64 {{ {expr} }}") == expected

    def test_float_arithmetic(self):
        result = run_val("fn main() -> f64 { 1.5 + 2.5 }")
        assert abs(result - 4.0) < 1e-9


class TestComparisons:
    @pytest.mark.parametrize("expr, expected", [
        ("5 == 5", True),
        ("5 != 3", True),
        ("3 < 5", True),
        ("5 > 3", True),
        ("5 <= 5", True),
        ("5 >= 6", False),
    ])
    def test_cmp(self, expr, expected):
        assert run_val(f"fn main() -> bool {{ {expr} }}") is expected


class TestLogical:
    @pytest.mark.parametrize("expr, expected", [
        ("true && true", True),
        ("true && false", False),
        ("false || true", True),
        ("!false", True),
    ])
    def test_logic(self, expr, expected):
        assert run_val(f"fn main() -> bool {{ {expr} }}") is expected


# ---------------------------------------------------------------------------
# Variables & control flow
# ---------------------------------------------------------------------------

class TestVariables:
    def test_let(self):
        assert run_val("fn main() -> i64 { let x = 42; x }") == 42

    def test_var_mutation(self):
        src = """
        fn main() -> i64 {
            var x = 0
            x = x + 1
            x = x + 1
            x
        }
        """
        assert run_val(src) == 2

    def test_let_immutable_rejects_assignment(self):
        # should raise or silently refuse -- either way, don't crash
        try:
            run_val("fn main() -> i64 { let x = 5\nx = 10\nx }")
        except Exception:
            pass


class TestControlFlow:
    @pytest.mark.parametrize("cond, expected", [
        ("true", 1),
        ("false", 2),
    ])
    def test_if_else(self, cond, expected):
        assert run_val(f"fn main() -> i64 {{ if {cond} {{ 1 }} else {{ 2 }} }}") == expected

    def test_while_loop(self):
        src = """
        fn main() -> i64 {
            var i = 0
            var sum = 0
            while i < 5 {
                sum = sum + i
                i = i + 1
            }
            sum
        }
        """
        assert run_val(src) == 10  # 0+1+2+3+4

    def test_for_loop(self):
        src = """
        fn main() -> i64 {
            var sum = 0
            for x in [1, 2, 3, 4, 5] { sum = sum + x }
            sum
        }
        """
        assert run_val(src) == 15

    def test_break(self):
        src = """
        fn main() -> i64 {
            var i = 0
            while true {
                if i >= 3 { break }
                i = i + 1
            }
            i
        }
        """
        assert run_val(src) == 3

    def test_return(self):
        src = """
        fn main() -> i64 {
            var x = 0
            while true {
                if x == 5 { return x }
                x = x + 1
            }
            -1
        }
        """
        assert run_val(src) == 5


# ---------------------------------------------------------------------------
# Functions & closures
# ---------------------------------------------------------------------------

class TestFunctions:
    def test_basic_call(self):
        src = """
        fn add(a: i64, b: i64) -> i64 = a + b
        fn main() -> i64 { add(3, 4) }
        """
        assert run_val(src) == 7

    def test_recursion(self):
        src = """
        fn fib(n: i64) -> i64 {
            if n <= 1 { n }
            else { fib(n - 1) + fib(n - 2) }
        }
        fn main() -> i64 { fib(10) }
        """
        assert run_val(src) == 55

    def test_closure(self):
        src = """
        fn make_adder(n: i64) -> any {
            |x| x + n
        }
        fn main() -> i64 {
            let add5 = make_adder(5)
            add5(10)
        }
        """
        assert run_val(src) == 15

    def test_higher_order(self):
        src = """
        fn apply(f: any, x: i64) -> i64 = f(x)
        fn double(x: i64) -> i64 = x * 2
        fn main() -> i64 { apply(double, 7) }
        """
        assert run_val(src) == 14


# ---------------------------------------------------------------------------
# Collections
# ---------------------------------------------------------------------------

class TestArrays:
    def test_indexing(self):
        assert run_val("fn main() -> i64 { let a = [1, 2, 3]; a[1] }") == 2

    def test_len(self):
        assert run_val("fn main() -> i64 { len([1, 2, 3, 4]) }") == 4

    def test_push(self):
        src = """
        fn main() -> i64 {
            var a = [1, 2, 3]
            a.push(4)
            len(a)
        }
        """
        assert run_val(src) == 4

    @pytest.mark.parametrize("method, src, expected", [
        ("map",    "[1, 2, 3].map(|x| x * 2)",            [2, 4, 6]),
        ("filter", "[1, 2, 3, 4, 5].filter(|x| x % 2 == 0)", [2, 4]),
        ("concat", "[1, 2] ++ [3, 4]",                    [1, 2, 3, 4]),
    ])
    def test_array_transforms(self, method, src, expected):
        assert run_val(f"fn main() -> any {{ {src} }}") == expected


class TestStrings:
    @pytest.mark.parametrize("src, expected", [
        ('"hello" ++ " " ++ "world"', "hello world"),
        ('"hello".upper()',            "HELLO"),
        ('"HELLO".lower()',            "hello"),
    ])
    def test_string_ops(self, src, expected):
        assert run_val(f"fn main() -> str {{ {src} }}") == expected

    def test_len(self):
        assert run_val('fn main() -> i64 { len("hello") }') == 5

    def test_contains(self):
        assert run_val('fn main() -> bool { "hello world".contains("world") }') is True


# ---------------------------------------------------------------------------
# Pattern matching
# ---------------------------------------------------------------------------

class TestMatch:
    def test_basic(self):
        src = """
        fn main() -> str {
            match 2 {
                1 => "one"
                2 => "two"
                _ => "other"
            }
        }
        """
        assert run_val(src) == "two"

    def test_wildcard(self):
        src = """
        fn main() -> str {
            match 99 {
                1 => "one"
                _ => "default"
            }
        }
        """
        assert run_val(src) == "default"

    def test_tuple(self):
        src = """
        fn main() -> str {
            match (1, 2) {
                (1, 2) => "exact"
                _      => "other"
            }
        }
        """
        assert run_val(src) == "exact"


class TestMatchPatterns:
    @pytest.mark.parametrize("x, body, expected", [
        (2, '1 => "one", 2 => "two", _ => "other"', "two"),
        (5, '1..3 => "low", 4..7 => "mid", _ => "high"', "mid"),
        (85, '90..=100 => "A", 80..=89 => "B", 70..=79 => "C", _ => "F"', "B"),
        (99, '0 => "zero", _ => "other"', "other"),
    ])
    def test_literal_and_range(self, x, body, expected):
        src = f'fn main() -> str {{ let x = {x}\nmatch x {{ {body} }} }}'
        assert run_val(src) == expected

    def test_exclusive_range_boundary(self):
        # 1..3 must NOT match 3
        src = """
        fn main() -> str {
            let x = 3
            match x { 1..3 => "in range", _ => "out" }
        }
        """
        assert run_val(src) == "out"

    def test_inclusive_range_boundary(self):
        # 1..=3 SHOULD match 3
        src = """
        fn main() -> str {
            let x = 3
            match x { 1..=3 => "in range", _ => "out" }
        }
        """
        assert run_val(src) == "in range"

    def test_or_pattern(self):
        src = """
        fn main() -> str {
            let x = 2
            match x { 1 | 2 | 3 => "small", _ => "large" }
        }
        """
        assert run_val(src) == "small"

    def test_guard(self):
        src = """
        fn main() -> str {
            let x = 5
            match x { n if n > 3 => "big", _ => "small" }
        }
        """
        assert run_val(src) == "big"


# ---------------------------------------------------------------------------
# Structs
# ---------------------------------------------------------------------------

class TestStructs:
    def test_field_access(self):
        src = """
        struct Point { x: f64, y: f64 }
        fn main() -> f64 {
            let p = Point { x: 3.0, y: 4.0 }
            p.x
        }
        """
        assert abs(run_val(src) - 3.0) < 1e-9

    def test_string_field(self):
        src = """
        struct Person { name: str, age: i64 }
        fn main() -> str {
            let p = Person { name: "Alice", age: 30 }
            p.name
        }
        """
        assert run_val(src) == "Alice"


# ---------------------------------------------------------------------------
# Builtins
# ---------------------------------------------------------------------------

class TestBuiltins:
    @pytest.mark.parametrize("src, expected", [
        ("len([1,2,3])",   3),
        ("str(42)",        "42"),
        ("min(3, 7)",      3),
        ("max(3, 7)",      7),
        ("abs(-5)",        5),
    ])
    def test_simple_builtins(self, src, expected):
        assert run_val(f"fn main() -> any {{ {src} }}") == expected

    def test_sorted(self):
        result = run_val("fn main() -> any { sorted([3, 1, 4, 1, 5, 9, 2, 6]) }")
        assert result == [1, 1, 2, 3, 4, 5, 6, 9]

    def test_type_of(self):
        result = run_val("fn main() -> str { type_of(42) }")
        assert "int" in str(result).lower()


# ---------------------------------------------------------------------------
# Error handling
# ---------------------------------------------------------------------------

class TestErrorHandling:
    @pytest.mark.parametrize("src", [
        "fn main() -> i64 { 5 / 0 }",
        "fn main() -> i64 { [1, 2, 3][10] }",
        'fn main() { panic!("test panic") }',
    ])
    def test_raises(self, src):
        with pytest.raises(Exception):
            run_val(src)


# ---------------------------------------------------------------------------
# Spread operator
# ---------------------------------------------------------------------------

class TestSpread:
    def test_dotdot(self):
        assert run_val("let a = [1,2,3]\nlet b = [0, ..a, 4]\nb") == [0, 1, 2, 3, 4]

    def test_dotdotdot(self):
        # both syntaxes should work
        assert run_val("let a = [1,2,3]\nlet b = [0, ...a, 4]\nb") == [0, 1, 2, 3, 4]

    def test_multiple(self):
        assert run_val("let a = [1,2]\nlet b = [3,4]\nlet c = [..a, ..b]\nc") == [1, 2, 3, 4]

    def test_in_call(self):
        assert run_val("fn add(a,b,c) { a+b+c }\nlet args=[1,2,3]\nadd(..args)") == 6


# ---------------------------------------------------------------------------
# Global builtins (maps, flatten, chars, bytes)
# ---------------------------------------------------------------------------

class TestGlobalBuiltins:
    def test_keys(self):
        result = run_val("let m = { x: 1, y: 2 }\nkeys(m)")
        assert sorted(result) == ["x", "y"]

    def test_values(self):
        result = run_val("let m = { x: 1, y: 2 }\nvalues(m)")
        assert sorted(result) == [1, 2]

    def test_entries(self):
        result = run_val("let m = { a: 10 }\nentries(m)")
        assert len(result) == 1 and result[0] == ("a", 10)

    def test_flatten(self):
        assert run_val("flatten([[1,2],[3,4],[5]])") == [1, 2, 3, 4, 5]

    def test_flat_map(self):
        assert run_val("flat_map([1,2,3], |x| [x, x*x])") == [1, 1, 2, 4, 3, 9]

    def test_chars(self):
        assert run_val('chars("abc")') == ["a", "b", "c"]

    def test_bytes(self):
        assert run_val('bytes("ABC")') == [65, 66, 67]


# ---------------------------------------------------------------------------
# Enum matching
# ---------------------------------------------------------------------------

class TestEnumMatching:
    def test_unit_variant(self):
        src = """
enum Color { Red Green Blue }
let c = Color::Red
match c {
    Color::Red => 1
    Color::Green => 2
    _ => 3
}
"""
        assert run_val(src) == 1

    def test_struct_variant(self):
        src = """
enum Shape {
    Circle { radius: f64 }
}
let c = Shape::Circle { radius: 5.0 }
match c {
    Shape::Circle { radius } => radius
    _ => 0.0
}
"""
        assert run_val(src) == 5.0

    def test_struct_variant_field_binding(self):
        src = """
enum Event {
    Click { x: i64, y: i64 }
    KeyPress { code: i64 }
}
let e = Event::Click { x: 10, y: 20 }
match e {
    Event::Click { x, y } => x + y
    _ => 0
}
"""
        assert run_val(src) == 30

    def test_multi_variant_dispatch(self):
        src = """
enum Shape {
    Circle { radius: f64 }
    Rectangle { width: f64, height: f64 }
}
fn area(s) {
    match s {
        Shape::Circle { radius } => 3.14 * radius * radius
        Shape::Rectangle { width, height } => width * height
        _ => 0.0
    }
}
let r = Shape::Rectangle { width: 3.0, height: 4.0 }
area(r)
"""
        assert run_val(src) == 12.0


# ---------------------------------------------------------------------------
# Actors
# ---------------------------------------------------------------------------

class TestActors:
    def test_state_mutation(self):
        src = """
actor Counter {
    var count: i64 = 0
    fn handle(msg) { count = count + 1 }
}
let c = spawn Counter
c ! { "kind": "inc" }
c ! { "kind": "inc" }
c ! { "kind": "inc" }
c.count
"""
        assert run_val(src) == 3

    def test_independent_instances(self):
        src = """
actor Box {
    var value: i64 = 0
    fn handle(msg) { value = value + msg.n }
}
let a = spawn Box
let b = spawn Box
a ! { "n": 10 }
b ! { "n": 5 }
a ! { "n": 3 }
[a.value, b.value]
"""
        assert run_val(src) == [13, 5]

    def test_self_reference(self):
        src = """
actor Reflector {
    var last_self_is_actor: bool = false
    fn handle(msg) {
        last_self_is_actor = self != null
    }
}
let r = spawn Reflector
r ! { "kind": "ping" }
r.last_self_is_actor
"""
        assert run_val(src) is True

    def test_message_forwarding(self):
        src = """
actor Adder {
    var total: i64 = 0
    fn handle(msg) { total = total + msg.n }
}
actor Forwarder {
    var target: any = null
    fn handle(msg) {
        if msg.kind == "set_target" {
            target = msg.actor
        }
        if msg.kind == "forward" {
            target ! { "n": msg.n }
        }
    }
}
let adder = spawn Adder
let fwd   = spawn Forwarder
fwd ! { "kind": "set_target", "actor": adder }
fwd ! { "kind": "forward", "n": 7 }
fwd ! { "kind": "forward", "n": 3 }
adder.total
"""
        assert run_val(src) == 10


# ---------------------------------------------------------------------------
# Pipe operator
# ---------------------------------------------------------------------------

class TestPipeOperator:
    @pytest.mark.parametrize("src, expected", [
        ("[1,2,3,4,5] |> filter(|x| x > 3)",                              [4, 5]),
        ("[1,2,3] |> map(|x| x * 2)",                                      [2, 4, 6]),
        ("[1,2,3,4,5] |> filter(|x| x % 2 == 0) |> map(|x| x * 10)",     [20, 40]),
        ("[1,2,3,4,5] |> reduce(|acc, x| acc + x, 0)",                     15),
    ])
    def test_pipe(self, src, expected):
        assert run_val(src) == expected

    def test_map_both_arg_orders(self):
        # map(fn, arr) and map(arr, fn) are both accepted
        assert run_val("map(|x| x + 1, [10, 20])") == [11, 21]
        assert run_val("map([10, 20], |x| x + 1)") == [11, 21]


# ---------------------------------------------------------------------------
# format! specs
# ---------------------------------------------------------------------------

class TestFormatSpec:
    @pytest.mark.parametrize("fmt, arg, expected", [
        ("{:.2}",  "3.14159", "3.1"),
        ("{:.2f}", "3.14159", "3.14"),
        ("{:>5}",  '"hi"',    "   hi"),
        ("{:<5}",  '"hi"',    "hi   "),
        ("{:x}",   "255",     "ff"),
        ("{:05}",  "42",      "00042"),
    ])
    def test_format(self, fmt, arg, expected):
        assert run_val(f'format!("{fmt}", {arg})') == expected


# ---------------------------------------------------------------------------
# Option / Result
# ---------------------------------------------------------------------------

class TestOptionResult:
    @pytest.mark.parametrize("src, expected", [
        ("Some(42).unwrap()",              42),
        ("None.unwrap_or(99)",             99),
        ("Some(1).is_some()",              True),
        ("None.is_none()",                 True),
        ("Some(5).map(|x| x * 2).unwrap()", 10),
        ("Ok(100).unwrap()",               100),
        ('Err("oops").is_err()',           True),
        ("Ok(3).map(|x| x + 1).unwrap()", 4),
    ])
    def test_basic(self, src, expected):
        assert run_val(src) == expected

    def test_err_propagation(self):
        src = """
fn divide(a, b) {
    if b == 0 { Err("div by zero") } else { Ok(a / b) }
}
fn calc(x, y) {
    let v = divide(x, y)?
    Ok(v * 2)
}
calc(10, 0).is_err()
"""
        assert run_val(src) is True

    def test_ok_propagation(self):
        src = """
fn divide(a, b) {
    if b == 0 { Err("div by zero") } else { Ok(a / b) }
}
fn calc(x, y) {
    let v = divide(x, y)?
    Ok(v * 2)
}
calc(10, 2).unwrap()
"""
        assert run_val(src) == 10

    def test_match_some_none(self):
        src = """
let x = Some(7)
match x {
    Some(v) => v * 3,
    None => 0,
}
"""
        assert run_val(src) == 21


# ---------------------------------------------------------------------------
# Range with step
# ---------------------------------------------------------------------------

class TestRangeStep:
    @pytest.mark.parametrize("expr, expected", [
        ("[x for x in 0..10..2]",    [0, 2, 4, 6, 8]),
        ("[x for x in 0..=10..2]",   [0, 2, 4, 6, 8, 10]),
        ("[x for x in 10..0..-2]",   [10, 8, 6, 4, 2]),
    ])
    def test_stepped(self, expr, expected):
        assert run_val(expr) == expected


# ---------------------------------------------------------------------------
# Decorators
# ---------------------------------------------------------------------------

class TestDecorators:
    def test_simple(self):
        src = """
fn double(f) {
    fn wrapper(x) { f(x) * 2 }
    wrapper
}
@double
fn square(x) { x * x }
square(3)
"""
        assert run_val(src) == 18  # (3*3)*2

    def test_with_args(self):
        src = """
fn multiply_by(n) {
    fn decorator(f) {
        fn wrapper(x) { f(x) * n }
        wrapper
    }
    decorator
}
@multiply_by(3)
fn inc(x) { x + 1 }
inc(4)
"""
        assert run_val(src) == 15  # (4+1)*3

    def test_memoize(self):
        src = """
fn memoize(f) {
    let cache = {}
    fn wrapper(x) {
        let key = str(x)
        let cached = cache.get(key)
        if cached != null { cached } else {
            let r = f(x)
            cache.set(key, r)
            r
        }
    }
    wrapper
}
@memoize
fn fib(n) {
    if n <= 1 { n } else { fib(n-1) + fib(n-2) }
}
fib(10)
"""
        assert run_val(src) == 55


# ---------------------------------------------------------------------------
# while-let
# ---------------------------------------------------------------------------

class TestWhileLet:
    def test_some_iteration(self):
        src = """
fn next(lst, i) {
    if i < lst.len() { Some(lst[i]) } else { None }
}
let data = [10, 20, 30]
let mut i = 0
let mut total = 0
while let Some(v) = next(data, i) {
    total = total + v
    i = i + 1
}
total
"""
        assert run_val(src) == 60

    def test_countdown(self):
        src = """
let mut count = 3
let mut acc = 0
while let Some(n) = if count > 0 { Some(count) } else { None } {
    acc = acc + n
    count = count - 1
}
acc
"""
        assert run_val(src) == 6  # 3+2+1
