import pytest
from xs.parser.parser import parse, ParseError
from xs.ast.nodes import (
    Program, FnDecl, LetStmt, VarStmt, ConstStmt,
    BinOp, UnaryOp, LitInt, LitFloat, LitBool, LitString, LitNull,
    Ident, Call, Block, If, While, For, Return,
    StructDecl, EnumDecl, ClassDecl,
    Lambda, Match, MatchArm, PatRange, MethodCall,
)


def ast(src: str) -> Program:
    return parse(src)


def first_decl(src: str):
    return ast(src).stmts[0]


def first_stmt(src: str):
    fn = first_decl(src)
    return fn.body.stmts[0]


def expr_in_fn(src: str):
    """Wrap in fn main() and pull the body expression."""
    prog = ast(f"fn main() {{ {src} }}")
    fn = prog.stmts[0]
    body = fn.body
    if body.expr is not None:
        return body.expr
    if body.stmts:
        stmt = body.stmts[0]
        return stmt.expr if hasattr(stmt, "expr") else stmt
    return None


def _body_node(fn_body):
    if fn_body.expr is not None:
        return fn_body.expr
    return fn_body.stmts[0] if fn_body.stmts else None


# ---------------------------------------------------------------------------
# Literals
# ---------------------------------------------------------------------------

class TestLiterals:
    @pytest.mark.parametrize("src, node_type, value", [
        ("42",    LitInt,    42),
        ("true",  LitBool,   True),
        ("false", LitBool,   False),
        ('"hello"', LitString, "hello"),
    ])
    def test_literal(self, src, node_type, value):
        node = expr_in_fn(src)
        assert isinstance(node, node_type) and node.value == value

    def test_float(self):
        node = expr_in_fn("3.14")
        assert isinstance(node, LitFloat) and abs(node.value - 3.14) < 1e-9

    def test_null(self):
        assert isinstance(expr_in_fn("null"), LitNull)


# ---------------------------------------------------------------------------
# Binary & unary ops
# ---------------------------------------------------------------------------

class TestBinaryOps:
    def test_addition(self):
        node = expr_in_fn("1 + 2")
        assert isinstance(node, BinOp) and node.op == "+"

    def test_precedence(self):
        # 1 + 2 * 3 -> 1 + (2 * 3)
        node = expr_in_fn("1 + 2 * 3")
        assert isinstance(node, BinOp) and node.op == "+"
        assert isinstance(node.right, BinOp) and node.right.op == "*"

    @pytest.mark.parametrize("src, op", [
        ("x == y", "=="),
        ("a && b", "&&"),
        ("a || b", "||"),
    ])
    def test_op(self, src, op):
        node = expr_in_fn(src)
        assert isinstance(node, BinOp) and node.op == op


class TestUnaryOps:
    @pytest.mark.parametrize("src, op", [("-x", "-"), ("!x", "!")])
    def test_unary(self, src, op):
        node = expr_in_fn(src)
        assert isinstance(node, UnaryOp) and node.op == op


# ---------------------------------------------------------------------------
# Declarations
# ---------------------------------------------------------------------------

class TestVariableDeclarations:
    def test_let(self):
        stmt = first_stmt("fn main() { let x = 42 }")
        assert isinstance(stmt, LetStmt)
        assert hasattr(stmt, "pattern")  # LetStmt uses PatIdent

    def test_var(self):
        stmt = first_stmt("fn main() { var x = 42 }")
        assert isinstance(stmt, VarStmt) and stmt.name == "x"

    def test_const(self):
        assert isinstance(ast("const PI: f64 = 3.14").stmts[0], ConstStmt)

    def test_let_with_type_annotation(self):
        stmt = first_stmt("fn main() { let x: i64 = 5 }")
        assert isinstance(stmt, LetStmt) and stmt.type_ann is not None


class TestFunctions:
    def test_simple_fn(self):
        fn = ast("fn add(x: i64, y: i64) -> i64 { x + y }").stmts[0]
        assert isinstance(fn, FnDecl) and fn.name == "add" and len(fn.params) == 2

    def test_arrow_fn(self):
        fn = ast("fn double(x: i64) -> i64 = x * 2").stmts[0]
        assert isinstance(fn, FnDecl) and not isinstance(fn.body, Block)

    def test_no_return_type(self):
        fn = ast("fn greet(name: str) { println!(name) }").stmts[0]
        assert isinstance(fn, FnDecl) and fn.ret_type is None

    def test_nested(self):
        src = """
        fn outer() {
            fn inner() { 42 }
            inner()
        }
        """
        assert isinstance(ast(src).stmts[0], FnDecl)


# ---------------------------------------------------------------------------
# Control flow -- keep individual tests because the AST setup is non-trivial
# ---------------------------------------------------------------------------

