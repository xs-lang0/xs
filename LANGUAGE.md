# XS Language Reference

Complete reference for the XS programming language.

> **Notation:** Examples show `-- output` comments to indicate what a line
> prints or evaluates to. These are line comments, not output syntax.

---

## Comments

```xs
-- Line comment (everything after -- to end of line is ignored)
{- Block comment (nestable: {- inner -} still works) -}
#!/usr/bin/env xs   -- shebang line (silently skipped by the interpreter)
```

---

## Statement Separators

Newlines and semicolons both act as statement separators. Semicolons are optional
and can be used to place multiple statements on a single line.

```xs
-- Newlines separate statements (most common)
let a = 1
let b = 2

-- Semicolons separate statements on the same line
let x = 10; let y = 20; let z = x + y

-- Semicolons work everywhere: inside blocks, after loops, between functions
if true { let p = 1; let q = 2 }
fn add(a, b) { return a + b }; fn mul(a, b) { return a * b }
var i = 0; while i < 3 { i = i + 1 }; assert(i == 3, "done")

-- Extra semicolons are harmless
let val = 42;;;
```

---

## Variables & Constants

```xs
let x = 42          -- immutable binding (cannot be reassigned)
var y = "hello"     -- mutable binding (can be reassigned with =)
const MAX = 100     -- compile-time constant

let x: i32 = 42     -- with type annotation (parsed, not enforced at runtime)
var name: str = "XS"
```

**`let` vs `var`:** `let` creates an immutable binding — attempting to reassign
a `let` variable is a semantic error. Use `var` when you need to reassign.

**`const`:** Like `let` but signals compile-time intent. Semantically identical
to `let` at runtime.

---

## Data Types

| Type | Literal | Example |
|------|---------|---------|
| Integer (i64) | Decimal, hex, binary, octal | `42`, `0xFF`, `0b1010`, `0o77`, `1_000_000` |
| Float (f64) | Decimal, scientific | `3.14`, `1e10`, `2.5e-3` |
| Boolean | `true`, `false` | `true` |
| String | Double/single quoted | `"hello"`, `'world'` |
| Null | `null` | `null` |
| Array | Brackets | `[1, 2, 3]` |
| Tuple | Parentheses | `(1, "a", true)` |
| Map | Hash-braces | `#{"key": "value", "n": 42}` |
| Range | Dots | `0..10`, `1..=5` |
| Function | `fn` keyword | `fn(x) { x + 1 }` |

**Integers** are signed 64-bit (`int64_t`). Underscores in numeric literals are
visual separators and ignored: `1_000_000` is `1000000`.

**Floats** are IEEE 754 double-precision (`double`).

**Null** is a distinct type, not equivalent to `0`, `false`, or `""`.

---

## Strings

```xs
-- Basic
let s = "hello world"

-- Interpolation — expressions inside {braces}
let name = "XS"
println("Hello, {name}!")           -- Hello, XS!
println("{1 + 2} is three")         -- 3 is three

-- Escape sequences
"\n"    -- newline
"\t"    -- tab
"\\"    -- backslash
"\""    -- double quote
"\{"    -- literal brace (suppresses interpolation)
"\0"    -- null byte
"\e"    -- ESC (0x1B)

-- Multiline (triple-quoted, auto-dedented)
let text = """
    line one
    line two
"""

-- Raw strings (no escapes, no interpolation)
let pattern = r"\d+\.\d+"
let raw_multi = r"""
    {not interpolated}
"""

-- Color strings (ANSI terminal colors)
let warning = c"bold;yellow;Warning!"

-- String concatenation operator
"hello" ++ " world"                 -- "hello world"
```

### String Interpolation

Interpolation uses `{expr}` inside double-quoted strings. Any expression is
valid inside the braces. Use `\{` to produce a literal `{`.

```xs
let x = 42
println("x = {x}")          -- x = 42
println("{x * 2} is double") -- 84 is double
println("\{not interpolated}") -- {not interpolated}
```

Interpolation works in regular strings and color strings. It does **not** work
in raw strings (`r"..."`) or single-quoted strings.

### Escape Sequences

| Escape | Character | Code |
|--------|-----------|------|
| `\n` | Newline | 0x0A |
| `\t` | Tab | 0x09 |
| `\r` | Carriage return | 0x0D |
| `\\` | Backslash | 0x5C |
| `\"` | Double quote | 0x22 |
| `\'` | Single quote | 0x27 |
| `\0` | Null byte | 0x00 |
| `\a` | Bell | 0x07 |
| `\b` | Backspace | 0x08 |
| `\f` | Form feed | 0x0C |
| `\v` | Vertical tab | 0x0B |
| `\e` | Escape (ESC) | 0x1B |
| `\{` | Literal `{` (suppresses interpolation) | |

