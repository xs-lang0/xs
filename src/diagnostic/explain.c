#include "diagnostic/diagnostic.h"
#include "diagnostic/colorize.h"
#include "core/xs.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char *code;
    const char *title;
    const char *explanation;
} ErrorExplanation;

static const ErrorExplanation explanations[] = {
    /* Lexer */
    {
        "L0001",
        "unterminated string literal",
        "A string literal was opened with a quote character but never\n"
        "closed. The lexer reached the end of the line (or file) without\n"
        "finding a matching closing quote.\n"
        "\n"
        "Incorrect:\n"
        "\n"
        "  let name = \"hello\n"
        "\n"
        "The string starts at the `\"` but no closing `\"` is found.\n"
        "\n"
        "Corrected:\n"
        "\n"
        "  let name = \"hello\"\n"
        "\n"
        "For multi-line strings, use triple quotes:\n"
        "\n"
        "  let text = \"\"\"hello\n"
        "  world\"\"\"\n"
    },
    {
        "L0002",
        "unterminated block comment",
        "A block comment was opened with `{-` but the matching `-}`\n"
        "was never found. Block comments in XS are nestable, so every\n"
        "`{-` must have a corresponding `-}`.\n"
        "\n"
        "Incorrect:\n"
        "\n"
        "  {- this comment never ends\n"
        "  let x = 42\n"
        "\n"
        "Corrected:\n"
        "\n"
        "  {- this comment is properly closed -}\n"
        "  let x = 42\n"
        "\n"
        "Nested comments must also be balanced:\n"
        "\n"
        "  {- outer {- inner -} still outer -}\n"
    },
    {
        "L0003",
        "invalid escape sequence",
        "A string literal contains a backslash followed by a character\n"
        "that is not a recognized escape sequence.\n"
        "\n"
        "Recognized escapes: \\n, \\t, \\r, \\\\, \\\", \\', \\0, \\x{hex}\n"
        "\n"
        "Incorrect:\n"
        "\n"
        "  let path = \"C:\\new_folder\"\n"
        "\n"
        "Here `\\n` is interpreted as a newline, not a literal backslash-n.\n"
        "\n"
        "Corrected:\n"
        "\n"
        "  let path = \"C:\\\\new_folder\"\n"
        "\n"
        "Use `\\\\` for a literal backslash.\n"
    },
    {
        "L0004",
        "invalid number literal",
        "A number literal has invalid syntax. This can happen with\n"
        "malformed hex literals, multiple decimal points, or trailing\n"
        "underscores.\n"
        "\n"
        "Incorrect:\n"
        "\n"
        "  let x = 0xGG\n"
        "  let y = 1.2.3\n"
        "  let z = 100_\n"
        "\n"
        "Corrected:\n"
        "\n"
        "  let x = 0xFF\n"
        "  let y = 1.23\n"
        "  let z = 100\n"
        "\n"
        "Underscores can be used as digit separators: `1_000_000`.\n"
    },

    // --- Parser
    {
        "P0001",
        "unexpected token",
        "The parser encountered a token that does not fit the expected\n"
        "grammar at this position. This usually means a typo or missing\n"
        "syntax element.\n"
        "\n"
        "Incorrect:\n"
        "\n"
        "  let x = = 5\n"
        "\n"
        "The second `=` is unexpected after the first `=`.\n"
        "\n"
        "Corrected:\n"
        "\n"
        "  let x = 5\n"
        "\n"
        "Check the surrounding context for missing operators,\n"
        "identifiers, or delimiters.\n"
    },
    {
        "P0002",
        "expected expression",
        "The parser expected an expression (a value, variable, function\n"
        "call, etc.) but found something else.\n"
        "\n"
        "Incorrect:\n"
        "\n"
        "  let x = \n"
        "  let y = 5\n"
        "\n"
        "The `let x =` requires a value expression on the right side.\n"
        "\n"
        "Corrected:\n"
        "\n"
        "  let x = 0\n"
        "  let y = 5\n"
        "\n"
        "Every `let`, `var`, or `const` binding with `=` needs a value.\n"
    },
    {
        "P0003",
        "expected statement",
        "The parser expected a statement (let, var, fn, if, for, etc.)\n"
        "but found a token that cannot begin a statement.\n"
        "\n"
        "Incorrect:\n"
        "\n"
        "  fn main() {\n"
        "      ) + 5\n"
        "  }\n"
        "\n"
        "A `)` cannot begin a statement.\n"
        "\n"
        "Corrected:\n"
        "\n"
        "  fn main() {\n"
        "      let x = (3) + 5\n"
        "  }\n"
        "\n"
        "Ensure each line begins with a valid statement or expression.\n"
    },
    {
        "P0010",
        "missing semicolon",
        "XS uses newlines as statement terminators in most cases, but\n"
        "some contexts require explicit separation. This error indicates\n"
        "that two statements appear to run together.\n"
        "\n"
        "Incorrect:\n"
        "\n"
        "  let x = 5 let y = 10\n"
        "\n"
        "Two `let` bindings on the same line without separation.\n"
        "\n"
        "Corrected:\n"
        "\n"
        "  let x = 5\n"
        "  let y = 10\n"
        "\n"
        "Place each statement on its own line.\n"
    },
    {
        "P0011",
        "missing closing delimiter",
        "An opening delimiter (parenthesis, bracket, or brace) was found\n"
        "but the matching closing delimiter is missing.\n"
        "\n"
        "Incorrect:\n"
        "\n"
        "  let arr = [1, 2, 3\n"
        "  println(arr)\n"
        "\n"
        "The `[` on the first line is never closed with `]`.\n"
        "\n"
        "Corrected:\n"
        "\n"
        "  let arr = [1, 2, 3]\n"
        "  println(arr)\n"
        "\n"
        "Ensure every `(`, `[`, or `{` has a matching `)`, `]`, or `}`.\n"
    },
    {
        "P0012",
        "unmatched brace/paren/bracket",
        "A closing delimiter was found without a corresponding opening\n"
        "delimiter, or the wrong type of closing delimiter was used.\n"
        "\n"
        "Incorrect:\n"
        "\n"
        "  fn foo() {\n"
        "      let x = 5\n"
        "\n"
        "  fn bar() {\n"
        "      let y = 10\n"
        "  }\n"
        "\n"
        "The `foo` function body is never closed. The `}` on line 6\n"
        "closes `bar`, but `foo` remains open.\n"
        "\n"
        "Corrected:\n"
        "\n"
        "  fn foo() {\n"
        "      let x = 5\n"
        "  }\n"
        "\n"
        "  fn bar() {\n"
        "      let y = 10\n"
        "  }\n"
    },
    {
        "P0020",
        "unknown keyword",
        "An identifier was used in a keyword position but is not a\n"
        "recognized XS keyword. This can happen due to typos.\n"
        "\n"
        "Incorrect:\n"
        "\n"
        "  func add(a: i32, b: i32) -> i32 {\n"
        "      return a + b\n"
        "  }\n"
        "\n"
        "XS uses `fn` for function declarations, not `func`.\n"
        "\n"
        "Corrected:\n"
        "\n"
        "  fn add(a: i32, b: i32) -> i32 {\n"
        "      return a + b\n"
        "  }\n"
        "\n"
        "Check the XS language reference for the correct keyword.\n"
    },

    /* Type / Semantic */
    {
        "T0001",
        "mismatched types",
        "An expression has a type that is incompatible with the expected\n"
        "type in this context. This can occur in variable assignments,\n"
        "function arguments, return values, or match arms.\n"
        "\n"
        "Incorrect:\n"
        "\n"
        "  let x: i32 = \"hello\"\n"
        "\n"
        "The variable `x` is declared as `i32` but assigned a `str` value.\n"
        "\n"
        "Corrected:\n"
        "\n"
        "  let x: i32 = 42\n"
        "  -- or --\n"
        "  let x: str = \"hello\"\n"
        "\n"
        "Ensure the assigned value matches the declared type. Use\n"
        "conversion functions like `int()`, `float()`, or `str()` if needed.\n"
    },
    {
        "T0002",
        "undefined name",
        "A variable, function, or type name was used but has not been\n"
        "defined in the current scope or any enclosing scope.\n"
        "\n"
        "Incorrect:\n"
        "\n"
        "  println(mesage)\n"
        "\n"
        "The name `mesage` is not defined. Perhaps you meant `message`.\n"
        "\n"
        "Corrected:\n"
        "\n"
        "  let message = \"hello\"\n"
        "  println(message)\n"
        "\n"
        "Check for typos in the name. The compiler will suggest similar\n"
        "names from the current scope when available.\n"
    },
    {
        "T0003",
        "unused variable",
        "A variable was declared but never used in the enclosing scope.\n"
        "This is a warning, not an error, but indicates potentially\n"
        "dead code.\n"
        "\n"
        "Warning:\n"
        "\n"
        "  let temp = compute_value()\n"
        "  println(\"done\")\n"
        "\n"
        "The variable `temp` is assigned but never read.\n"
        "\n"
        "Corrected (use the value):\n"
        "\n"
        "  let temp = compute_value()\n"
        "  println(temp)\n"
        "\n"
        "Corrected (prefix with underscore to suppress):\n"
        "\n"
        "  let _temp = compute_value()\n"
    },
    {
        "T0004",
        "mutability violation",
        "An attempt was made to assign to a variable that was declared\n"
        "with `let` (immutable) or `const`. Only `var` bindings can be\n"
        "reassigned.\n"
        "\n"
        "Incorrect:\n"
        "\n"
        "  let count = 0\n"
        "  count = count + 1\n"
        "\n"
        "The variable `count` is immutable because it was declared with `let`.\n"
        "\n"
        "Corrected:\n"
        "\n"
        "  var count = 0\n"
        "  count = count + 1\n"
        "\n"
        "Use `var` instead of `let` when you need to reassign the variable.\n"
        "`const` is even stricter and requires a compile-time constant.\n"
    },
    {
        "T0005",
        "wrong number of arguments",
        "A function was called with the wrong number of arguments.\n"
        "The function signature specifies a different parameter count\n"
        "than what was provided at the call site.\n"
        "\n"
        "Incorrect:\n"
        "\n"
        "  fn add(a: i32, b: i32) -> i32 {\n"
        "      return a + b\n"
        "  }\n"
        "  let result = add(1, 2, 3)\n"
        "\n"
        "`add` expects 2 arguments but 3 were provided.\n"
        "\n"
        "Corrected:\n"
        "\n"
        "  let result = add(1, 2)\n"
        "\n"
        "Check the function signature and provide the correct number\n"
        "of arguments. Default parameters are optional.\n"
    },
    {
        "T0006",
        "not callable",
        "An expression was used in a function call position, but it is\n"
        "not a function, method, or callable value.\n"
        "\n"
        "Incorrect:\n"
        "\n"
        "  let x = 42\n"
        "  let y = x(5)\n"
        "\n"
        "The variable `x` is an integer, not a function.\n"
        "\n"
        "Corrected:\n"
        "\n"
        "  fn x(n: i32) -> i32 { return n * 2 }\n"
        "  let y = x(5)\n"
        "\n"
        "Ensure the callee is a function, lambda, or callable struct.\n"
    },
    {
        "T0007",
        "no such field",
        "An attempt was made to access a field that does not exist on\n"
        "the given struct, class, or module.\n"
        "\n"
        "Incorrect:\n"
        "\n"
        "  struct Point { x: i32, y: i32 }\n"
        "  let p = Point { x: 1, y: 2 }\n"
        "  println(p.z)\n"
        "\n"
        "The struct `Point` has fields `x` and `y`, but not `z`.\n"
        "\n"
        "Corrected:\n"
        "\n"
        "  println(p.x)\n"
        "\n"
        "Check the struct definition for available fields.\n"
    },
    {
        "T0008",
        "no such method",
        "An attempt was made to call a method that does not exist on\n"
        "the given type.\n"
        "\n"
        "Incorrect:\n"
        "\n"
        "  let name = \"hello\"\n"
        "  name.push(\"!\")\n"
        "\n"
        "Strings do not have a `push` method.\n"
        "\n"
        "Corrected:\n"
        "\n"
        "  let name = \"hello\" + \"!\"\n"
        "\n"
        "Use `str` concatenation or the appropriate method for the type.\n"
        "Check the type's documentation for available methods.\n"
    },

    {
        "S0001",
        "non-exhaustive match",
        "A `match` expression does not cover all possible values of the\n"
        "subject. Every match must be exhaustive to prevent runtime errors.\n"
        "\n"
        "Incorrect:\n"
        "\n"
        "  enum Color { Red, Green, Blue }\n"
        "  let c = Color.Red\n"
        "  match c {\n"
        "      Color.Red => println(\"red\")\n"
        "      Color.Green => println(\"green\")\n"
        "  }\n"
        "\n"
        "`Color.Blue` is not handled.\n"
        "\n"
        "Corrected:\n"
        "\n"
        "  match c {\n"
        "      Color.Red => println(\"red\")\n"
        "      Color.Green => println(\"green\")\n"
        "      Color.Blue => println(\"blue\")\n"
        "  }\n"
        "\n"
        "Or add a wildcard: `_ => println(\"other\")`\n"
    },
    {
        "S0002",
        "unreachable pattern",
        "A pattern in a `match` expression can never be reached because\n"
        "a previous pattern already covers all the same cases.\n"
        "\n"
        "Warning:\n"
        "\n"
        "  match x {\n"
        "      _ => println(\"anything\")\n"
        "      42 => println(\"forty-two\")\n"
        "  }\n"
        "\n"
        "The `42` pattern is unreachable because `_` matches everything.\n"
        "\n"
        "Corrected:\n"
        "\n"
        "  match x {\n"
        "      42 => println(\"forty-two\")\n"
        "      _ => println(\"anything\")\n"
        "  }\n"
        "\n"
        "Place specific patterns before wildcard or catch-all patterns.\n"
    },
    {
        "S0003",
        "@pure function side-effect violation",
        "A function marked with @pure attempted to perform a side effect,\n"
        "such as I/O, mutation of external state, or calling an impure\n"
        "function.\n"
        "\n"
        "Incorrect:\n"
        "\n"
        "  @pure\n"
        "  fn add(a: i32, b: i32) -> i32 {\n"
        "      println(\"adding\")  -- side effect!\n"
        "      return a + b\n"
        "  }\n"
        "\n"
        "`println` is an I/O operation and cannot be called from a @pure function.\n"
        "\n"
        "Corrected:\n"
        "\n"
        "  @pure\n"
        "  fn add(a: i32, b: i32) -> i32 {\n"
        "      return a + b\n"
        "  }\n"
        "\n"
        "Remove the side-effecting call or remove the @pure annotation.\n"
    },
    {
        "S0004",
        "duplicate definition",
        "A name was defined more than once in the same scope. Each name\n"
        "must be unique within its scope.\n"
        "\n"
        "Incorrect:\n"
        "\n"
        "  fn greet() { println(\"hello\") }\n"
        "  fn greet() { println(\"hi\") }\n"
        "\n"
        "The function `greet` is defined twice.\n"
        "\n"
        "Corrected:\n"
        "\n"
        "  fn greet() { println(\"hello\") }\n"
        "  fn greet_informal() { println(\"hi\") }\n"
        "\n"
        "Give each definition a unique name, or remove the duplicate.\n"
    },
    {
        "S0005",
        "shadowed variable",
        "A variable declaration shadows a variable with the same name\n"
        "in an enclosing scope. This is a warning because it may indicate\n"
        "an accidental name collision.\n"
        "\n"
        "Warning:\n"
        "\n"
        "  let x = 10\n"
        "  if true {\n"
        "      let x = 20  -- shadows outer x\n"
        "      println(x)  -- prints 20\n"
        "  }\n"
        "  println(x)  -- prints 10\n"
        "\n"
        "Corrected (use a different name):\n"
        "\n"
        "  let x = 10\n"
        "  if true {\n"
        "      let inner_x = 20\n"
        "      println(inner_x)\n"
        "  }\n"
        "  println(x)\n"
        "\n"
        "Intentional shadowing can be signaled with `let x = x + 1`.\n"
    },
    {
        "S0006",
        "orphan impl",
        "An `impl` block references a trait or type that is not defined\n"
        "in the current module. To maintain coherence, impl blocks must\n"
        "be in the same module as the trait or the type.\n"
        "\n"
        "Incorrect:\n"
        "\n"
        "  -- in my_module.xs\n"
        "  impl Display for SomeExternalType {\n"
        "      fn fmt(self) -> str { \"...\" }\n"
        "  }\n"
        "\n"
        "Neither `Display` nor `SomeExternalType` is defined here.\n"
        "\n"
        "Corrected:\n"
        "\n"
        "  -- define the trait or type in this module, or\n"
        "  -- move the impl to the module that owns the type\n"
        "\n"
        "At least one of the trait or the type must be local.\n"
    },

    {NULL, NULL, NULL}
};