class TestControlFlow:
    def test_if(self):
        assert isinstance(expr_in_fn("if x > 0 { 1 } else { -1 }"), If)

    def test_if_elif_else(self):
        src = """
        fn main() {
            if x < 0 { -1 }
            elif x == 0 { 0 }
            else { 1 }
        }
        """
        fn = ast(src).stmts[0]
        assert isinstance(_body_node(fn.body), If)

    def test_while(self):
        fn = ast("fn main() { while i < 10 { i = i + 1 } }").stmts[0]
        assert isinstance(_body_node(fn.body), While)

    def test_for_in(self):
        fn = ast("fn main() { for x in arr { println!(x) } }").stmts[0]
        assert isinstance(_body_node(fn.body), For)

    def test_return(self):
        fn = ast("fn main() { return 42 }").stmts[0]
        assert isinstance(_body_node(fn.body), Return)


# ---------------------------------------------------------------------------
# Structs & enums
# ---------------------------------------------------------------------------

class TestStructs:
    def test_basic(self):
        s = ast("struct Point { x: f64, y: f64 }").stmts[0]
        assert isinstance(s, StructDecl) and s.name == "Point"

    def test_with_impl(self):
        src = """
        struct Point { x: f64, y: f64 }
        impl Point {
            fn distance(self) -> f64 = sqrt(self.x * self.x + self.y * self.y)
        }
        """
        assert len(ast(src).stmts) >= 2


class TestEnums:
    def test_simple(self):
        assert isinstance(ast("enum Color { Red, Green, Blue }").stmts[0], EnumDecl)

    def test_with_data(self):
        assert isinstance(
            ast("enum Shape { Circle(f64), Rectangle(f64, f64), Point }").stmts[0],
            EnumDecl,
        )


# ---------------------------------------------------------------------------
# Lambdas
# ---------------------------------------------------------------------------

class TestLambda:
    def test_single_param(self):
        assert isinstance(expr_in_fn("|x| x * 2"), Lambda)

    def test_multi_param(self):
        node = expr_in_fn("|a, b| a + b")
        assert isinstance(node, Lambda) and len(node.params) == 2

    def test_block_body(self):
        assert isinstance(expr_in_fn("|x| { let y = x + 1; y * 2 }"), Lambda)


# ---------------------------------------------------------------------------
# Match -- keep complex AST checks as standalone tests
# ---------------------------------------------------------------------------

class TestMatch:
    def test_basic(self):
        src = """
        fn main() {
            match x {
                0 => "zero"
                1 => "one"
                _ => "other"
            }
        }
        """
        fn = ast(src).stmts[0]
        assert isinstance(_body_node(fn.body), Match)

    def test_with_guards(self):
        src = """
        fn main() {
            match n {
                x if x < 0 => "negative"
                0           => "zero"
                _           => "positive"
            }
        }
        """
        fn = ast(src).stmts[0]
        assert isinstance(_body_node(fn.body), Match)

    def test_range_exclusive(self):
        src = """
        fn main() {
            match x { 1..10 => "in range", _ => "out" }
        }
        """
        fn = ast(src).stmts[0]
        node = _body_node(fn.body)
        assert isinstance(node, Match)
        arm = node.arms[0]
        assert isinstance(arm.pattern, PatRange) and arm.pattern.inclusive is False

    def test_range_inclusive(self):
        src = """
        fn main() {
            match x { 1..=10 => "in range", _ => "out" }
        }
        """
        fn = ast(src).stmts[0]
        node = _body_node(fn.body)
        arm = node.arms[0]
        assert isinstance(arm.pattern, PatRange) and arm.pattern.inclusive is True

    def test_range_negative_start(self):
        # edge case: negative number at range start
        src = """
        fn main() {
            match x { -10..-1 => "negative", _ => "other" }
        }
        """
        fn = ast(src).stmts[0]
        node = _body_node(fn.body)
        assert isinstance(node.arms[0].pattern, PatRange)


# ---------------------------------------------------------------------------
# Calls
# ---------------------------------------------------------------------------

class TestCalls:
    def test_no_args(self):
        assert isinstance(expr_in_fn("foo()"), Call)

    def test_with_args(self):
        node = expr_in_fn("add(1, 2)")
        assert isinstance(node, Call) and len(node.args) == 2

    def test_nested(self):
        assert isinstance(expr_in_fn("f(g(x))"), Call)

    def test_method_call(self):
        assert isinstance(expr_in_fn("arr.push(42)"), MethodCall)


# ---------------------------------------------------------------------------
# Error recovery -- parser should not crash
# ---------------------------------------------------------------------------

class TestParseErrors:
    def test_unclosed_brace(self):
        assert parse("fn main() { let x = 5") is not None

    def test_missing_equals(self):
        # regression: used to segfault on malformed let
        assert parse("let x 5") is not None