Any other character after `\` is passed through unchanged.

### Color Strings

Color strings embed ANSI escape codes at parse time. The format is
`c"style1;style2;...;text"` — styles come first separated by semicolons,
and the last semicolon-separated segment is the text content.

```xs
let err = c"bold;red;Error!"
let ok  = c"green;Success"
let fancy = c"italic;underline;bright_cyan;styled text"
```

**Available styles:**

| Category | Values |
|----------|--------|
| Attributes | `bold`, `dim`, `italic`, `underline`, `blink`, `reverse`, `hidden`, `strikethrough` |
| Foreground | `black`, `red`, `green`, `yellow`, `blue`, `magenta`, `cyan`, `white` |
| Bright FG | `bright_black`, `bright_red`, `bright_green`, `bright_yellow`, `bright_blue`, `bright_magenta`, `bright_cyan`, `bright_white` |
| Background | `bg_black`, `bg_red`, `bg_green`, `bg_yellow`, `bg_blue`, `bg_magenta`, `bg_cyan`, `bg_white` |
| Bright BG | `bg_bright_black`, `bg_bright_red`, etc. |
| 256-color | `fg256,N`, `bg256,N` (N = 0-255) |
| Truecolor | `rgb,R,G,B`, `bgrgb,R,G,B` |

Color strings support interpolation in the text portion: `c"bold;x = {x}"`.

A reset sequence is automatically appended, so colors don't bleed into
subsequent output.

### String Methods

```xs
-- Case conversion
"hello".upper()                     -- "HELLO"
"HELLO".lower()                     -- "hello"
"hello world".title()               -- "Hello World"
"hello".capitalize()                -- "Hello"
"Hello".swapcase()                  -- "hELLO"

-- Trimming
"  hi  ".trim()                     -- "hi"
"  hi".trim_start()                 -- "hi"
"hi  ".trim_end()                   -- "hi"

-- Searching
"hello world".contains("world")     -- true
"hello".starts_with("hel")          -- true
"hello".ends_with("llo")            -- true
"hello".find("ll")                  -- 2  (index, or -1 if not found)
"hello".rfind("l")                  -- 3  (last occurrence)
"hi hi hi".count("hi")              -- 3

-- Transformations
"a,b,c".split(",")                  -- ["a", "b", "c"]
"hello".replace("l", "r")           -- "herro"
"hello".chars()                     -- ["h", "e", "l", "l", "o"]
"hello".len()                       -- 5
"hello".repeat(3)                   -- "hellohellohello"
"hello".slice(1, 3)                 -- "el"
"hello".reverse()                   -- "olleh"
"ab".bytes()                        -- [97, 98]
"one\ntwo".lines()                  -- ["one", "two"]

-- Padding
"hi".pad_left(5, ".")               -- "...hi"
"hi".pad_right(5, ".")              -- "hi..."
"hi".center(6, ".")                 -- "..hi.."

-- Prefix/suffix removal
"hello".remove_prefix("hel")        -- "lo"
"hello".remove_suffix("llo")        -- "he"

-- Classification (returns true/false)
"42".is_digit()                     -- true
"abc".is_alpha()                    -- true
"abc123".is_alnum()                 -- true
"ABC".is_upper()                    -- true

-- Parsing
"42".parse_int()                    -- 42
"3.14".parse_float()                -- 3.14
"FF".parse_int(16)                  -- 255 (with base)

-- Splitting
"hello".split_at(2)                 -- ("he", "llo")
"hello".char_at(1)                  -- "e"
"long text".truncate(4, "...")      -- "long..."

-- Emptiness
"".is_empty()                       -- true
```

### String Behavior Details

**`.len()` counts bytes** (via C `strlen`), not Unicode codepoints. For pure
ASCII strings the byte count equals the character count.

**String indexing (`s[i]`)** returns a one-byte string. Negative indices count
from the end. Out-of-bounds indices return `null`.

**`.chars()`** splits byte-by-byte, returning an array of one-character strings.

**Method aliases:** Several methods have aliases for convenience:
- `.find()` / `.index_of()`
- `.trim_start()` / `.ltrim()`
- `.trim_end()` / `.rtrim()`
- `.pad_left()` / `.lpad()` / `.pad_start()`
- `.pad_right()` / `.rpad()` / `.pad_end()`
- `.parse_int()` / `.to_int()` / `.as_int()`

---

## Arrays

```xs
var arr = [1, 2, 3, 4, 5]

-- Access
arr[0]                      -- 1
arr[-1]                     -- 5 (negative indexing)

-- Methods (mutating — modify the array in place)
arr.push(6)                 -- append element(s)
arr.pop()                   -- remove and return last
arr.reverse()               -- reverse in-place
arr.sort()                  -- sort in-place
arr.sort(fn(a, b) { a - b }) -- sort with custom comparator

