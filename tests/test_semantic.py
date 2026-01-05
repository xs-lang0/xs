import pytest
from xs.parser.parser import parse
from xs.semantic.analyzer import SemanticAnalyzer


def check(src: str):
    prog = parse(src)
    analyzer = SemanticAnalyzer()
    analyzer.analyze(prog)
    errors = [d for d in analyzer.diagnostics if d.level == "error"]
    warnings = [d for d in analyzer.diagnostics if d.level == "warning"]
    return errors, warnings


def error_msgs(src: str):
    errors, _ = check(src)
    return [e.message for e in errors]


def has_no_errors(src: str):
    return len(check(src)[0]) == 0


# ---------------------------------------------------------------------------
# Imports
# ---------------------------------------------------------------------------

class TestImports:
    @pytest.mark.parametrize("src", [
        "import math\nlet x = math.pi",
        "import base64\nlet e = base64.encode(\"hello\")",
        "import uuid\nlet id = uuid.v4()",
        "import io\nlet f = io.open(\"test\")",
    ])
    def test_import_is_valid(self, src):
        assert has_no_errors(src)

    def test_undefined_without_import(self):
        msgs = error_msgs("let x = nonexistent_var.foo()")
        assert any("nonexistent_var" in m for m in msgs)


# ---------------------------------------------------------------------------
# Destructuring
# ---------------------------------------------------------------------------

class TestDestructuring:
    @pytest.mark.parametrize("src", [
        "let [a, b, c] = [1, 2, 3]\nprintln!(a)",
        "let [head, ..tail] = [1, 2, 3]\nprintln!(head)",
        "let [[x, y], z] = [[1, 2], 3]\nprintln!(x)",
        # struct destructuring
        'struct Point { x: i64, y: i64 }\nlet pt = Point { x: 1, y: 2 }\nlet { x, y } = pt\nprintln!(x)',
        # map destructuring
        'let person = { name: "Alice", age: 30 }\nlet { name, age } = person\nprintln!(name)',
        # fn param destructuring
        "fn sum3(arr) {\n    let [a, b, c] = arr\n    a + b + c\n}",
    ])
    def test_destructuring_accepted(self, src):
        assert has_no_errors(src)


# ---------------------------------------------------------------------------
# Map literals -- identifier keys must not trigger "undefined name"
# ---------------------------------------------------------------------------

class TestMapLiterals:
    def test_ident_key_not_undefined(self):
        msgs = error_msgs('let m = { name: "Alice", age: 30 }')
        assert not any("Undefined name" in m for m in msgs)

    @pytest.mark.parametrize("src", [
        'let m = { "key": "value" }',
        'let m = { name: "Alice", age: 30, score: 3.14 }',
    ])
    def test_valid_map(self, src):
        assert has_no_errors(src)


# ---------------------------------------------------------------------------
# Builtin methods -- just checking sema doesn't reject them
# ---------------------------------------------------------------------------

class TestBuiltinMethods:
    @pytest.mark.parametrize("src", [
        "let a = [1, 2, 3]\nlet s = a.slice(0, 2)",
        "let a = [1, 2, 3]\nlet b = a.contains(2)",
        'let s = "hello"\nlet c = s.chars()',
        'let s = "3.14"\nlet f = s.parse_float()',
        'let m = { name: "Alice" }\nm.set("name", "Bob")\nlet v = m.get("name")',
    ])
    def test_method_accepted(self, src):
        assert has_no_errors(src)


def test_signal_get_set():
    """Signals are a built-in reactive primitive."""
    assert has_no_errors("let s = signal(42)\nlet v = s.get()\ns.set(100)")


# ---------------------------------------------------------------------------
# Constructors
# ---------------------------------------------------------------------------

class TestConstructorCalls:
    def test_struct_constructor(self):
        src = """
struct Point { x: i64, y: i64 }
let p = Point { x: 1, y: 2 }
"""
        assert has_no_errors(src)

    def test_class_constructor_no_spurious_error(self):
        src = """
class Counter {
    fn new(start: i64) -> Counter {
        Counter { count: start }
    }
}
"""
        errors, _ = check(src)
        # edge case from early type checker: "cannot unify Counter with fn(...)"
        assert not any("unify Counter with fn" in e.message for e in errors)


# ---------------------------------------------------------------------------
# Type compatibility
# ---------------------------------------------------------------------------

class TestTypeCompat:
    @pytest.mark.parametrize("src", [
        "let x: u64 = 42",
        "let x: u8 = 255",
    ])
    def test_int_width_assignment(self, src):
        assert has_no_errors(src)

    def test_tuple_with_unknown_elem(self):
        assert has_no_errors("fn identity(x) { x }\nlet t = (identity(1), [1, 2, 3])")


# ---------------------------------------------------------------------------
# Shadowing warnings
# ---------------------------------------------------------------------------

class TestShadowing:
    def test_self_not_flagged(self):
        src = """
class Foo {
    fn method(self, x: i64) -> i64 { x }
}
"""
        _, warnings = check(src)
        shadow = [w for w in warnings if "shadow" in w.message]
        assert not any("self" in w.message for w in shadow)

    def test_redeclare_warns(self):
        _, warnings = check("let x = 1\nlet x = 2")
        assert any("shadow" in w.message for w in warnings)


# ---------------------------------------------------------------------------
# Generics
# ---------------------------------------------------------------------------

class TestGenerics:
    @pytest.mark.parametrize("src", [
        "fn identity<T>(x: T) -> T { x }",
        "fn map_fn<A, B>(x: A, f: fn(A) -> B) -> B { f(x) }",
    ])
    def test_generic_params(self, src):
        assert has_no_errors(src)
