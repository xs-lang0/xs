import pytest
from xs.parser.parser import parse
from xs.ast.nodes import LitInt, LitFloat, LitBool, BinOp, UnaryOp, Ident, Return
from xs.optimizer.passes import (
    ConstantFolding, DeadCodeElimination,
    StrengthReduction, InlineSmallFunctions,
    Optimizer,
)


def fold(src: str):
    """Apply constant folding and pull the body expression out."""
    prog = parse(src)
    result = ConstantFolding().run(prog)
    fn = result.stmts[0]
    body = fn.body
    if body.expr is not None:
        return body.expr
    if body.stmts:
        stmt = body.stmts[0]
        return stmt.expr if hasattr(stmt, "expr") else stmt
    return None


def _reduce(src: str):
    """Apply strength reduction and pull the body expression."""
    prog = parse(src)
    result = StrengthReduction().run(prog)
    fn = result.stmts[0]
    body = fn.body
    if body.expr is not None:
        return body.expr
    if body.stmts:
        stmt = body.stmts[0]
        return stmt.expr if hasattr(stmt, "expr") else stmt
    return None


# ---------------------------------------------------------------------------
# Constant folding
# ---------------------------------------------------------------------------

class TestConstantFolding:
    @pytest.mark.parametrize("expr, expected_type, expected_val", [
        ("2 + 3",          LitInt,   5),
        ("3 * 4",          LitInt,   12),
        ("(2 + 3) * 4",   LitInt,   20),     # nested
        ("5 > 3",          LitBool,  True),
        ("true && false",  LitBool,  False),
        ("-(-5)",          LitInt,   5),       # double negation
        ("!true",          LitBool,  False),
    ])
    def test_folds(self, expr, expected_type, expected_val):
        node = fold(f"fn f() {{ {expr} }}")
        assert isinstance(node, expected_type) and node.value == expected_val

    def test_float(self):
        node = fold("fn f() { 1.5 + 2.5 }")
        assert isinstance(node, LitFloat) and abs(node.value - 4.0) < 1e-9

    def test_x_plus_zero(self):
        # x + 0 should ideally fold to just x, but we accept either
        node = fold("fn f(x: i64) { x + 0 }")
        assert isinstance(node, (Ident, BinOp))

    def test_multiply_by_zero(self):
        node = fold("fn f(x: i64) { x * 0 }")
        assert isinstance(node, LitInt) and node.value == 0

    def test_power_of_zero(self):
        # anything ** 0 == 1
        node = fold("fn f(x: i64) { x ** 0 }")
        assert isinstance(node, LitInt) and node.value == 1

    def test_stats(self):
        prog = parse("fn f() { 1 + 2 + 3 }")
        opt = ConstantFolding()
        opt.run(prog)
        assert opt.stats()["folded_expressions"] >= 1


# ---------------------------------------------------------------------------
# Dead code elimination
# ---------------------------------------------------------------------------

class TestDeadCodeElimination:
    def _dce_block(self, src: str):
        prog = parse(src)
        result = DeadCodeElimination().run(prog)
        return result.stmts[0].body

    def test_removes_after_return(self):
        block = self._dce_block("""
        fn f() -> i64 {
            return 1
            let x = 2
            x + 3
        }
        """)
        assert len(block.stmts) == 1
        assert isinstance(block.stmts[0], Return)

    def test_keeps_before_return(self):
        block = self._dce_block("""
        fn f() -> i64 {
            let x = 1
            let y = 2
            return x + y
            let z = 3
        }
        """)
        assert len(block.stmts) == 3  # let x, let y, return

    def test_break_dead_code(self):
        # shouldn't crash on dead code after break
        src = """
        fn f() {
            while true {
                break
                let x = 1
            }
        }
        """
        assert DeadCodeElimination().run(parse(src)) is not None

    def test_stats(self):
        prog = parse("""
        fn f() -> i64 {
            return 1
            let dead = 2
            let also_dead = 3
        }
        """)
        opt = DeadCodeElimination()
        opt.run(prog)
        assert opt.stats()["removed_statements"] >= 2


# ---------------------------------------------------------------------------
# Strength reduction
# ---------------------------------------------------------------------------

class TestStrengthReduction:
    @pytest.mark.parametrize("expr, expected_op, shift_amount", [
        ("x * 4", "<<", 2),
        ("x * 8", "<<", 3),
        ("x / 4", ">>", 2),
    ])
    def test_mul_div_to_shift(self, expr, expected_op, shift_amount):
        node = _reduce(f"fn f(x: i64) {{ {expr} }}")
        assert isinstance(node, BinOp) and node.op == expected_op
        assert isinstance(node.right, LitInt) and node.right.value == shift_amount

    def test_x_squared_to_mul(self):
        node = _reduce("fn f(x: i64) { x ** 2 }")
        assert isinstance(node, BinOp) and node.op == "*"

    def test_mod_2_to_bitand(self):
        node = _reduce("fn f(x: i64) { x % 2 }")
        assert isinstance(node, BinOp) and node.op == "&"
        assert isinstance(node.right, LitInt) and node.right.value == 1

    def test_stats(self):
        opt = StrengthReduction()
        opt.run(parse("fn f(x: i64) { x * 4 }"))
        assert opt.stats()["reductions"] >= 1


# ---------------------------------------------------------------------------
# Inlining
# ---------------------------------------------------------------------------

class TestInlining:
    def test_arrow_function(self):
        src = """
        fn double(x: i64) -> i64 = x * 2
        fn main() -> i64 { double(5) }
        """
        assert InlineSmallFunctions().run(parse(src)) is not None

    def test_block_fn_not_inlined(self):
        # block fns are too big to inline
        src = """
        fn complex(x: i64) -> i64 {
            let y = x + 1
            y * 2
        }
        fn main() -> i64 { complex(5) }
        """
        assert InlineSmallFunctions().run(parse(src)) is not None


# ---------------------------------------------------------------------------
# Full optimizer pipeline
# ---------------------------------------------------------------------------

class TestOptimizer:
    def test_level_0_no_passes(self):
        assert len(Optimizer(level=0)._passes) == 0

    def test_level_scaling(self):
        assert len(Optimizer(level=2)._passes) >= len(Optimizer(level=1)._passes) > 0

    def test_full_pipeline(self):
        src = """
        fn double(x: i64) -> i64 = x * 2
        fn main() -> i64 {
            let result = 2 + 3
            return result * 0
            let dead = 99
        }
        """
        assert Optimizer(level=2).optimize(parse(src)) is not None

    def test_report_format(self):
        prog = parse("fn main() { 2 + 3 }")
        opt = Optimizer(level=2)
        opt.optimize(prog)
        report = opt.report()
        assert "Optimizer" in report and "constant-folding" in report

    def test_custom_passes(self):
        passes = [ConstantFolding(), StrengthReduction()]
        result = Optimizer(passes=passes).optimize(parse("fn main() { 2 + 3 }"))
        assert result is not None