-- Methods (non-mutating — return new values)
arr.len()                   -- length
arr.first()                 -- first element or null
arr.last()                  -- last element or null
arr.is_empty()              -- true if length is 0
arr.contains(3)             -- true
arr.index_of(3)             -- index or -1
arr.join(", ")              -- "1, 2, 3, 4, 5"
arr.slice(1, 3)             -- [2, 3]
arr.reversed()              -- new reversed array
arr.sorted()                -- new sorted array
arr.sort_by(fn(x) { -x })  -- new array sorted by key
arr.flatten()               -- flatten one level
arr.zip([10, 20, 30])       -- [(1, 10), (2, 20), (3, 30)]

-- Higher-order methods
arr.map(fn(x) { return x * 2 })
arr.filter(fn(x) { return x > 2 })
arr.reduce(fn(acc, x) { return acc + x }, 0)   -- reduce(fn, init)
arr.fold(0, fn(acc, x) { return acc + x })     -- fold(init, fn)
arr.find(fn(x) { return x > 3 })
arr.any(fn(x) { return x > 4 })
arr.all(fn(x) { return x > 0 })

-- Aggregates
arr.sum()                   -- sum of numbers
arr.min()                   -- minimum
arr.max()                   -- maximum

-- Repeat syntax
let zeros = [0; 10]         -- [0, 0, 0, 0, 0, 0, 0, 0, 0, 0]

-- Spread
let combined = [...arr, 6, 7, 8]
```

**`reduce` vs `fold`:** Both do the same thing but with different argument
order. `reduce(fn, init)` puts the function first; `fold(init, fn)` puts the
initial value first. Use whichever reads better.

**Mutating vs non-mutating:** `.reverse()` and `.sort()` modify the array
in-place and return `null`. `.reversed()` and `.sorted()` return a new array,
leaving the original unchanged.

---

## Maps

```xs
var m = #{"name": "Alice", "age": 30}

-- Access
m["name"]                   -- "Alice"
m["age"]                    -- 30
m["new_key"] = 42           -- set new key

-- Methods
m.keys()                    -- ["name", "age"]
m.values()                  -- ["Alice", 30]
m.entries()                 -- [("name", "Alice"), ("age", 30)]
m.len()                     -- 2
m.has("name")               -- true
m.get("name", "default")    -- "Alice" (with fallback)
m.set("key", "val")         -- set entry
m.delete("key")             -- remove entry
m.merge(other_map)          -- merge maps
```

**Map literal syntax:** Maps use `#{}` — the `#` prefix distinguishes them
from blocks. Keys must be strings.

---

## Functions

```xs
-- Basic function
fn greet(name) {
    println("Hello, {name}!")
}

-- With return value
fn add(a, b) {
    return a + b
}

-- With type annotations (parsed, not enforced at runtime)
fn factorial(n: i64) -> i64 {
    if n <= 1 { return 1 }
    return n * factorial(n - 1)
}

-- Expression-body shorthand (= instead of block)
fn double(x) = x * 2

-- Lambda / anonymous function
let square = fn(x) { return x * x }
let inc = fn(x) { x + 1 }          -- implicit return (last expression)

-- Closures capture enclosing variables
fn make_counter() {
    var count = 0
    return fn() {
        count = count + 1
        return count
    }
}
var c = make_counter()
println(c())    -- 1
println(c())    -- 2

-- Higher-order functions
fn apply(f, x) { return f(x) }
println(apply(fn(x) { return x * x }, 5))   -- 25

-- fn main() is auto-called if defined
fn main() {
    println("Program starts here")
}

-- Async functions
async fn fetch_data() {
    -- ...
}

-- Generator functions
fn* range_gen(n) {
    var i = 0
    while i < n {
        yield i
        i = i + 1
    }
}
```

### Closure Capture Semantics

Closures capture variables **by reference** through an environment chain.
The parent environment is captured at **function definition time**, so mutations
inside a closure are visible to the outer scope and vice versa.

```xs
fn make_adder(base) {
    var total = base
    let add = fn(n) {
        total = total + n   -- mutates the outer `total`
        return total
    }
    add(10)
    println(total)          -- 20 — mutation visible outside the closure
    return add
}
var a = make_adder(10)
println(a(5))              -- 25
println(a(3))              -- 28 — state persists across calls
```

### Implicit Return

The last expression in a function body is implicitly returned if there is no
explicit `return` statement. This applies to both named functions and lambdas.

```xs
fn add(a, b) { a + b }      -- returns a + b
let sq = fn(x) { x * x }    -- returns x * x
```

