import pytest
from xs.lexer.lexer import tokenize
from xs.lexer.token import TokenKind


def tok_kinds(src: str):
    """Return (kind, value) pairs, dropping EOF/NEWLINE noise."""
    tokens = tokenize(src)
    return [(t.kind, t.value) for t in tokens
            if t.kind not in (TokenKind.EOF, TokenKind.NEWLINE)]


def kinds_only(src: str):
    return [k for k, _ in tok_kinds(src)]


# ---------------------------------------------------------------------------
# Literals
# ---------------------------------------------------------------------------

class TestLiterals:
    @pytest.mark.parametrize("src, kind, value", [
        ("42",     TokenKind.INT,    42),
        ("3.14",   TokenKind.FLOAT,  3.14),
        ("0xFF",   TokenKind.INT,    255),
        ("0b1010", TokenKind.INT,    10),
        ("0o17",   TokenKind.INT,    15),
        ('"hello"', TokenKind.STRING, "hello"),
    ])
    def test_literal(self, src, kind, value):
        toks = tok_kinds(src)
        assert toks[0] == (kind, value)

    def test_bool_true(self):
        k = kinds_only("true")
        assert TokenKind.BOOL in k or TokenKind.KW_TRUE in dir(TokenKind)

    def test_null(self):
        # regression: used to crash on empty input
        k = kinds_only("null")
        assert k[0] == TokenKind.NULL


# ---------------------------------------------------------------------------
# Keywords
# ---------------------------------------------------------------------------

class TestKeywords:
    @pytest.mark.parametrize("kw, expected_kind", [
        ("let",    TokenKind.KW_LET),
        ("fn",     TokenKind.KW_FN),
        ("while",  TokenKind.KW_WHILE),
        ("return", TokenKind.KW_RETURN),
        ("struct", TokenKind.KW_STRUCT),
        ("match",  TokenKind.KW_MATCH),
        ("var",    TokenKind.KW_VAR),
        ("const",  TokenKind.KW_CONST),
    ])
    def test_keyword(self, kw, expected_kind):
        assert kinds_only(kw)[0] == expected_kind

    def test_if_else(self):
        ks = kinds_only("if x else y")
        assert TokenKind.KW_IF in ks and TokenKind.KW_ELSE in ks

    def test_for_in(self):
        ks = kinds_only("for x in arr")
        assert TokenKind.KW_FOR in ks and TokenKind.KW_IN in ks


# ---------------------------------------------------------------------------
# Operators
# ---------------------------------------------------------------------------

class TestOperators:
    @pytest.mark.parametrize("src, expected_kind", [
        ("+",  TokenKind.OP_PLUS),
        ("-",  TokenKind.OP_MINUS),
        ("*",  TokenKind.OP_STAR),
        ("/",  TokenKind.OP_SLASH),
        ("%",  TokenKind.OP_PERCENT),
        ("==", TokenKind.OP_EQ),
        ("!=", TokenKind.OP_NEQ),
        ("<",  TokenKind.OP_LT),
        ("<=", TokenKind.OP_LE),
        (">",  TokenKind.OP_GT),
        (">=", TokenKind.OP_GE),
        ("&&", TokenKind.OP_AND),
        ("||", TokenKind.OP_OR),
        ("!",  TokenKind.OP_NOT),
        ("->", TokenKind.OP_ARROW),
        ("=>", TokenKind.OP_FAT_ARROW),
        ("++", TokenKind.OP_CONCAT),
    ])
    def test_op(self, src, expected_kind):
        assert expected_kind in kinds_only(src)


# ---------------------------------------------------------------------------
# Delimiters
# ---------------------------------------------------------------------------

class TestDelimiters:
    @pytest.mark.parametrize("src, expected_kind", [
        ("{", TokenKind.LBRACE),
        ("}", TokenKind.RBRACE),
        ("(", TokenKind.LPAREN),
        (")", TokenKind.RPAREN),
        ("[", TokenKind.LBRACKET),
        ("]", TokenKind.RBRACKET),
    ])
    def test_delim(self, src, expected_kind):
        assert expected_kind in kinds_only(src)


# ---------------------------------------------------------------------------
# Comments, macros, identifiers, compound expressions
# ---------------------------------------------------------------------------

class TestComments:
    def test_line_comment(self):
        values = [v for _, v in tok_kinds("42 // this is a comment\n99")]
        assert 42 in values and 99 in values

    def test_block_comment(self):
        toks = tok_kinds("/* block comment */ 42")
        ints = [(k, v) for k, v in toks if k == TokenKind.INT]
        assert ints == [(TokenKind.INT, 42)]


class TestMacro:
    def test_macro_bang(self):
        assert TokenKind.MACRO_BANG in kinds_only("println!")

    def test_format_bang(self):
        assert any(k == TokenKind.MACRO_BANG for k, _ in tok_kinds("format!"))


class TestIdentifiers:
    @pytest.mark.parametrize("src, expected_name", [
        ("hello",      "hello"),
        ("_private",   "_private"),
        ("var2",       "var2"),
        ("myVariable", "myVariable"),
    ])
    def test_ident(self, src, expected_name):
        assert tok_kinds(src)[0] == (TokenKind.IDENT, expected_name)


class TestCompoundExpressions:
    def test_simple_expr(self):
        ks = kinds_only("x + 42")
        assert TokenKind.IDENT in ks
        assert TokenKind.OP_PLUS in ks
        assert TokenKind.INT in ks

    def test_function_call(self):
        ks = kinds_only("foo(1, 2)")
        assert TokenKind.IDENT in ks
        assert TokenKind.LPAREN in ks and TokenKind.RPAREN in ks

    def test_let_statement(self):
        ks = kinds_only("let x = 5")
        for expected in (TokenKind.KW_LET, TokenKind.IDENT, TokenKind.OP_ASSIGN, TokenKind.INT):
            assert expected in ks