int diag_explain(const char *code) {
    if (!code) {
        fprintf(stderr, "usage: xs --explain <code>\n");
        return 1;
    }

    const char *rst = DIAG_COLOR("\033[0m");
    const char *bold = DIAG_COLOR("\033[1m");
    const char *mag = DIAG_COLOR("\033[35m");
    const char *cyan = DIAG_COLOR("\033[36m");
    const char *gray = DIAG_COLOR("\033[90m");

    for (int i = 0; explanations[i].code; i++) {
        if (strcmp(explanations[i].code, code) == 0) {
            const ErrorExplanation *e = &explanations[i];

            fprintf(stdout, "\n%s%s%s: %s%s%s\n\n",
                    mag, e->code, rst,
                    bold, e->title, rst);

            const char *p = e->explanation;
            while (*p) {
                const char *eol = strchr(p, '\n');
                int linelen = eol ? (int)(eol - p) : (int)strlen(p);

                char linebuf[1024];
                if (linelen >= (int)sizeof(linebuf))
                    linelen = (int)sizeof(linebuf) - 1;
                memcpy(linebuf, p, (size_t)linelen);
                linebuf[linelen] = '\0';

                if (linelen >= 2 && linebuf[0] == ' ' && linebuf[1] == ' ' &&
                    linelen > 2 && linebuf[2] != ' ' && linebuf[2] != '\0') {
                    char colored[4096];
                    diag_colorize_line(linebuf, colored, sizeof colored);
                    fprintf(stdout, "%s%s%s\n", cyan, colored, rst);
                } else if (strncmp(linebuf, "Incorrect:", 10) == 0 ||
                           strncmp(linebuf, "Corrected:", 10) == 0 ||
                           strncmp(linebuf, "Warning:", 8) == 0) {
                    fprintf(stdout, "%s%s%s\n", bold, linebuf, rst);
                } else {
                    fprintf(stdout, "%s%s%s\n", gray, linebuf, rst);
                }

                p += linelen;
                if (*p == '\n') p++;
            }

            fprintf(stdout, "\n");
            return 0;
        }
    }

    fprintf(stderr, "unknown error code '%s'\n", code);
    fprintf(stderr, "\nAvailable codes:\n");
    for (int i = 0; explanations[i].code; i++) {
        fprintf(stderr, "  %s%s%s: %s\n",
                mag, explanations[i].code, rst,
                explanations[i].title);
    }
    fprintf(stderr, "\n");

    return 1;
}