**Common misconception:** Implicit return only works for the *last expression*
in a block. If you need early returns or conditional returns, use `return`
explicitly.

---

## Control Flow

### If / Else If / Else

```xs
if x > 0 {
    println("positive")
} else if x < 0 {
    println("negative")
} else {
    println("zero")
}

-- If as expression (returns last value of taken branch)
let sign = if x > 0 { "+" } else { "-" }
```

**Note:** Braces `{}` are always required around the body — there is no
braceless `if`.

### While Loop

```xs
var i = 0
while i < 10 {
    println(i)
    i = i + 1
}
```

### For Loop

```xs
-- Iterate over array
for x in [1, 2, 3] {
    println(x)
}

-- Iterate over range
for i in 0..10 {           -- 0 to 9 (exclusive end)
    println(i)
}
for i in 1..=10 {          -- 1 to 10 (inclusive end)
    println(i)
}

-- Iterate over string characters
for ch in "hello".chars() {
    print(ch)
}

-- Iterate over map keys
for key in config.keys() {
    println("{key}: {config[key]}")
}

-- Iterate over map entries
for entry in config.entries() {
    println(entry)
}
```

**Common misconception:** `for i in 0..10` does **not** include 10. Use
`0..=10` for inclusive ranges. This is consistent with array indexing where
`0..len` covers all valid indices.

### Loop (Infinite)

```xs
var n = 0
loop {
    n = n + 1
    if n >= 10 { break }
}
```

### Break & Continue

```xs
for i in 0..100 {
    if i % 2 == 0 { continue }     -- skip even numbers
    if i > 20 { break }            -- stop at 20
    println(i)
}
```

---

## Pattern Matching

`match` is an expression — it evaluates to the value of the matched arm.

```xs
let result = match value {
    0 => "zero"
    1 => "one"
    n if n > 100 => "big: {n}"     -- guard clause
    _ => "other"                    -- wildcard (matches anything)
}
```

### Pattern Types

```xs
-- Literal patterns
match data {
    42              => "exact int"
    "hello"         => "exact string"
    true            => "boolean"
    null            => "null"
    _               => "wildcard"
}

-- Variable binding (binds matched value to a name)
match data {
    x => "bound to x: {x}"
}

-- Tuple patterns
match point {
    (0, 0) => "origin"
    (x, 0) => "on x-axis at {x}"
    (0, y) => "on y-axis at {y}"
    (x, y) => "({x}, {y})"
}

-- Struct patterns
match shape {
    Circle { radius } => "circle r={radius}"
    Rect { w, h }     => "rect {w}x{h}"
}

-- Enum patterns
match result {
    Ok(val)   => "success: {val}"
    Err(msg)  => "error: {msg}"
}

-- Or patterns (match any of several alternatives)
match ch {
    'a' | 'e' | 'i' | 'o' | 'u' => "vowel"
    _ => "consonant"
}

-- Range patterns
match age {
    0..18   => "minor"
    18..=65 => "adult"
    _       => "senior"
}

-- Slice patterns (destructure arrays)
match arr {
    [first, ..rest] => "head: {first}"
    []              => "empty"
}

-- Capture with @ (bind and match simultaneously)
match value {
    n @ 1..=10 => "small number: {n}"
    n          => "other: {n}"
}
```

**Exhaustiveness:** The semantic analyzer checks that `match` expressions cover
all possible cases. A wildcard `_` or variable pattern makes any match
exhaustive.

---

## Structs

```xs
struct Point {
    x,
    y
}

-- Create instance
var p = Point { x: 10, y: 20 }
println(p.x)       -- 10
println(p.y)       -- 20

-- With type annotations (parsed, not enforced)
struct Config {
    host: str,
    port: i32,
    debug: bool
}

-- Implement methods
impl Point {
    fn distance(self) {
        return math.sqrt(self.x * self.x + self.y * self.y)
    }

    fn translate(self, dx, dy) {
        return Point { x: self.x + dx, y: self.y + dy }
    }
}
```

**`self` parameter:** Methods that access instance fields must take `self` as
the first parameter. `self` is not implicit — omitting it means the method
cannot access instance state.

---

## Enums

```xs
enum Color {
    Red,
    Green,
    Blue
}

-- With associated data
enum Shape {
    Circle(f64),
    Rect(f64, f64),
    Point
}

-- Match on enums
match color {
    Red   => "#FF0000"
    Green => "#00FF00"
    Blue  => "#0000FF"
}
```

---

## Traits

```xs
trait Printable {
    fn to_string(self) -> str
}

impl Printable for Point {
    fn to_string(self) {
        return "({self.x}, {self.y})"
    }
}
```

---

## Error Handling

### `throw` — Catchable Exceptions

`throw expr` raises a catchable exception. The thrown value can be any type
(string, integer, map, array, etc.).

```xs
-- Throw and catch
try {
    throw "something went wrong"
} catch e {
    println("Error: {e}")
}

-- Throw any value
try {
    throw #{"kind": "NotFound", "message": "item missing"}
} catch e {
    println(e)
}

-- Nested try/catch with re-throw
try {
    try {
        throw "inner error"
    } catch e {
        throw "rethrown: {e}"   -- re-throw propagates to outer handler
    }
} catch e {
    println("Outer caught: {e}")
}

-- Throw from functions — propagates up the call stack
fn divide(a, b) {
    if b == 0 { throw "division by zero" }
    return a / b
}

try {
    divide(10, 0)
} catch e {
    println(e)   -- "division by zero"
}

-- Finally — runs regardless of whether an exception was thrown
try {
    -- risky code
} catch e {
    println("error: {e}")
} finally {
    println("cleanup")
}
```

**Common misconception:** `try/catch` used as an expression always evaluates to
`null`. To extract a value from a catch block, assign to a `var` inside the
handler:

```xs
var result = null
try {
    result = risky_operation()
} catch e {
    result = "fallback"
}
```

### `panic(msg)` — Unrecoverable Abort

`panic(msg)` immediately terminates the process with exit code 1. It prints a
message to stderr and is **not catchable** by `try/catch`.

```xs
panic("fatal: out of memory")
-- stderr: xs: panic: fatal: out of memory
-- process exits with code 1
```

### When to Use Each

| Mechanism | Catchable? | Use case |
|-----------|------------|----------|
| `throw expr` | Yes, via `try/catch` | Recoverable errors: bad input, missing keys, validation |
| `panic(msg)` | No (exits immediately) | Unrecoverable errors: invariant violations, impossible states |

### `defer`

`defer` schedules a block to run when the enclosing function exits,
regardless of whether an exception was thrown.

```xs
fn process_file(path) {
    let f = io.read_file(path)
    defer println("done processing")
    -- ... process f ...
}
```

---

## Imports

```xs
import math
import io
import json

-- Access module members with dot notation
println(math.PI)
println(math.sqrt(16))
let data = json.parse(text)
io.write_file("out.txt", data)
```

**Note:** There is no selective import (`from X import Y`) — you always import
the full module and access members via `module.member`.

---

## Modules

```xs
module Utils {
    fn helper() { {- ... -} }
}
```

---

## Type Aliases

```xs
type UserId = i64
type StringMap = Map<str, str>
```

---

## Operators

### Arithmetic

| Operator | Description | Example |
|----------|-------------|---------|
| `+` | Addition | `3 + 4` → `7` |
| `-` | Subtraction | `10 - 3` → `7` |
| `*` | Multiplication | `4 * 5` → `20` |
| `/` | Division (truncates toward zero for integers) | `10 / 3` → `3` |
| `%` | Modulo (sign follows dividend) | `10 % 3` → `1` |
| `**` | Power | `2 ** 10` → `1024` |

### Comparison

| Operator | Description |
|----------|-------------|
| `==` | Equal |
| `!=` | Not equal |
| `<` | Less than |
| `>` | Greater than |
| `<=` | Less or equal |
| `>=` | Greater or equal |
| `<=>` | Spaceship (three-way comparison: returns -1, 0, or 1) |

### Logical

| Operator | Description |
|----------|-------------|
| `and` / `&&` | Logical AND (short-circuit) |
| `or` / `\|\|` | Logical OR (short-circuit) |
| `not` / `!` | Logical NOT |

**Short-circuit:** `and` stops evaluating if the left side is falsy; `or` stops
if the left side is truthy. The result is the last evaluated operand (not
necessarily `true`/`false`).

### Bitwise

| Operator | Description |
|----------|-------------|
| `&` | Bitwise AND |
| `\|` | Bitwise OR |
| `^` | Bitwise XOR |
| `~` | Bitwise NOT |
| `<<` | Left shift |
| `>>` | Right shift |

### String

| Operator | Description | Example |
|----------|-------------|---------|
| `++` | Concatenation | `"hello" ++ " world"` → `"hello world"` |

**Common misconception:** `+` does **not** concatenate strings. Use `++` for
string concatenation. `+` on strings is a type error.

### Other

| Operator | Description | Example |
|----------|-------------|---------|
| `..` | Exclusive range | `0..10` (0 through 9) |
| `..=` | Inclusive range | `1..=5` (1 through 5) |
| `??` | Null coalesce | `x ?? default` (use default if x is null) |
| `\|>` | Pipe | `data \|> process` (pass data as first arg) |
| `as` | Type cast | `x as float` |
| `is` | Type check | `x is int` |
| `in` | Membership | `x in arr` |
| `?.` | Optional chaining | `obj?.field` (null if obj is null) |

### Assignment

| Operator | Description |
|----------|-------------|
| `=` | Assign (only works on `var` bindings) |
| `+=` `-=` `*=` `/=` `%=` | Compound assign |
| `**=` `&=` `\|=` `^=` | Compound assign |
| `<<=` `>>=` | Compound shift assign |

### Precedence (lowest to highest)

1. `=` `+=` `-=` ... (assignment)
2. `|>` (pipe)
3. `??` `..` `..=` (null coalesce, range)
4. `||` `or` (logical or)
5. `&&` `and` (logical and)
6. `==` `!=` (equality)
7. `<` `>` `<=` `>=` `is` `in` (comparison)
8. `|` (bitwise or)
9. `^` (bitwise xor)
10. `&` (bitwise and)
11. `<<` `>>` (shift)
12. `+` `-` `++` (add, concat)
13. `*` `/` `%` (multiply, divide)
14. `**` (power, right-associative)
15. `as` (cast)
16. Unary: `-` `!` `not` `~` (prefix)
17. Postfix: `?` `.` `?.` `[]` `()` (access, call)

---

## Algebraic Effects

```xs
-- Declare an effect
effect Log {
    fn log(msg: str)
}

-- Perform an effect operation
perform Log.log("hello")

-- Handle effects
handle some_computation() {
    Log.log(msg) => {
        println("[LOG] {msg}")
        resume null
    }
}

-- resume returns a value to the perform site
resume value
```

---

## Concurrency

```xs
-- Spawn a task
spawn do_work()

-- Nursery (structured concurrency — waits for all spawned tasks)
nursery {
    spawn task1()
    spawn task2()
}

-- Await
let result = await fetch_data()

-- Yield (generators)
yield value
```

---

## List Comprehensions

```xs
let squares = [x * x for x in 0..10]
let evens = [x for x in 0..20 if x % 2 == 0]
```

**Note:** List comprehensions return a new array. The `if` clause is optional
and filters elements.

---

## Built-in Functions

### I/O

| Function | Description |
|----------|-------------|
| `println(args...)` | Print with newline. Supports `{}` format placeholders. |
| `print(args...)` | Print without trailing newline |
| `eprint(args...)` | Print to stderr without newline |
| `eprintln(args...)` | Print to stderr with newline |
| `input(prompt?)` | Read line from stdin, optionally showing a prompt |
| `clear()` | Clear the terminal screen |

### Type Checking

| Function | Description |
|----------|-------------|
| `type(val)` | Type name as lowercase string (`"int"`, `"str"`, `"array"`, etc.) |
| `typeof(val)` | Alias for `type()` |
| `type_of(val)` | Type name capitalized (`"Int"`, `"Str"`, `"Array"`, etc.) |
| `is_null(val)` | Check if null |
| `is_int(val)` | Check if integer |
| `is_float(val)` | Check if float |
| `is_str(val)` | Check if string |
| `is_bool(val)` | Check if boolean |
| `is_array(val)` | Check if array |
| `is_fn(val)` | Check if function |

### Conversion

| Function | Description |
|----------|-------------|
| `int(val)` / `i64(val)` | Convert to integer (truncates floats toward zero) |
| `float(val)` / `f64(val)` | Convert to float |
| `str(val)` | Convert to string representation |
| `bool(val)` | Convert to boolean |
| `char(val)` | Convert integer to single-character string |
| `chr(n)` | Integer to character (alias for `char`) |
| `ord(ch)` | Character (first byte) to integer |

### Math

| Function | Description |
|----------|-------------|
| `abs(x)` | Absolute value |
| `min(a, b)` | Minimum of two values |
| `max(a, b)` | Maximum of two values |
| `pow(base, exp)` | Power |
| `sqrt(x)` | Square root |
| `floor(x)` | Floor (round toward negative infinity) |
| `ceil(x)` | Ceiling (round toward positive infinity) |
| `round(x)` | Round to nearest integer |
| `log(x)` | Natural logarithm |
| `sin(x)` `cos(x)` `tan(x)` | Trigonometric functions (radians) |

### Collections

| Function | Description |
|----------|-------------|
| `len(val)` | Length of array, string, or map |
| `range(n)` | Create array `[0, 1, ..., n-1]` |
| `array()` | Create empty array |
| `map()` | Create empty map |
| `sorted(arr)` | Return sorted copy of array |
| `sum(arr)` | Sum of numeric array elements |
| `enumerate(arr)` | Array of `(index, value)` tuples |
| `zip(a, b)` | Zip two arrays into array of tuples |
| `flatten(arr)` | Flatten nested arrays one level |
| `contains(coll, val)` | Membership test |
| `keys(map)` | Map keys as array |
| `values(map)` | Map values as array |
| `entries(map)` | Map entries as array of `[key, value]` |
| `chars(str)` | String to array of characters |
| `bytes(str)` | String to array of byte values |

### Debugging & Testing

| Function | Description |
|----------|-------------|
| `assert(cond, msg?)` | Assert condition is truthy. Optional message on failure. Panics (not catchable). |
| `assert_eq(a, b)` | Assert `a == b`. Panics with both values shown on failure. |
| `dbg(val)` | Debug-print value to stderr with type info, returns val |
| `pprint(val)` | Pretty-print value with indentation |
| `repr(val)` | Return debug string representation |
| `panic(msg)` | Print message to stderr and exit with code 1 (not catchable) |
| `todo(msg?)` | Mark code as unimplemented; panics with message |
| `unreachable()` | Mark code as unreachable; panics if reached |
| `exit(code)` | Exit process with given exit code |

### Copying

| Function | Description |
|----------|-------------|
| `copy(val)` / `clone(val)` | Deep copy a value |

### Result/Option Constructors

| Function | Description |
|----------|-------------|
| `Ok(val)` | Create an Ok result |
| `Err(val)` | Create an Err result |
| `Some(val)` | Create a Some option |
| `None()` | Create a None value |

### String Formatting

| Function | Description |
|----------|-------------|
| `format(fmt, args...)` | Format string with `{}` placeholders |
| `sprintf(fmt, args...)` | Alias for `format` |

### Constants

| Name | Value |
|------|-------|
| `PI` | 3.14159265358979... |
| `E` | 2.71828182845904... |
| `INF` | Infinity |
| `NAN` | Not a Number |

### Globals

| Name | Type | Description |
|------|------|-------------|
| `argv` | `array` of strings | Command-line arguments passed after the script name. Does **not** include the interpreter or script path. Empty array if no arguments. |

```xs
-- Run: xs script.xs hello world
println(argv)          -- ["hello", "world"]
println(len(argv))     -- 2
println(argv[0])       -- "hello"

-- Run: xs script.xs
println(argv)          -- []
```

---

## Standard Library Modules

Import with `import <module>` and access members via `module.member()`.

### `math`

**Constants:** `PI`, `E`, `TAU`, `INF`, `NAN`

**Functions:** `sqrt`, `cbrt`, `pow`, `abs`, `floor`, `ceil`, `round`, `trunc`,
`sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sinh`, `cosh`, `tanh`,
`exp`, `log`, `log2`, `log10`, `hypot`, `min`, `max`, `clamp`, `lerp`, `sign`,
`gcd`, `lcm`, `factorial`, `degrees`, `radians`, `isnan`, `isinf`

### `time`

`now()`, `clock()`, `sleep(secs)`, `sleep_ms(ms)`, `millis()`, `time_since(t)`,
`format(t, fmt)`, `parse(s, fmt)`, `year(t)`, `month(t)`, `day(t)`, `hour(t)`,
`minute(t)`, `second(t)`, `stopwatch()`

### `io`

`read_file(path)`, `write_file(path, data)`, `append_file(path, data)`,
`read_lines(path)`, `read_json(path)`, `write_json(path, data)`,
`exists(path)`, `size(path)`, `delete_file(path)`, `copy_file(src, dst)`,
`rename_file(old, new)`, `make_dir(path)`, `list_dir(path)`, `is_file(path)`,
`is_dir(path)`, `read_line()`, `wait_for_key()`

### `os`

`platform`, `sep`, `pid()`, `args()`, `cwd()`, `chdir(path)`, `home()`,
`cpu_count()`, `exit(code)`, `env.get(key)`, `env.set(key, val)`,
`env.has(key)`, `env.all()`

### `json`

`parse(str)`, `stringify(val)`, `pretty(val)`, `valid(str)`

### `string`

`pad_left(s, n, ch)`, `pad_right(s, n, ch)`, `center(s, n, ch)`,
`truncate(s, n)`, `escape_html(s)`, `words(s)`, `levenshtein(a, b)`,
`is_numeric(s)`, `camel_to_snake(s)`, `snake_to_camel(s)`

### `path`

`join(parts...)`, `basename(p)`, `dirname(p)`, `ext(p)`, `stem(p)`, `sep`

### `collections`

`Counter(arr)`, `Stack()`, `PriorityQueue()`, `Deque()`, `Set(arr)`,
`OrderedMap()`

### `random`

`int(min, max)`, `float()`, `bool()`, `choice(arr)`, `shuffle(arr)`,
`sample(arr, n)`, `seed(n)`

### `hash`

`md5(data)`, `sha1(data)`, `sha256(data)`, `sha512(data)`, `hmac(key, data)`

### `crypto`

`sha256(data)`, `md5(data)`, `random_bytes(n)`, `random_int(min, max)`,
`uuid4()`

### `encode`

`base64_encode(data)`, `base64_decode(data)`, `hex_encode(data)`,
`hex_decode(data)`, `url_encode(s)`, `url_decode(s)`

### `re`

`match(pattern, str)`, `find(pattern, str)`, `replace(pattern, str, repl)`,
`split(pattern, str)`

### `csv`

`parse(str)`, `stringify(rows)`

### `url`

`parse(str)`, `encode(s)`, `decode(s)`

### `reflect`

`type_of(val)`, `fields(val)`, `methods(val)`, `is_instance(val, type)`

### `fmt`

`number(n, decimals)`, `hex(n)`, `bin(n)`, `pad(s, n)`, `comma(n)`,
`filesize(n)`, `ordinal(n)`, `pluralize(word, n)`

### `log`

`debug(msg)`, `info(msg)`, `warn(msg)`, `error(msg)`, `set_level(level)`

### `test`

`assert(cond)`, `assert_eq(a, b)`, `assert_ne(a, b)`, `run(name, fn)`,
`summary()`

### `net`

`tcp_connect(host, port)`, `tcp_listen(port)`, `resolve(host)`

### `async`

`spawn(fn)`, `sleep(secs)`, `channel()`, `select(channels)`, `all(tasks)`,
`race(tasks)`, `resolve(val)`, `reject(err)`

### `process`

`run(cmd)`, `spawn(cmd)`

### `thread`

`spawn(fn)`, `id()`, `cpu_count()`, `sleep(secs)`

### `buf`

`new(cap)`, `write_u8(v)`, `read_u8()`, `to_str()`, `to_hex()`, `len()`

### `db`

`open(path)`, `exec(sql)`, `query(sql)`, `close()`

### `gc`

`collect()`, `disable()`, `enable()`, `stats()`

### `reactive`

`signal(val)`, `derived(fn)`, `effect(fn)`, `batch(fn)`

### `cli`

Command-line argument parsing utilities.

### `ffi`

Foreign function interface for calling native code.

---

## Type Annotations

Type annotations are parsed but **not enforced at runtime**. The semantic
analyzer checks them in `--check` mode.

```xs
-- Parameter types and return type
fn add(a: i32, b: i32) -> i32 { return a + b }

-- Variable types
let x: i64 = 42
var name: str = "hello"

-- Generic type parameters
fn sort<T>(arr: [T]) -> [T] where T: Ord { {- ... -} }

-- Array types
let nums: [i32] = [1, 2, 3]

-- Optional types
let maybe: i32? = null

-- Function types
let f: fn(i32) -> i32 = fn(x) { return x + 1 }
```

**Common misconception:** Type annotations do not cause runtime type errors.
`let x: i32 = "hello"` will parse and run without error at runtime. Use
`xs --check` to get static type errors.

---

## Execution Backends

```bash
xs script.xs              # tree-walker interpreter (default)
xs --vm script.xs         # bytecode VM (faster)
xs --backend vm script.xs # same as --vm
```

The bytecode VM compiles the AST to bytecode before execution and is
significantly faster for compute-heavy code (loops, recursion). Both backends
produce identical results for correct programs.

---

## Numeric Behavior

### Integer Division (`/`) — Truncation Toward Zero

Integer division truncates toward zero (C semantics), **not** toward negative
infinity (floor division). This matters for negative operands:

```xs
 7  /  3   --  2
(-7) /  3  -- -2   (truncates toward zero, NOT -3)
 7  / (-3) -- -2
(-7) / (-3) --  2
```

### Floor Division (`//`)

Floors the result on division.
```xs
5 // 2 -- 2
```

### Modulo (`%`) — Sign Matches Dividend

The `%` operator uses C remainder semantics: the result has the same sign as the
**dividend** (left operand).

```xs
 7  %  3   --  1
(-7) %  3  -- -1   (sign follows -7)
 7  % (-3) --  1   (sign follows  7)
```

### Float-to-Integer Conversion — Truncation Toward Zero

`int(x)` casts a float to `i64` by truncating toward zero (the fractional part
is discarded, not rounded):

```xs
int(3.9)   --  3
int(-3.9)  -- -3   (toward zero, not -4)
int(3.1)   --  3
int(-3.1)  -- -3
```

### Integer Overflow — Two's Complement Wrap

Integers are signed 64-bit (`int64_t`). Overflow wraps silently via two's
complement, matching C behavior:

```xs
let max = 9223372036854775807   -- 2^63 - 1
let wrapped = max + 1           -- -9223372036854775808
```

There is no overflow detection or big-integer promotion.
