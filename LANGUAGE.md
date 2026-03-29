# XS Language Reference

Complete reference for the XS programming language. Covers syntax, semantics, the standard library, and the weird corners. If something here doesn't match what the interpreter does, the interpreter wins.

> **Notation:** Examples use `-- output` comments to show what a line prints.

---

## Comments

```xs
-- line comment (everything after -- is ignored)

{- block comment (nestable: {- inner -} works fine) -}

#!/usr/bin/env xs   -- shebang line, silently ignored by the interpreter
```

Block comments nest properly, so you can comment out code that already has block comments in it.

---

## Statement Separators

Newlines and semicolons both separate statements.

```xs
let a = 1
let b = 2

let x = 10; let y = 20; let z = x + y

-- semicolons inside blocks
if true { let p = 1; let q = 2 }

-- extra semicolons are fine
let val = 42;;;
```

---

## Variables

```xs
let x = 42          -- immutable binding
var y = "hello"     -- mutable binding (can reassign)
const MAX = 100     -- constant (same as let at runtime, signals intent)

-- with type annotations
let count: int = 42
var name: str = "XS"
```

`let` bindings cannot be reassigned: that's a runtime error. `var` bindings can be reassigned with `=`.

`const` is identical to `let` at runtime.

### Reactive Bindings

`bind` creates a variable that automatically recomputes when its dependencies change.

```xs
var price = 10
var qty = 3
bind total = price * qty
println(total)                   -- 30

price = 20
println(total)                   -- 60 (auto-updated)

qty = 5
println(total)                   -- 100

-- bindings can depend on other bindings (cascading)
bind doubled = total * 2
println(doubled)                 -- 200

price = 1
println(total)                   -- 5
println(doubled)                 -- 10

-- works with strings too
var name = "world"
bind greeting = "hello " ++ name
println(greeting)                -- hello world
name = "xs"
println(greeting)                -- hello xs
```

`bind` tracks which variables are read when the expression is first evaluated. When any of those variables are reassigned, the binding automatically recomputes. Cascading works: if binding A depends on binding B, and B's dependency changes, both B and A update in order.

In the VM and transpilers, `bind` acts as a regular `let` (no reactivity). Reactive updates only happen in the interpreter.

### Contracts (where clauses)

Add `where` after a type annotation to enforce a condition on the value. The condition is checked at runtime.

```xs
let age: int where age > 0 and age < 150 = 25
let name: str where name.len > 0 = "xs"
let score: int where score >= 0 and score <= 100 = 85
```

If the condition fails, a catchable error is thrown:

```xs
try {
    let bad: int where bad > 100 = 5
} catch e {
    println(e)                   -- contract violation
}
```

Contracts work on function parameters too:

```xs
fn divide(a: int, b: int where b != 0) {
    return a / b
}

println(divide(10, 2))           -- 5
divide(10, 0)                    -- throws: contract violation
```

Contracts are gradual: no `where` clause means no checking. Add them where you want enforcement. They're checked at runtime in the interpreter. In the VM and transpilers, contracts on function params are not yet enforced (variables are).

### Destructuring

```xs
-- array destructuring
let [a, b, c] = [1, 2, 3]
println(a)               -- 1

-- tuple destructuring
let (x, y) = (10, 20)
println(x)               -- 10

-- nested tuple destructuring
let (a, (b, c)) = (1, (2, 3))
println(c)               -- 3

-- struct destructuring
struct Point { x, y }
let Point { x: px, y: py } = Point { x: 100, y: 200 }
println(px)              -- 100
```

Array destructuring requires an exact length match.

---

## Data Types

| Type | Literal | Example |
|------|---------|---------|
| Integer (i64) | decimal, hex, binary, octal | `42`, `0xFF`, `0b1010`, `0o77` |
| Float (f64) | decimal, scientific | `3.14`, `1e10`, `2.5e-3` |
| Boolean | `true`, `false` | `true` |
| String | double/single quoted | `"hello"`, `'world'` |
| Null | `null` | `null` |
| Array | brackets | `[1, 2, 3]` |
| Tuple | parentheses | `(1, "a", true)` |
| Map | hash-braces | `#{"key": "value"}` |
| Range | dots | `0..10`, `1..=5` |
| Regex | forward slashes | `/[0-9]+/` |
| Function | `fn` keyword | `fn(x) { x + 1 }` |

```xs
println(type(42))        -- int
println(type(3.14))      -- float
println(type("hi"))      -- str
println(type(true))      -- bool
println(type(null))      -- null
println(type([]))        -- array
println(type(#{}))       -- map
println(type((1, 2)))    -- tuple
println(type(0..5))      -- range
println(type(/abc/))     -- re
```

---

## Numeric Literals

```xs
42                  -- decimal integer
0xFF                -- hexadecimal (255)
0b1010              -- binary (10)
0o17                -- octal (15)
1_000_000           -- underscores as separators (1000000)

3.14                -- float
1e3                 -- scientific notation (1000.0)
2.5e-3              -- 0.0025
```

Integers are signed 64-bit (`int64_t`). On overflow, they automatically promote to arbitrary-precision bigints:

```xs
let max = 9223372036854775807   -- 2^63 - 1
println(max + 1)                -- 9223372036854775808 (bigint)
println(2 ** 100)               -- 1267650600228229401496703205376
```

Floats are IEEE 754 double-precision. Bigints support all arithmetic operators and mix freely with regular ints.

---

## Strings

Both single and double quotes create strings. They're identical: both support interpolation and escape sequences.

```xs
let s = "hello world"
let s2 = 'also a string'
```

### Interpolation

Expressions inside `{braces}` are evaluated and embedded.

```xs
let name = "XS"
println("Hello, {name}!")         -- Hello, XS!
println("{1 + 2} is three")       -- 3 is three
println("{name} has {len(name)} chars")  -- XS has 2 chars

-- escape the brace to get a literal {
println("\{not interpolated}")    -- {not interpolated}
```

### Escape Sequences

| Escape | Character |
|--------|-----------|
| `\n` | Newline |
| `\t` | Tab |
| `\r` | Carriage return |
| `\\` | Backslash |
| `\"` | Double quote |
| `\'` | Single quote |
| `\0` | Null byte |
| `\a` | Bell |
| `\b` | Backspace |
| `\f` | Form feed |
| `\v` | Vertical tab |
| `\e` | ESC (0x1B) |
| `\{` | Literal `{` (suppresses interpolation) |

Any other `\x` passes through unchanged.

### Triple-Quoted Strings

Multi-line strings with automatic indentation handling.

```xs
let text = """
    line one
    line two
"""
println(text.contains("\n"))     -- true
```

Triple-quoted strings still support interpolation. Use `r"""..."""` for raw triple-quoted.

### Raw Strings

No escape processing, no interpolation.

```xs
let pattern = r"\d+\.\d+"
println(pattern)                 -- \d+\.\d+

let x = 42
let raw = r"no {x} here \n raw"
println(raw)                     -- no {x} here \n raw
```

### Color Strings

Embed ANSI terminal colors at parse time. Format: `c"style;style;...;text"`: the last segment is the text, everything before it is styling.

```xs
let err = c"bold;red;Error!"
let ok  = c"green;Success"
println(err)                     -- prints "Error!" in bold red
```

A reset sequence is appended automatically.

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

Color strings support interpolation in the text part: `c"bold;x = {x}"`.

### String Concatenation

```xs
"hello" ++ " world"              -- "hello world"
"a" ++ "b" ++ "c"               -- "abc"
```

`+` does **not** concatenate strings. Use `++`.

---

## String Methods

```xs
-- case conversion
"hello".upper()                  -- "HELLO"
"HELLO".lower()                  -- "hello"
"hello world".title()            -- "Hello World"
"hello".capitalize()             -- "Hello"

-- trimming
"  hi  ".trim()                  -- "hi"
"  hi".trim_start()              -- "hi"
"hi  ".trim_end()                -- "hi"

-- searching
"hello world".contains("world")  -- true
"hello".starts_with("hel")       -- true
"hello".ends_with("llo")         -- true
"hello".find("ll")               -- 2  (index, or -1)
"hello".rfind("l")               -- 3  (last occurrence)
"hi hi hi".count("hi")           -- 3
"hello".index_of("ll")           -- 2  (alias for find)

-- transformations
"a,b,c".split(",")               -- ["a", "b", "c"]
"hello".replace("l", "r")        -- "herro"
"hello".chars()                  -- ["h", "e", "l", "l", "o"]
"hello".len()                    -- 5
"ha".repeat(3)                   -- "hahaha"
"hello".slice(1, 3)              -- "el"
"hello".reverse()                -- "olleh"
"ab".bytes()                     -- [97, 98]
"one\ntwo".lines()               -- ["one", "two"]

-- padding
"hi".pad_left(5, ".")            -- "...hi"
"hi".pad_right(5, ".")           -- "hi..."
"hi".center(6, ".")              -- "..hi.."

-- prefix/suffix removal
"hello".remove_prefix("hel")     -- "lo"
"hello".remove_suffix("llo")     -- "he"

-- classification
"42".is_digit()                  -- true
"abc".is_alpha()                 -- true
"abc123".is_alnum()              -- true
"ABC".is_upper()                 -- true
"abc".is_lower()                 -- true
"".is_empty()                    -- true

-- parsing
"42".parse_int()                 -- 42
"3.14".parse_float()             -- 3.14
"FF".parse_int(16)               -- 255

-- splitting
"hello".split_at(2)              -- ("he", "llo")
"hello".char_at(1)               -- "e"

-- truncation (total length including suffix)
"long text".truncate(7, "...")   -- "long..."
"long text".truncate(4)          -- "l..."

-- joining (called on the separator)
",".join(["a", "b", "c"])        -- "a,b,c"
```

**`.len()` counts bytes**, not Unicode codepoints. For ASCII strings, byte count equals character count.

**String indexing (`s[i]`)** returns a one-byte string. Negative indices count from the end. Out-of-bounds returns `null`.

**Method aliases:**
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

-- access
arr[0]                           -- 1
arr[-1]                          -- 5 (negative indexing)

-- mutating methods (modify in-place, return null)
arr.push(6)                      -- append element
arr.pop()                        -- remove and return last element
arr.reverse()                    -- reverse in-place
arr.sort()                       -- sort in-place (ascending)
arr.sort(fn(a, b) { a - b })    -- sort with comparator

-- non-mutating methods (return new values)
arr.len()                        -- length
arr.first()                      -- first element or null
arr.last()                       -- last element or null
arr.is_empty()                   -- true if length is 0
arr.contains(3)                  -- true
arr.index_of(3)                  -- index or -1
arr.join(", ")                   -- "1, 2, 3, 4, 5"
arr.slice(1, 3)                  -- [2, 3]
arr.reversed()                   -- new reversed array
arr.sorted()                     -- new sorted array
arr.sort_by(fn(x) { -x })       -- new array sorted by key function
arr.flatten()                    -- flatten one level
arr.zip([10, 20, 30])            -- [(1, 10), (2, 20), (3, 30)]
arr.enumerate()                  -- [(0, 1), (1, 2), (2, 3), ...]

-- higher-order methods
arr.map(fn(x) { x * 2 })
arr.filter(fn(x) { x > 2 })
arr.reduce(fn(acc, x) { acc + x }, 0)   -- reduce(fn, init)
arr.fold(0, fn(acc, x) { acc + x })     -- fold(init, fn)
arr.find(fn(x) { x > 3 })       -- first match or null
arr.any(fn(x) { x > 4 })        -- true if any element matches
arr.all(fn(x) { x > 0 })        -- true if all elements match

-- aggregates
arr.sum()                        -- sum of numbers
arr.min()                        -- minimum value
arr.max()                        -- maximum value

-- repeat syntax
let zeros = [0; 10]              -- [0, 0, 0, 0, 0, 0, 0, 0, 0, 0]

-- spread
let combined = [...arr, 6, 7, 8]
```

**`reduce` vs `fold`:** Same operation, different argument order. `reduce(fn, init)` puts the function first; `fold(init, fn)` puts the initial value first.

**Mutating vs non-mutating:** `.reverse()` and `.sort()` modify in-place and return `null`. `.reversed()` and `.sorted()` return a new array.

---

## Tuples

```xs
let t = (1, "hello", true)
t.0                              -- 1
t.1                              -- "hello"
t.2                              -- true
len(t)                           -- 3
```

Tuples are immutable fixed-size sequences. Access elements with `.0`, `.1`, etc.

---

## Maps

```xs
var m = #{"name": "Alice", "age": 30}

-- access
m["name"]                        -- "Alice"
m["new_key"] = 42                -- set new key

-- methods
m.keys()                         -- ["name", "age"]
m.values()                       -- ["Alice", 30]
m.entries()                      -- [("name", "Alice"), ("age", 30)]
m.len()                          -- 2
m.has("name")                    -- true
m.get("name", "default")         -- "Alice" (with fallback)
m.set("key", "val")              -- set entry, returns null
m.delete("key")                  -- remove entry
m.merge(other_map)               -- merge (right side wins on key conflict)

-- spread
let m2 = #{...m, "extra": true}
```

Map literals use `#{}`: the `#` distinguishes them from blocks. Keys can be strings or integers.

---

## Ranges

```xs
0..10                            -- exclusive range (0 through 9)
1..=5                            -- inclusive range (1 through 5)

let r = 0..5
println(type(r))                 -- range
println(len(r))                  -- 5

-- iteration
for i in 0..5 { print("{i} ") }  -- 0 1 2 3 4
for i in 1..=3 { print("{i} ") } -- 1 2 3

-- membership test
println(3 in 1..5)               -- true
```

`0..10` does **not** include 10. Use `0..=10` for inclusive.

---

## Regex

Regex is a first-class type (`re`) with literal syntax using forward slashes.

```xs
let pat = /[0-9]+/
println(type(pat))               -- re

-- with type annotation
let digits: re = /[0-9]+/
```

Uses POSIX extended regex syntax. Use character classes like `[0-9]`, `[a-z]`, `[[:digit:]]` instead of shorthand like `\d`.

### Regex Methods

```xs
let pat = /[0-9]+/

pat.test("abc123")               -- true (matches anywhere in string)
pat.match("abc123")              -- "123" (first match, or null)
pat.replace("abc123def", "NUM")  -- "abcNUMdef"
pat.source()                     -- "[0-9]+"
```

### Regex in Pattern Matching

Regex literals work as match patterns, testing the value against the pattern:

```xs
fn classify(s) {
    match s {
        /^[0-9]+$/ => "number"
        /^[a-z]+$/ => "lowercase"
        /^[A-Z]+$/ => "uppercase"
        _ => "mixed"
    }
}
```

---

## Operators

### Arithmetic

| Operator | Description | Example |
|----------|-------------|---------|
| `+` | Addition | `3 + 4` → `7` |
| `-` | Subtraction / unary negation | `10 - 3` → `7` |
| `*` | Multiplication | `4 * 5` → `20` |
| `/` | Division (truncates toward zero for ints) | `10 / 3` → `3` |
| `%` | Modulo (sign follows dividend) | `10 % 3` → `1` |
| `**` | Power | `2 ** 10` → `1024` |
| `//` | Floor division (rounds toward negative infinity) | `-7 // 2` → `-4` |

### Comparison

| Operator | Description |
|----------|-------------|
| `==` | Equal |
| `!=` | Not equal |
| `<` | Less than |
| `>` | Greater than |
| `<=` | Less or equal |
| `>=` | Greater or equal |
| `<=>` | Spaceship (three-way: returns -1, 0, or 1) |

```xs
println(5 <=> 3)                 -- 1
println(3 <=> 5)                 -- -1
println(5 <=> 5)                 -- 0
```

### Logical

| Operator | Description |
|----------|-------------|
| `and` / `&&` | Logical AND (short-circuit) |
| `or` / `\|\|` | Logical OR (short-circuit) |
| `not` / `!` | Logical NOT |

Short-circuit: `and` stops if the left side is falsy; `or` stops if the left side is truthy. The result is the last evaluated operand, not necessarily `true`/`false`.

### Bitwise

| Operator | Description | Example |
|----------|-------------|---------|
| `&` | Bitwise AND | `0xFF & 0x0F` → `15` |
| `\|` | Bitwise OR | `0x0F \| 0xF0` → `255` |
| `^` | Bitwise XOR | `0x0F ^ 0xFF` → `240` |
| `~` | Bitwise NOT | `~0xFF` → `-256` |
| `<<` | Left shift | `1 << 3` → `8` |
| `>>` | Right shift | `8 >> 2` → `2` |

### String

| Operator | Description |
|----------|-------------|
| `++` | Concatenation |

### Null Coalesce

```xs
let val = null ?? 42             -- 42
let val2 = 10 ?? 99             -- 10
```

Returns the left side if it's not null, otherwise the right side.

### Pipe

```xs
fn double(x) { x * 2 }
let r = 5 |> double              -- 10
let n = [1, 2, 3] |> len        -- 3

-- chain multiple
let result = 5 |> double |> double  -- 20
```

`x |> f` passes `x` as the first argument to `f`.

### Membership

```xs
println(2 in [1, 2, 3])         -- true
println("a" in #{"a": 1})       -- true (checks keys)
println("ell" in "hello")       -- true (substring check)
println(3 in 1..5)              -- true

println(5 not in [1, 2, 3])     -- true
```

### Type Operators

```xs
-- is: runtime type check
println(42 is int)               -- true
println("hi" is str)             -- true
println(3.14 is float)           -- true
println(42 is float)             -- false

-- as: type cast
println(42 as float)             -- 42.0
println(42 as str)               -- "42"
println("42" as int)             -- 42
```

### Optional Chaining

```xs
let obj = #{"a": #{"b": 42}}
println(obj?.a?.b)               -- 42
println(obj?.x?.y)               -- null (no error)

let x = null
println(x?.foo)                  -- null
```

### Compound Assignment

All of these require a `var` binding on the left side.

```xs
var x = 10
x += 5                           -- 15
x -= 3                           -- 12
x *= 2                           -- 24
x /= 4                           -- 6
x %= 4                           -- 2
```

Also available: `&=`, `|=`, `^=`, `<<=`, `>>=`.

### Operator Precedence (lowest to highest)

1. `=` `+=` `-=` `*=` `/=` `%=` etc. (assignment)
2. `|>` (pipe)
3. `??` `..` `..=` (null coalesce, range)
4. `||` `or` (logical or)
5. `&&` `and` (logical and)
6. `==` `!=` (equality)
7. `<` `>` `<=` `>=` `is` `in` `not in` (comparison, membership)
8. `|` (bitwise or)
9. `^` (bitwise xor)
10. `&` (bitwise and)
11. `<<` `>>` (bit shift)
12. `+` `-` `++` (add, subtract, string concat)
13. `*` `/` `%` `//` (multiply, divide, modulo, floor div)
14. `**` (power, right-associative)
15. `as` (cast)
16. Unary: `-` `!` `not` `~` (prefix)
17. Postfix: `?.` `.` `[]` `()` (access, call)

---

## Numeric Behavior

### Integer Division: Truncation Toward Zero

```xs
println(7 / 3)                   -- 2
println((-7) / 3)                -- -2  (toward zero, NOT -3)
println(7 / (-3))                -- -2
```

### Floor Division: Toward Negative Infinity

```xs
println(5 // 2)                  -- 2
println(-7 // 2)                 -- -4
println(7 // -2)                 -- -4
```

### Modulo: Sign Follows Dividend

```xs
println(7 % 3)                   -- 1
println((-7) % 3)                -- -1  (sign follows -7)
println(7 % (-3))                -- 1   (sign follows 7)
```

### Division by Zero

Division by zero doesn't crash: it prints a runtime warning to stderr and returns `null`:

```xs
let d = 10 / 0                   -- prints warning, d is null
let m = 10 % 0                   -- prints warning, m is null
```

### Float-to-Integer Conversion

`int(x)` truncates toward zero (not rounding):

```xs
println(int(3.9))                -- 3
println(int(-3.9))               -- -3
```

### Integer Overflow

Automatically promotes to arbitrary-precision bigint:

```xs
let max = 9223372036854775807    -- 2^63 - 1
println(max + 1)                 -- 9223372036854775808 (bigint)
println(2 ** 100)                -- 1267650600228229401496703205376
```

---

## Control Flow

### If / Elif / Else

```xs
if x > 0 {
    println("positive")
} elif x < 0 {
    println("negative")
} else {
    println("zero")
}
```

Braces are always required.

`if` works as an expression: it returns the value of the taken branch:

```xs
let sign = if x > 0 { "+" } else { "-" }
```

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
-- over array
for x in [1, 2, 3] {
    println(x)
}

-- over range (exclusive)
for i in 0..5 {
    println(i)                   -- 0, 1, 2, 3, 4
}

-- over range (inclusive)
for i in 1..=3 {
    println(i)                   -- 1, 2, 3
}

-- over string characters
for ch in "hello".chars() {
    print(ch)
}

-- over map (iterates keys)
let m = #{"a": 1, "b": 2}
for k in m {
    println("{k}: {m[k]}")
}

-- over map key-value pairs (direct destructuring)
for (k, v) in m {
    println("{k} = {v}")
}

-- .entries() also works
for (k, v) in m.entries() {
    println("{k} = {v}")
}
```

### Loop (Infinite)

```xs
var n = 0
loop {
    n = n + 1
    if n >= 10 { break }
}
```

### Break and Continue

```xs
for i in 0..100 {
    if i % 2 == 0 { continue }  -- skip even numbers
    if i > 20 { break }         -- stop at 20
    println(i)
}
```

### Break with Value

`loop` can return a value via `break`:

```xs
let result = loop {
    break 42
}
println(result)                  -- 42
```

### Labeled Loops

Break or continue an outer loop from an inner one.

```xs
outer: for i in range(5) {
    for j in range(5) {
        if i * j == 6 {
            break outer          -- breaks the outer loop
        }
    }
}

outer2: for i in range(3) {
    for j in range(3) {
        if j == 1 { continue outer2 }  -- skips to next i
    }
}
```

---

## Pattern Matching

`match` is an expression: it returns the value of the matched arm.

```xs
let result = match value {
    0 => "zero"
    1 => "one"
    n if n > 100 => "big: {n}"  -- guard clause
    _ => "other"                 -- wildcard
}
```

### Pattern Types

```xs
-- literals
match data {
    42       => "exact int"
    "hello"  => "exact string"
    true     => "boolean"
    null     => "null"
    _        => "wildcard"
}

-- variable binding (captures the value)
match data {
    x => "bound: {x}"
}

-- tuple destructuring
match point {
    (0, 0) => "origin"
    (x, 0) => "on x-axis at {x}"
    (0, y) => "on y-axis at {y}"
    (x, y) => "({x}, {y})"
}

-- struct destructuring
match shape {
    Circle { radius } => "circle r={radius}"
    Rect { w, h }     => "rect {w}x{h}"
}

-- enum destructuring
match result {
    Ok(val)   => "success: {val}"
    Err(msg)  => "error: {msg}"
}

-- or patterns
match ch {
    "a" | "e" | "i" | "o" | "u" => "vowel"
    _ => "consonant"
}

-- range patterns
match age {
    0..18   => "minor"
    18..=65 => "adult"
    _       => "senior"
}

-- slice patterns (arrays)
match arr {
    [first, ..rest] => "head: {first}, rest: {rest}"
    []              => "empty"
}

-- @ capture (bind and test simultaneously)
match value {
    n @ 1..=10 => "small: {n}"
    n          => "other: {n}"
}

-- regex patterns
match input {
    /^[0-9]+$/ => "number"
    /^[a-z]+$/ => "word"
    _          => "other"
}

-- string prefix patterns
match url {
    "https://" ++ rest => "secure: {rest}"
    "http://" ++ rest  => "insecure: {rest}"
    _                  => "unknown protocol"
}
```

The semantic analyzer checks that `match` covers all cases. A wildcard `_` or variable pattern makes a match exhaustive. For enum matches, the checker verifies all variants are covered.

---

## Functions

```xs
-- basic function
fn greet(name) {
    println("Hello, {name}!")
}

-- with return value
fn add(a, b) {
    return a + b
}

-- expression-body shorthand
fn double(x) = x * 2

-- implicit return (last expression in body)
fn square(x) { x * x }

-- with type annotations
fn factorial(n: int) -> int {
    if n <= 1 { return 1 }
    return n * factorial(n - 1)
}

-- lambda / anonymous function
let sq = fn(x) { x * x }

-- arrow lambda
let inc = (x) => x + 1

-- default parameters
fn greet(name, greeting = "hello") {
    return "{greeting}, {name}"
}
println(greet("world"))          -- hello, world
println(greet("world", "hi"))    -- hi, world

-- variadic
fn sum(...args) {
    var total = 0
    for a in args { total = total + a }
    return total
}
println(sum(1, 2, 3))           -- 6
println(sum())                   -- 0

-- functions as values
fn apply(f, x) { return f(x) }
println(apply(fn(x) { x * 3 }, 5))  -- 15

-- fn main() is auto-called if defined
fn main() {
    println("entry point")
}
```

**Implicit return** applies to the last expression in the block. For early returns, use `return` explicitly.

### Function Attributes

Decorate functions with `@` or `#[...]` attributes before the `fn` keyword.

```xs
-- @pure: checked by sema: cannot call println, read_file, sleep, etc.
@pure
fn add(a, b) { return a + b }

-- @test: marks a function as a test case (used by xs test)
@test
fn test_math() {
    assert_eq(1 + 1, 2)
}

-- @deprecated: warns callers at check time
@deprecated("use new_api() instead")
fn old_api() { return 42 }

-- #[...] attribute syntax (equivalent)
#[test]
fn test_strings() {
    assert("hello".len() == 5)
}

#[deprecated("moved to v2")]
fn legacy() { }

-- pub: marks a function as public (visible to importers)
pub fn helper() { return 42 }

-- static: in impl blocks, a method that doesn't take self
impl Point {
    static fn origin() {
        return Point { x: 0, y: 0 }
    }
}
```

`@pure` is enforced by the semantic analyzer: calling impure functions (I/O, sleep, exit) inside a `@pure` function is an error.

### Closures

Closures capture variables by reference through an environment chain. Mutations inside a closure are visible to the outer scope.

```xs
fn make_counter() {
    var count = 0
    return fn() {
        count = count + 1
        return count
    }
}
let c = make_counter()
println(c())                     -- 1
println(c())                     -- 2
println(c())                     -- 3
```

Nested closures work: each level captures its parent's environment:

```xs
fn outer() {
    var x = 10
    fn middle() {
        var y = 20
        fn inner() { return x + y }
        return inner()
    }
    return middle()
}
println(outer())                 -- 30
```

### Mutual Recursion

Functions can call each other before both are defined (they're hoisted):

```xs
fn is_even(n) {
    if n == 0 { return true }
    return is_odd(n - 1)
}
fn is_odd(n) {
    if n == 0 { return false }
    return is_even(n - 1)
}
println(is_even(10))             -- true
println(is_odd(7))               -- true
```

### Function Overloading

Multiple functions with the same name are dispatched by argument count at call time.

```xs
fn greet() {
    println("hello, world!")
}

fn greet(name) {
    println("hello, {name}!")
}

fn greet(name, greeting) {
    println("{greeting}, {name}!")
}

greet()                          -- hello, world!
greet("Alice")                   -- hello, Alice!
greet("Bob", "hey")             -- hey, Bob!
```

Overloading works with default parameters and variadic functions. When multiple overloads could match, the first one with an exact arity match wins. If no overload matches the argument count, it's a runtime error.

```xs
fn calc(x) = x * 2
fn calc(x, y) = x + y
fn calc(x, y, z) = x + y + z

println(calc(5))                 -- 10
println(calc(3, 4))              -- 7
println(calc(1, 2, 3))           -- 6
```

---

## Generators

Generator functions use `fn*` and `yield` to produce values lazily.

```xs
fn* count_up(n) {
    var i = 0
    while i < n {
        yield i
        i = i + 1
    }
}

for x in count_up(5) {
    print("{x} ")                -- 0 1 2 3 4
}
```

Generators pause at each `yield` and resume when the next value is requested. They work with `for..in` loops.

---

## Tagged Blocks

Tags are user-defined control structures. Define a tag with `tag name(params) { body }`, then call it with a trailing block: `name(args) { block }`. Inside the tag body, `yield` executes the caller's block and returns its result.

```xs
-- define a tag that runs a block twice
tag twice() {
    yield;
    yield;
}

twice() {
    println("hello!")             -- prints twice
}
```

Tags are useful for wrapping common patterns like retry logic, timing, error suppression, and resource management.

```xs
-- retry a block up to n times
tag retry(n) {
    var attempts = 0
    loop {
        try {
            let result = yield;
            return result
        } catch e {
            attempts = attempts + 1
            if attempts >= n {
                throw "failed after {n} attempts: {e}"
            }
        }
    }
}

retry(3) {
    let resp = http.get("https://flaky-api.com")
    resp["body"]
}
```

```xs
-- measure how long a block takes
tag timed() {
    import time
    let start = time.clock()
    let result = yield;
    println("took {time.clock() - start}s")
    return result
}

timed() {
    heavy_computation()
}
```

```xs
-- provide a fallback if block returns null
tag with_default(fallback) {
    let val = yield;
    if val == null { return fallback }
    return val
}

let config = with_default(#{}) {
    load_config("app.toml")
}
```

Trailing blocks work at both statement level and inside `let`/`var` assignments:

```xs
let result = suppress() {
    might_throw()
}
```

Tags desugar to regular functions with an implicit `__block` parameter. The trailing block `{ ... }` is passed as a zero-argument lambda. `yield` inside the tag body calls that lambda.

---

## Structs

```xs
struct Point { x, y }

-- create an instance
let p = Point { x: 10, y: 20 }
println(p.x)                    -- 10
println(p.y)                    -- 20

-- with type annotations on fields
struct Config {
    host: str,
    port: int,
    debug: bool
}

-- field defaults
struct Options {
    verbose = false,
    retries: int = 3,
    timeout: f64 = 30.0
}
let opts = Options {}               -- all defaults
let opts2 = Options { verbose: true } -- override one

-- derives: auto-implement traits
struct Vec2 { x, y } derives Eq, Hash

-- #[derive(...)] attribute syntax (equivalent)
#[derive(Eq, Hash)]
struct Vec3 { x, y, z }
```

### Impl Blocks

Add methods to structs with `impl`:

```xs
impl Point {
    fn distance(self) {
        return sqrt(self.x * self.x + self.y * self.y)
    }

    fn translate(self, dx, dy) {
        return Point { x: self.x + dx, y: self.y + dy }
    }
}

let p = Point { x: 3, y: 4 }
println(p.distance())           -- 5
let moved = p.translate(1, 0)
println(moved.x)                -- 4
```

Methods that access instance data take `self` as the first parameter. `self` is not implicit.

### Operator Overloading

Define operators as methods in impl blocks by using the operator as the function name:

```xs
struct Vec2 { x, y }
impl Vec2 {
    fn +(self, other) {
        return Vec2 { x: self.x + other.x, y: self.y + other.y }
    }
    fn *(self, scalar) {
        return Vec2 { x: self.x * scalar, y: self.y * scalar }
    }
}

let a = Vec2 { x: 1, y: 2 }
let b = Vec2 { x: 3, y: 4 }
let c = a + b
println(c.x)                    -- 4
println(c.y)                    -- 6
```

Overloadable operators: `+`, `-`, `*`, `/`, `%`, `==`, `!=`, `<`, `>`, `<=`, `>=`, `++`, `&&`, `||`.

### Spread / Update Syntax

Create a new struct based on an existing one, overriding specific fields:

```xs
let p = Point { x: 10, y: 20 }
let p2 = Point { ...p, y: 30 }
println(p2.x)                   -- 10
println(p2.y)                   -- 30
```

### Struct Destructuring

```xs
let Point { x: a, y: b } = Point { x: 100, y: 200 }
println(a)                       -- 100
println(b)                       -- 200
```

---

## Enums

```xs
-- simple enum
enum Color { Red, Green, Blue }
let c = Color::Red
println(c)                       -- Color::Red

-- enum with associated data
enum Shape {
    Circle(radius),
    Rect(w, h),
    Triangle(a, b, c)
}

let s = Shape::Circle(5)
let r = Shape::Rect(3, 4)
```

### Pattern Matching on Enums

```xs
fn describe(shape) {
    return match shape {
        Shape::Circle(r) => "circle r={r}"
        Shape::Rect(w, h) => "rect {w}x{h}"
        Shape::Triangle(a, b, c) => "triangle {a},{b},{c}"
        _ => "unknown"
    }
}

println(describe(Shape::Circle(5)))    -- circle r=5
println(describe(Shape::Rect(3, 4)))   -- rect 3x4
```

---

## Traits

Traits define shared behavior across types.

```xs
trait Describe {
    fn describe(self) -> str
}

struct Dog { name, breed }
struct Car { make, year }

impl Describe for Dog {
    fn describe(self) -> str {
        return "{self.name} the {self.breed}"
    }
}

impl Describe for Car {
    fn describe(self) -> str {
        return "{self.year} {self.make}"
    }
}

let d = Dog { name: "Rex", breed: "Shepherd" }
println(d.describe())           -- Rex the Shepherd
let car = Car { make: "Volvo", year: 2024 }
println(car.describe())         -- 2024 Volvo
```

A type can implement multiple traits:

```xs
trait Area {
    fn area(self) -> f64
}

struct Circle { radius }

impl Describe for Circle {
    fn describe(self) -> str { return "circle r={self.radius}" }
}

impl Area for Circle {
    fn area(self) -> f64 { return 3.14159 * self.radius * self.radius }
}
```

### Default Methods

Traits can provide default implementations. Types only need to override them if they want different behavior.

```xs
trait Greet {
    fn hello(self) -> str {
        return "hello from {self.name}"
    }
    fn goodbye(self) -> str   -- no default, must implement
}

struct Person { name }
impl Greet for Person {
    -- hello() uses the default, only goodbye() is required
    fn goodbye(self) -> str { return "bye from {self.name}" }
}
```

### Super Traits

A trait can require another trait as a prerequisite:

```xs
trait Display {
    fn display(self) -> str
}

trait PrettyPrint: Display {
    fn pretty(self) -> str
}

-- implementing PrettyPrint for a type requires Display to also be implemented
```

### Associated Types

Traits can declare associated type names:

```xs
trait Iterator {
    type Item
    fn next(self) -> Item
}
```

### Trait Checking

The semantic analyzer enforces trait implementations:

- **Missing methods**: if an impl block is missing a required method (one without a default body), that's an error.
- **Parameter count mismatch**: if the impl's method has a different number of params than the trait declares, that's an error.
- **Return type mismatch**: if the trait declares `-> str` and the impl returns `-> int`, that's an error.
- **Orphan rule**: you can only implement a trait if either the trait or the type is defined in the same file. Implementing a foreign trait for a foreign type is an error.

---

## Classes

Classes support constructors, methods, fields with defaults, and single inheritance.

```xs
class Animal {
    name = ""
    sound = "..."

    fn init(self, name) {
        self.name = name
    }

    fn speak(self) {
        return "{self.name} says {self.sound}"
    }
}

let cat = Animal("Cat")
cat.sound = "meow"
println(cat.speak())             -- Cat says meow
```

The constructor method is `init`. Instantiate with `ClassName(args)`.

### Inheritance

```xs
class Dog : Animal {
    fn init(self, name) {
        super.init(name)
        self.sound = "woof"
    }

    fn fetch(self) {
        return "{self.name} fetches the ball"
    }
}

let d = Dog("Rex")
println(d.speak())               -- Rex says woof
println(d.fetch())               -- Rex fetches the ball
```

Subclasses call `super.init(...)` to initialize parent fields. Methods can be overridden.

### Fields with Defaults

```xs
class Config {
    host = "localhost"
    port = 8080
    debug = false

    fn init(self, host, port) {
        self.host = host
        self.port = port
    }

    fn url(self) { return "{self.host}:{self.port}" }
}

let c = Config("example.com", 443)
println(c.url())                 -- example.com:443
println(c.debug)                 -- false
```

---

## Type System

XS has gradual typing. Code runs fine without any annotations. Add them where you want enforcement: the type checker only kicks in on annotated code and passes through everything else silently.

### Type Annotations

Annotations go after a colon on variables, after parameter names, after `->` for return types, and after struct field names.

```xs
-- variables
let count: int = 42
var name: str = "XS"
const MAX: i64 = 100

-- function parameters and return type
fn add(a: int, b: int) -> int {
    return a + b
}

-- struct fields
struct Config {
    host: str,
    port: int,
    debug: bool
}

-- const with annotation
const PI: f64 = 3.14159
```

### Primitive Types

| Annotation | Description |
|------------|-------------|
| `int` / `i64` | 64-bit signed integer (default integer type) |
| `i8`, `i16`, `i32` | Smaller signed integers |
| `u8`, `u16`, `u32`, `u64` | Unsigned integers |
| `float` / `f64` | 64-bit float (default float type) |
| `f32` | 32-bit float |
| `str` / `string` | String |
| `bool` | Boolean |
| `char` | Character |
| `byte` | Alias for `u8` |
| `re` | Regex |
| `any` / `dyn` | Any type (disables checking) |
| `void` / `unit` | No value |
| `never` | Function that never returns |

Integer annotations are interchangeable for checking purposes: `int`, `i32`, and `i64` all accept integer literals. Same for `float`, `f32`, `f64`.

### Composite Types

```xs
-- array of ints
let nums: [int] = [1, 2, 3]

-- tuple
let pair: (int, str) = (42, "hello")

-- optional (nullable)
let maybe: int? = null
let found: str? = "yes"

-- function type
let transform: fn(int) -> int = fn(x) { x * 2 }

-- generic types
let items: array<int> = [1, 2, 3]
let lookup: map<str, int> = #{"a": 1}

-- nested
let grid: [[int]] = [[1, 2], [3, 4]]
let handlers: [fn(str) -> bool] = []
```

### What Gets Checked

The type checker runs as part of semantic analysis (before execution). It catches:

**Variable assignment mismatches:**
```xs
let x: int = "hello"
-- error[T0001]: type mismatch: expected 'int', got 'str'
--   hint: use int() or float() to convert a string to a number
```

**Function argument types:**
```xs
fn greet(name: str) { println("hi {name}") }
greet(42)
-- error[T0001]: type mismatch: expected 'str', got 'i64'
--   hint: use to_str() to convert a number to a string
```

**Return type mismatches:**
```xs
fn double(x: int) -> int {
    return "oops"
-- error[T0001]: type mismatch: expected 'int', got 'str'
}
```

**Struct field types:**
```xs
struct Point { x: int, y: int }
let p = Point { x: "bad", y: 0 }
-- error[T0001]: type mismatch: expected 'int', got 'str'
```

**Match arm consistency:**
```xs
let r = match x {
    1 => "one"
    2 => 42        -- error: match arm type mismatch
}
```

**Unknown type names:**
```xs
let x: Foo = 42
-- error[T0011]: unknown type 'Foo'
--   hint: check spelling or define a struct/enum named 'Foo'
```

**User-defined types work too**: if you define a struct or enum, it becomes a valid type name:
```xs
struct Point { x: int, y: int }
let p: Point = Point { x: 1, y: 2 }  -- valid
```

### What Doesn't Get Checked

Unannotated code passes through silently. The checker infers types where it can (literals, operators, function calls with known signatures) but never forces you to annotate.

```xs
-- no annotations, no errors, runs fine
let x = 42
let y = x + 1
fn foo(a, b) { return a + b }
```

### Type Checking Modes

```bash
xs script.xs            -- normal: type check annotated code, then run
xs --check script.xs    -- check only, don't execute
xs --strict script.xs   -- require annotations on all variables, params, and return types
xs --lenient script.xs  -- downgrade type errors to warnings
```

**Strict mode** enforces annotations everywhere:
```xs
-- with --strict, this is an error:
let x = 42
-- error[S0010]: missing type annotation for 'x' in strict mode
--   hint: use 'let x: <type> = ...'

-- fix:
let x: int = 42
```

In strict mode, every `let`/`var`/`const`, every function parameter, and every function return type must have an annotation.

### Type Aliases

```xs
type UserId = i64
type Handler = fn(str) -> bool
```

### Generic Type Parameters

Functions can declare type parameters with optional bounds:

```xs
fn identity<T>(x: T) -> T {
    return x
}

fn first<T>(arr: [T]) -> T {
    return arr[0]
}

-- with trait bounds
fn display<T: Describe>(item: T) -> str {
    return item.describe()
}
```

Structs and enums can also have type parameters (parsed but currently erased at runtime: the syntax is accepted for forward compatibility):

```xs
struct Pair<A, B> { first: A, second: B }
enum Option<T> { Some(T), None }
```

### Inferred Placeholder

Use `_` to let the checker infer a type in a position where you'd normally write one:

```xs
let x: _ = 42    -- inferred as int
```

---

## Error Handling

### Try / Catch / Finally

```xs
try {
    throw "something went wrong"
} catch e {
    println("Error: {e}")        -- Error: something went wrong
}

-- throw any value (string, int, map, whatever)
try {
    throw #{"kind": "NotFound", "msg": "missing"}
} catch e {
    println(e["msg"])            -- missing
}

-- finally always runs
try {
    throw "err"
} catch e {
    println("caught")
} finally {
    println("cleanup")           -- always executes
}
```

### Nested Try/Catch

```xs
try {
    try {
        throw "inner"
    } catch e {
        throw "rethrown: {e}"
    }
} catch e {
    println(e)                   -- rethrown: inner
}
```

### Throw from Functions

Exceptions propagate up the call stack:

```xs
fn divide(a, b) {
    if b == 0 { throw "division by zero" }
    return a / b
}

try {
    divide(10, 0)
} catch e {
    println(e)                   -- division by zero
}
```

### Panic

`panic(msg)` terminates immediately. It is **not catchable** by try/catch.

```xs
panic("fatal: out of memory")
-- prints to stderr: xs: panic: fatal: out of memory
-- exits with code 1
```

### Defer

`defer` schedules a block to run when the enclosing function returns. Multiple defers execute in LIFO (last-in, first-out) order.

```xs
fn example() {
    defer { println("third") }
    defer { println("second") }
    defer { println("first") }
    println("body")
}
example()
-- body
-- first
-- second
-- third
```

Defers run even if an exception is thrown.

### When to Use What

| Mechanism | Catchable? | Use case |
|-----------|------------|----------|
| `throw expr` | Yes | Recoverable errors: bad input, validation, missing data |
| `panic(msg)` | No | Unrecoverable: invariant violations, impossible states |
| `todo(msg?)` | No | Placeholder for unimplemented code |
| `unreachable()` | No | Code that should never execute |

---

## Unsafe Blocks

`unsafe { }` marks a block as unchecked. Currently a parsing/AST annotation: the runtime doesn't restrict anything inside unsafe blocks, but it signals intent to the reader and to future tooling.

```xs
unsafe {
    -- code that does something risky
    let ptr = some_ffi_call()
}
```

---

## Inline C

`inline c { ... }` embeds raw C code inside an XS function. The C code is passed through verbatim to the C transpiler. In the interpreter and VM, inline C blocks are skipped with a warning.

```xs
fn fast_hash(data) {
    inline c {
        uint64_t h = 0x525201;
        const char *s = xs_to_cstr(args[0]);
        while (*s) h = h * 31 + *s++;
        xs_return_int(h);
    }
    return 0  -- fallback for interpreter mode
}
```

Use `xs transpile --target c` to compile code that uses inline C. The raw C code has access to the enclosing function's arguments via an `args[]` array and can return values using helper macros.

This is useful for performance-critical inner loops, FFI glue, or leveraging C libraries directly without writing a full native plugin.

---

## Adapt Functions

`adapt fn` defines a function with different implementations for different compilation targets. The interpreter always picks the `native` branch. The JS transpiler picks `js`. The C transpiler picks `native`. The WASM transpiler picks `wasm`.

```xs
adapt fn greet(name: str) -> str {
    when native {
        return "hello from native, " ++ name
    }
    when js {
        return "hello from js, " ++ name
    }
    when wasm {
        return "hello from wasm, " ++ name
    }
}

println(greet("world"))          -- hello from native, world (in interpreter)
```

If the current target doesn't have a matching branch, it falls back to `native`, then to the first branch defined.

```xs
-- only need native and js? that's fine
adapt fn fast_hash(data: str) -> int {
    when native {
        var h = 0
        for ch in data { h = h * 31 + type(ch) }
        return h
    }
    when js {
        return data.len * 37
    }
}
```

Adapt functions support all the same features as regular functions: type annotations, contracts, default params. The branches are just blocks, so any XS code works inside them.

```xs
-- combine with contracts
adapt fn validate(n: int where n > 0) -> int {
    when native {
        return n * 2
    }
    when js {
        return n
    }
}
```

This makes multi-target code a language-level concept instead of just a build flag. Write your platform-specific code inline, and the right branch gets selected automatically.

---

## Signals (Reactive State)

Signals are observable values that automatically propagate changes. `derived()` creates computed signals that update when their dependencies change.

```xs
let count = signal(0)
println(count.get())             -- 0

count.set(5)
println(count.get())             -- 5

-- derived signals auto-update
let doubled = derived(fn() { count.get() * 2 })
println(doubled.get())           -- 10

count.set(10)
println(doubled.get())           -- 20

-- subscribe to changes
count.subscribe(fn(val) {
    println("count changed to {val}")
})
count.set(42)                    -- prints: count changed to 42
```

Signals are also available via `import reactive` as `reactive.signal()` and `reactive.derived()`.

---

## Algebraic Effects

Effects let you perform an operation without knowing how it will be handled: the handler decides. Think of it as exceptions you can resume from.

```xs
-- declare an effect
effect Ask {
    fn prompt(msg) -> str
}

-- perform an effect
fn greet() {
    let name = perform Ask.prompt("name?")
    return "Hello, {name}!"
}

-- handle the effect
let result = handle greet() {
    Ask.prompt(msg) => resume("World")
}
println(result)                  -- Hello, World!
```

`resume` returns a value to the `perform` site: execution continues from where it left off.

### Effect with Accumulator

```xs
effect Log {
    fn log(msg)
}

var logs = []
handle {
    perform Log.log("first")
    perform Log.log("second")
    perform Log.log("third")
} {
    Log.log(msg) => {
        logs.push(msg)
        resume(null)
    }
}
println(logs)                    -- ["first", "second", "third"]
```

The `handle` form can take a block as the computation (not just a function call).

---

## Concurrency

### Spawn

`spawn` runs a block as a task. In the current interpreter, tasks execute immediately (cooperative, not preemptive).

```xs
var done = false
spawn { done = true }
println(done)                    -- true

-- spawn returns a task handle (a map)
let t = spawn { 1 + 2 }
println(t["_result"])            -- 3
println(t["_status"])            -- done
```

### Async / Await

```xs
async fn compute(x) {
    return x * 2
}

let r = await compute(21)
println(r)                       -- 42

async fn fetch_user(id) {
    return #{"id": id, "name": "User {id}"}
}

let user = await fetch_user(42)
println(user["name"])            -- User 42
```

### Channels

Channels are FIFO message queues. Unbounded by default, or bounded with a capacity.

```xs
-- unbounded channel
let ch = channel()
ch.send("ping")
ch.send("pong")
println(ch.recv())               -- ping
println(ch.recv())               -- pong
println(ch.len())                -- 0
println(ch.is_empty())           -- true

-- bounded channel
let bch = channel(2)
bch.send("a")
bch.send("b")
println(bch.is_full())           -- true
println(bch.recv())              -- a
println(bch.is_full())           -- false
```

### Actors

Actors encapsulate state and respond to method calls or raw messages.

```xs
actor BankAccount {
    var balance = 0

    fn deposit(amount) {
        balance = balance + amount
    }

    fn withdraw(amount) {
        if amount > balance { return Err("insufficient funds") }
        balance = balance - amount
        return Ok(balance)
    }

    fn get_balance() { return balance }

    -- handle() processes raw messages sent with !
    fn handle(msg) {
        if msg == "reset" { balance = 0 }
    }
}

let acct = spawn BankAccount
acct.deposit(100)
acct.deposit(50)
println(acct.get_balance())      -- 150

-- send raw message with !
acct ! "reset"
println(acct.get_balance())      -- 0
```

### Nursery (Structured Concurrency)

Nursery blocks wait for all spawned tasks to finish before continuing. No tasks leak out.

```xs
var results = []
nursery {
    spawn { results.push("a") }
    spawn { results.push("b") }
    spawn { results.push("c") }
}
-- all tasks complete before we get here
println(results.sort())          -- ["a", "b", "c"]
```

Nurseries compose with channels for producer/consumer patterns:

```xs
let pipe = channel()
var output = []

nursery {
    spawn {
        for i in 1..=3 { pipe.send(i * 10) }
    }
    spawn {
        for i in 0..3 { output.push(pipe.recv()) }
    }
}
println(output)                  -- [10, 20, 30]
```

---

## Modules and Imports

### Importing Standard Library Modules

```xs
import math
println(math.sqrt(16))           -- 4
println(math.PI)                 -- 3.14159

-- with alias
import math as m
println(m.sqrt(16))              -- 4

-- selective import
from math import { sqrt, PI }
println(sqrt(25))                -- 5
println(PI)                      -- 3.14159
```

### Importing Files

```xs
-- use "path" imports a file as a module (namespace derived from filename)
use "utils.xs"
println(utils.helper())

-- with alias
use "utils.xs" as u
println(u.helper())

-- selective import
use "utils.xs" { helper, VERSION }
println(helper())
```

For directories, `use "dir/"` imports all `.xs` files in the directory.

### Declaring Modules Inline

```xs
module Utils {
    fn double(x) { return x * 2 }
    fn triple(x) { return x * 3 }
}
println(Utils.double(5))         -- 10
println(Utils.triple(4))         -- 12
```

---

## List Comprehensions

```xs
let squares = [x * x for x in 0..5]
println(squares)                 -- [0, 1, 4, 9, 16]

let evens = [x for x in 0..10 if x % 2 == 0]
println(evens)                   -- [0, 2, 4, 6, 8]
```

---

## Map Comprehensions

```xs
let sq = #{x: x * x for x in [1, 2, 3]}
println(sq)                      -- {1: 1, 2: 4, 3: 9}

-- with tuple destructuring
let m = #{k: v for (k, v) in #{"a": 1, "b": 2}.entries()}

-- with filter
let even_sq = #{x: x * x for x in [1, 2, 3, 4] if x % 2 == 0}
println(even_sq)                 -- {2: 4, 4: 16}
```

---

## Spread Operator

```xs
-- array spread
let a = [1, 2, 3]
let b = [...a, 4, 5]            -- [1, 2, 3, 4, 5]

-- map spread
let m = #{"a": 1}
let m2 = #{...m, "b": 2}        -- {"a": 1, "b": 2}

-- struct spread (update syntax)
let p = Point { x: 10, y: 20 }
let p2 = Point { ...p, y: 30 }
```

---

## Built-in Functions

### I/O

| Function | Description |
|----------|-------------|
| `println(args...)` | Print with newline. Supports `{}` placeholders |
| `print(args...)` | Print without trailing newline |
| `eprint(args...)` | Print to stderr without newline |
| `eprintln(args...)` | Print to stderr with newline |
| `input(prompt?)` | Read line from stdin |
| `clear()` | Clear terminal screen |

```xs
println("x = {}", 42)           -- x = 42  (positional {} placeholder)
println("hi", "there")          -- hi there (space-separated if no {})
```

### Type Checking

| Function | Description |
|----------|-------------|
| `type(val)` | Type name lowercase: `"int"`, `"str"`, `"array"`, etc. |
| `typeof(val)` | Alias for `type()` |
| `type_of(val)` | Type name capitalized: `"Int"`, `"Str"`, `"Array"`, etc. |
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
| `str(val)` | Convert to string |
| `bool(val)` | Convert to boolean |
| `char(n)` / `chr(n)` | Integer to single-character string |
| `ord(ch)` | Character (first byte) to integer |

```xs
println(int(3.9))                -- 3  (toward zero)
println(int(-3.9))               -- -3
println(chr(65))                 -- A
println(ord("A"))                -- 65
println(str(42))                 -- "42"
println(float(10))               -- 10.0
```

### Math

| Function | Description |
|----------|-------------|
| `abs(x)` | Absolute value |
| `min(a, b)` | Minimum of two values |
| `max(a, b)` | Maximum of two values |
| `pow(base, exp)` | Power |
| `sqrt(x)` | Square root |
| `floor(x)` | Floor |
| `ceil(x)` | Ceiling |
| `round(x)` | Round to nearest |
| `log(x)` | Natural logarithm |
| `sin(x)` `cos(x)` `tan(x)` | Trig functions (radians) |

### Collections

| Function | Description |
|----------|-------------|
| `len(val)` | Length of array, string, map, tuple, or range |
| `range(n)` | Create range `0..n` |
| `array()` | Empty array |
| `map()` | Empty map |
| `sorted(arr)` | Sorted copy of array |
| `sum(arr)` | Sum of numeric array |
| `enumerate(arr)` | Array of `(index, value)` tuples |
| `zip(a, b)` | Zip two arrays into tuples |
| `flatten(arr)` | Flatten one level |
| `keys(map)` | Map keys as array |
| `values(map)` | Map values as array |
| `entries(map)` | Map entries as `(key, value)` tuples |
| `chars(str)` | String to array of characters |
| `bytes(str)` | String to array of byte values |
| `contains(str, sub)` | Check if string contains substring |

```xs
println(enumerate([10, 20, 30]))  -- [(0, 10), (1, 20), (2, 30)]
println(zip([1, 2], [3, 4]))     -- [(1, 3), (2, 4)]
println(range(5))                -- 0..5
```

### Functional

| Function | Description |
|----------|-------------|
| `map(arr, fn)` | Apply fn to each element |
| `filter(arr, fn)` | Keep elements where fn returns truthy |
| `reduce(arr, fn, init)` | Reduce array to single value |

```xs
println(map([1, 2, 3], fn(x) { x * 2 }))      -- [2, 4, 6]
println(filter([1, 2, 3, 4], fn(x) { x > 2 })) -- [3, 4]
println(reduce([1, 2, 3], fn(a, b) { a + b }, 0))  -- 6
```

### Debugging and Testing

| Function | Description |
|----------|-------------|
| `assert(cond, msg?)` | Assert truthy; panics on failure |
| `assert_eq(a, b)` | Assert `a == b`; shows both values on failure |
| `dbg(val)` | Debug-print to stderr with type info, returns `val` |
| `pprint(val)` | Pretty-print with indentation |
| `repr(val)` | Debug string representation |
| `panic(msg)` | Print to stderr and exit (not catchable) |
| `todo(msg?)` | Mark unimplemented; panics |
| `unreachable()` | Mark unreachable; panics if reached |
| `exit(code)` | Exit with given code |

### Copying

| Function | Description |
|----------|-------------|
| `copy(val)` / `clone(val)` | Shallow copy of a value |

### Result / Option Constructors

```xs
println(Ok(42))                  -- Ok(42)
println(Err("bad"))              -- Err(bad)
println(Some(10))                -- Some(10)
println(None())                  -- null
```

These are used for explicit result / option values you can `match` on.

### String Formatting

```xs
-- positional placeholders
println(format("hello {} you are {}", "world", 42))
-- hello world you are 42
```

`sprintf` is an alias for `format`.

### Constants

| Name | Value |
|------|-------|
| `PI` | 3.14159265358979... |
| `E` | 2.71828182845904... |
| `INF` | Infinity (float) |
| `NAN` | Not a Number (float) |

### Globals

| Name | Type | Description |
|------|------|-------------|
| `argv` | array | Command-line args after the script name |

```xs
-- Run: xs script.xs hello world
println(argv)                    -- ["hello", "world"]
```

---

## Number Methods

```xs
println((-5).abs())              -- 5
println((7).clamp(0, 5))         -- 5
println((42).to_str())           -- "42"
println((4).is_even())           -- true
println((3).is_odd())            -- true
```

---

## Standard Library Modules

Import with `import <module>` and access via `module.member`.

---

### `math`

**Constants:** `PI` / `pi`, `E` / `e`, `TAU` / `tau`, `INF` / `inf`, `NAN` / `nan`

**Functions:**

| Category | Functions |
|----------|-----------|
| Trig | `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sinh`, `cosh`, `tanh`, `asinh`, `acosh`, `atanh` |
| Exponents/logs | `sqrt`, `cbrt`, `exp`, `expm1`, `log`, `log2`, `log10`, `log1p` |
| Rounding | `floor`, `ceil`, `round`, `trunc` |
| Utility | `abs`, `pow`, `hypot`, `gcd`, `lcm`, `factorial`, `clamp`, `lerp`, `sign`, `degrees`, `radians`, `fmod`, `modf`, `copysign`, `isclose`, `frexp`, `ldexp` |
| Combinatorial | `comb`, `perm` |
| Predicates | `is_nan`, `is_inf` |
| Aggregate | `prod`, `sum`, `min`, `max`, `mean` |
| Special | `erf`, `erfc`, `gamma`, `lgamma` |

```xs
import math
println(math.sqrt(16))           -- 4
println(math.gcd(12, 8))         -- 4
println(math.factorial(5))       -- 120
println(math.clamp(15, 0, 10))   -- 10
println(math.sign(-5))           -- -1
println(math.degrees(math.PI))   -- 180
println(math.radians(180))       -- 3.14159...
println(math.lerp(0, 100, 0.5))  -- 50
println(math.hypot(3, 4))        -- 5
```

---

### `time`

| Function | Description |
|----------|-------------|
| `now()` | Current Unix time as float (seconds since epoch) |
| `now_ms()` | Current time in milliseconds |
| `clock()` / `monotonic()` | Monotonic clock (for timing) |
| `sleep(secs)` | Sleep for seconds (float OK) |
| `sleep_ms(ms)` | Sleep for milliseconds |
| `millis()` | Current time in milliseconds |
| `stopwatch()` | Returns a stopwatch map with `elapsed()` method |
| `format(t, fmt)` | Format a timestamp as a string |
| `parse(s, fmt)` | Parse a string into a timestamp |
| `year(t)` | Year component of timestamp |
| `month(t)` | Month component |
| `day(t)` | Day component |
| `hour(t)` | Hour component |
| `minute(t)` | Minute component |
| `second(t)` | Second component |

```xs
import time
let t = time.now()
println("epoch: {t}")
time.sleep(0.01)
let sw = time.stopwatch()
-- sw.elapsed() returns seconds since creation
```

---

### `io`

**File operations:**

| Function | Description |
|----------|-------------|
| `read_file(path)` | Read entire file as string |
| `write_file(path, data)` | Write string to file (overwrites) |
| `append_file(path, data)` | Append string to file |
| `read_lines(path)` | Read file as array of lines |
| `write_lines(path, lines)` | Write array of lines to file |
| `read_bytes(path)` | Read file as byte array |
| `write_bytes(path, bytes)` | Write byte array to file |
| `read_json(path)` | Read and parse JSON file |
| `write_json(path, val)` | Serialize and write JSON file |

**File info:**

| Function | Description |
|----------|-------------|
| `exists(path)` / `file_exists(path)` | Check if path exists |
| `size(path)` / `file_size(path)` | File size in bytes |
| `file_info(path)` | Map with file metadata |
| `is_file(path)` | True if path is a regular file |
| `is_dir(path)` | True if path is a directory |

**File manipulation:**

| Function | Description |
|----------|-------------|
| `delete_file(path)` | Delete a file |
| `copy_file(src, dst)` | Copy a file |
| `move_file(src, dst)` | Move a file |
| `rename_file(old, new)` | Rename a file |
| `symlink(target, link)` | Create a symlink |

**Directories:**

| Function | Description |
|----------|-------------|
| `make_dir(path)` | Create directory (recursive) |
| `list_dir(path)` | List directory entries as array |
| `glob(pattern)` | Glob matching |

**Temp files:**

| Function | Description |
|----------|-------------|
| `temp_file()` | Create a temporary file, return path |
| `temp_dir()` | Create a temporary directory, return path |

**Stdin:**

| Function | Description |
|----------|-------------|
| `read_line(prompt?)` | Read a line from stdin |
| `wait_for_key(prompt?)` | Wait for user to press enter |
| `stdin_read()` | Read all of stdin |
| `stdin_readline()` | Read one line from stdin |
| `stdin_read_n(n)` | Read n bytes from stdin |
| `stdin_lines()` | Read all stdin lines as array |

**Sub-modules:** `io.stdout.write(s)`, `io.stdout.writeln(s)`, `io.stdout.flush()`, `io.stderr.write(s)`, `io.stderr.writeln(s)`, `io.stderr.flush()`

```xs
import io
let text = io.read_file("hello.txt")
io.write_file("out.txt", "hello\n")
let lines = io.read_lines("data.txt")
println(io.exists("out.txt"))    -- true
println(io.is_file("out.txt"))   -- true
```

---

### `os`

| Member | Description |
|--------|-------------|
| `platform` | `"linux"`, `"darwin"`, or `"windows"` |
| `sep` | Path separator (`"/"` or `"\\"`) |
| `args` | Command-line arguments array |
| `pid()` | Current process ID |
| `ppid()` | Parent process ID |
| `cwd()` | Current working directory |
| `chdir(path)` | Change working directory |
| `home()` | Home directory path |
| `tempdir()` | Temp directory path |
| `cpu_count()` | Number of CPU cores |
| `exit(code)` | Exit process |
| `mkdir(path)` | Create directory |
| `rmdir(path)` | Remove directory |
| `remove(path)` | Remove file or directory |
| `rename(old, new)` | Rename/move file |
| `exists(path)` | Check if path exists |
| `is_file(path)` | True if regular file |
| `is_dir(path)` | True if directory |
| `list_dir(path)` | List directory contents |
| `glob(pattern)` | Glob matching |
| `env(key)` / `getenv(key)` | Get environment variable |
| `setenv(key, val)` | Set environment variable |
| `hasenv(key)` | Check if env var exists |
| `environ()` | All environment variables as map |

```xs
import os
println(os.platform)             -- linux
println(os.cwd())                -- /home/user
println(os.env("HOME"))          -- /home/user
```

---

### `json`

| Function | Description |
|----------|-------------|
| `parse(str)` | Parse JSON string into XS value |
| `stringify(val)` | Serialize XS value to JSON string |
| `pretty(val)` | Serialize with indentation |
| `valid(str)` | Check if string is valid JSON |

```xs
import json
let s = json.stringify(#{"a": 1, "b": [1, 2, 3]})
println(s)                       -- {"a":1,"b":[1,2,3]}
let m = json.parse(s)
println(m["a"])                  -- 1
println(json.valid("[1,2,3]"))   -- true
```

---

### `string`

| Function | Description |
|----------|-------------|
| `pad_left(s, n, ch?)` | Left-pad string to width n |
| `pad_right(s, n, ch?)` | Right-pad string to width n |
| `center(s, n, ch?)` | Center string to width n |
| `truncate(s, n, suffix?)` | Truncate string to total length n |
| `camel_to_snake(s)` | Convert `helloWorld` to `hello_world` |
| `snake_to_camel(s)` | Convert `hello_world` to `helloWorld` |
| `escape_html(s)` | Escape HTML special characters |
| `is_numeric(s)` | True if string is a valid number |
| `words(s)` | Split string into words |
| `levenshtein(a, b)` | Edit distance between two strings |
| `similarity(a, b)` | Similarity score (0.0 to 1.0) |
| `repeat(s, n)` | Repeat string n times |
| `chars(s)` | String to array of characters |
| `bytes(s)` | String to array of byte values |

```xs
import string
println(string.words("hello world foo"))     -- ["hello", "world", "foo"]
println(string.camel_to_snake("helloWorld")) -- hello_world
println(string.snake_to_camel("hello_world"))-- helloWorld
println(string.levenshtein("kitten", "sitting")) -- 3
println(string.escape_html("<b>hi</b>"))     -- &lt;b&gt;hi&lt;/b&gt;
println(string.is_numeric("3.14"))           -- true
```

---

### `path`

| Member | Description |
|--------|-------------|
| `join(parts...)` | Join path components |
| `basename(p)` | Filename component |
| `dirname(p)` | Directory component |
| `ext(p)` | File extension (e.g. `".txt"`) |
| `stem(p)` | Filename without extension |
| `sep` | Path separator |

```xs
import path
println(path.basename("/foo/bar/baz.txt"))  -- baz.txt
println(path.dirname("/foo/bar/baz.txt"))   -- /foo/bar
println(path.ext("/foo/bar/baz.txt"))       -- .txt
println(path.stem("/foo/bar/baz.txt"))      -- baz
println(path.join("/foo", "bar", "baz"))    -- /foo/bar/baz
```

---

### `re`

| Function | Description |
|----------|-------------|
| `test(pattern, str)` / `is_match(pattern, str)` | True if pattern matches |
| `match(pattern, str)` | Return first match string, or null |
| `find_all(pattern, str)` | Return array of all matches |
| `replace(pattern, str, repl)` | Replace first match |
| `replace_all(pattern, str, repl)` | Replace all matches |
| `split(pattern, str)` | Split string by pattern |
| `groups(pattern, str)` | Return capture groups as array |

```xs
import re
println(re.match("\\d+", "abc 123 def"))      -- 123
println(re.find_all("\\d+", "1 2 3"))          -- ["1", "2", "3"]
println(re.replace("\\d+", "abc 123", "N"))    -- abc N
println(re.replace_all("\\d+", "1 2 3", "N")) -- N N N
println(re.split("\\s+", "a b c"))             -- ["a", "b", "c"]
println(re.test("^\\d+$", "123"))              -- true
```

---

### `random`

| Function | Description |
|----------|-------------|
| `int(min, max)` | Random integer in [min, max] |
| `float()` | Random float in [0.0, 1.0) |
| `bool()` | Random boolean |
| `choice(arr)` | Random element from array |
| `shuffle(arr)` | Shuffle array in-place |
| `sample(arr, n)` | n random elements without replacement |
| `seed(n)` | Set random seed |

```xs
import random
println(random.int(1, 10))       -- random int between 1 and 10
println(random.float())          -- random float 0.0-1.0
println(random.bool())           -- random boolean
println(random.choice(["a", "b", "c"]))  -- random element
```

---

### `hash`

| Function | Description |
|----------|-------------|
| `md5(data)` | MD5 hex digest |
| `sha1(data)` | SHA-1 hex digest |
| `sha256(data)` | SHA-256 hex digest |
| `sha512(data)` | SHA-512 hex digest |
| `hmac(key, data)` | HMAC-SHA256 hex digest |

```xs
import hash
println(hash.sha256("hello"))    -- 2cf24dba5fb0a30e...
println(hash.md5("hello"))       -- 5d41402abc4b2a76...
```

---

### `crypto`

| Function | Description |
|----------|-------------|
| `sha256(data)` | SHA-256 hex digest |
| `md5(data)` | MD5 hex digest |
| `random_bytes(n)` | n random bytes as hex string |
| `random_int(min, max)` | Cryptographically random integer |
| `uuid4()` | Generate UUID v4 |

```xs
import crypto
println(crypto.uuid4())          -- e.g. 8cbe806c-cd27-4d93-afd1-dbfa3f1b4f93
println(crypto.random_bytes(16)) -- 32 hex chars
```

---

### `encode`

| Function | Description |
|----------|-------------|
| `base64_encode(data)` | Base64 encode string |
| `base64_decode(data)` | Base64 decode string |
| `hex_encode(data)` | Hex encode string |
| `hex_decode(data)` | Hex decode string |
| `url_encode(s)` | URL-encode a string |
| `url_decode(s)` | URL-decode a string |

```xs
import encode
println(encode.base64_encode("hello"))  -- aGVsbG8=
println(encode.base64_decode("aGVsbG8="))  -- hello
println(encode.hex_encode("AB"))        -- 4142
println(encode.url_encode("a b+c"))     -- a+b%2Bc
```

Note: there's also a standalone `base64` module with `encode()` and `decode()`, and a `uuid` module with `v4()`.

---

### `collections`

| Function | Description |
|----------|-------------|
| `Counter(arr)` | Map of element counts |
| `Stack()` | Stack with `push`, `pop`, `peek`, `is_empty`, `len` |
| `PriorityQueue()` | Min-heap priority queue |
| `Deque()` | Double-ended queue |
| `Set(arr)` | Set (unique elements) |
| `OrderedMap()` | Map that preserves insertion order |

```xs
import collections

let s = collections.Set([1, 2, 3, 2, 1])
println(s)                       -- set with unique values

let c = collections.Counter(["a", "b", "a", "c", "a"])
println(c["a"])                  -- 3

let stack = collections.Stack()
stack.push(10)
stack.push(20)
println(stack.pop())             -- 20
```

---

### `fmt`

| Function | Description |
|----------|-------------|
| `number(n, decimals)` | Format number with fixed decimal places |
| `hex(n)` | Format integer as hex string (e.g. `"0xff"`) |
| `bin(n)` | Format integer as binary string (e.g. `"0b1010"`) |
| `pad(s, n)` | Pad string to width |
| `comma(n)` | Format number with comma separators |
| `filesize(n)` | Human-readable file size (e.g. `"1.2 MB"`) |
| `ordinal(n)` | Ordinal string (e.g. `"1st"`, `"2nd"`, `"3rd"`) |
| `pluralize(word, n)` | Pluralize word based on count |

```xs
import fmt
println(fmt.hex(255))            -- 0xff
println(fmt.bin(10))             -- 0b1010
println(fmt.comma(1000000))      -- 1,000,000
println(fmt.filesize(1536))      -- 1.5 KB
println(fmt.ordinal(1))          -- 1st
println(fmt.ordinal(11))         -- 11th
println(fmt.number(3.14159, 2))  -- 3.14
```

---

### `log`

| Function | Description |
|----------|-------------|
| `debug(msg)` | Log debug message |
| `info(msg)` | Log info message |
| `warn(msg)` | Log warning message |
| `error(msg)` | Log error message |
| `set_level(level)` | Set minimum log level |

```xs
import log
log.info("server started")
log.warn("disk space low")
log.error("connection failed")
log.set_level("warn")           -- only warn and above
```

---

### `test`

| Function | Description |
|----------|-------------|
| `assert(cond)` | Assert truthy |
| `assert_eq(a, b)` | Assert equal |
| `assert_ne(a, b)` | Assert not equal |
| `run(name, fn)` | Register a named test |
| `summary()` | Print test results summary |

```xs
import test
test.run("adds correctly", fn() {
    test.assert_eq(1 + 1, 2)
})
test.summary()
```

---

### `csv`

| Function | Description |
|----------|-------------|
| `parse(str)` | Parse CSV string into array of arrays |
| `stringify(rows)` | Serialize array of arrays to CSV string |

```xs
import csv
let rows = csv.parse("a,b,c\n1,2,3")
println(rows[0])                 -- ["a", "b", "c"]
```

---

### `url`

| Function | Description |
|----------|-------------|
| `parse(str)` | Parse URL string into component map |
| `encode(s)` | URL-encode a string |
| `decode(s)` | URL-decode a string |

---

### `reflect`

| Function | Description |
|----------|-------------|
| `type_of(val)` | Type name string |
| `fields(val)` | Field names of struct/class instance |
| `methods(val)` | Method names of struct/class instance |
| `is_instance(val, type)` | Check if value is instance of type |

---

### `net`

| Function | Description |
|----------|-------------|
| `tcp_connect(host, port)` | Open a TCP connection |
| `tcp_listen(port)` | Listen on a TCP port |
| `resolve(host)` | DNS lookup |

---

### `async`

| Function | Description |
|----------|-------------|
| `spawn(fn)` | Run function as async task |
| `sleep(secs)` | Async sleep |
| `channel()` | Create a channel |
| `select(channels)` | Poll multiple channels, return first ready |
| `all(tasks)` | Wait for all tasks, return results array |
| `race(tasks)` | Return result of first completed task |
| `resolve(val)` | Create already-resolved task |
| `reject(err)` | Create already-rejected task |

---

### `process`

| Function | Description |
|----------|-------------|
| `pid()` | Current process ID |
| `run(cmd)` | Run shell command; returns map with `ok`, `stdout`, `code` |

```xs
import process
let r = process.run("echo hello")
println(r["stdout"])             -- hello
println(r["ok"])                 -- true
println(r["code"])               -- 0
```

---

### `thread`

| Function | Description |
|----------|-------------|
| `spawn(fn)` | Spawn a thread |
| `id()` | Current thread ID |
| `cpu_count()` | Number of CPU cores |
| `sleep(secs)` | Sleep current thread |

---

### `buf`

Binary buffer for low-level I/O.

| Function | Description |
|----------|-------------|
| `new(cap)` | Create buffer with initial capacity |
| `write_u8(v)` | Append a byte |
| `read_u8()` | Read a byte |
| `to_str()` | Convert buffer to string |
| `to_hex()` | Convert buffer to hex string |
| `len()` | Buffer length |

---

### `db`

Embedded database (SQLite-style).

| Function | Description |
|----------|-------------|
| `open(path)` | Open database at path |
| `exec(sql)` | Execute SQL statement |
| `query(sql)` | Execute query, return rows |
| `close()` | Close database |

---

### `gc`

Manual control of the garbage collector.

| Function | Description |
|----------|-------------|
| `collect()` | Trigger collection |
| `disable()` | Disable automatic collection |
| `enable()` | Re-enable automatic collection |
| `stats()` | Return GC statistics map |

---

### `reactive`

Reactive state primitives.

| Function | Description |
|----------|-------------|
| `signal(val)` | Create a reactive signal (observable value) |
| `derived(fn)` | Create a derived signal computed from others |
| `effect(fn)` | Run side effect when dependencies change |
| `batch(fn)` | Batch multiple signal updates |

These are also available as top-level builtins: `signal(val)` and `derived(fn)`.

---

### `fs`

Additional filesystem operations (mirrors much of `io`).

---

### `cli`

Command-line argument parsing utilities.

---

### `ffi`

Foreign function interface for calling native C code.

---

## Execution Backends

```bash
xs script.xs                     -- tree-walker interpreter (default)
xs --vm script.xs                -- bytecode VM (faster)
xs --jit script.xs               -- JIT compilation
xs build script.xs               -- compile to bytecode (.xsc)
xs run script.xsc                -- run compiled bytecode
```

Both the interpreter and VM produce identical results for correct programs. The VM is faster for compute-heavy code.

The `build` command compiles to a `.xsc` file that can be distributed and run without the source.

---

## CLI Commands

```bash
xs <file.xs>                     -- run a script
xs run <file.xs|file.xsc>        -- run source or compiled bytecode
xs repl                          -- interactive REPL
xs test [pattern]                -- run test files matching pattern
xs check <file.xs>               -- type-check only, no execution
xs build <file.xs> [-o out.xsc]  -- compile to bytecode
xs lint [file|dir] [--fix]       -- lint source files
xs fmt [file|dir] [--check]      -- format source (--check to just verify)
xs doc [dir]                     -- generate documentation
xs transpile --target <js|c|wasm32|wasi> <file.xs>
xs new <name>                    -- scaffold a new project
xs lsp [-s <lsp.xs>]             -- start LSP server
xs dap                           -- start DAP debug server
xs replay <trace.xst>            -- replay a recorded execution trace
```

**Flags:**

| Flag | Description |
|------|-------------|
| `--vm` | Use bytecode VM backend |
| `--jit` | Use JIT backend |
| `--check` | Type-check without running |
| `--strict` | Require type annotations everywhere |
| `--lenient` | Skip some static checks |
| `--optimize` | Enable optimizations |
| `--watch` | Re-run on file changes |
| `--no-color` | Disable colored output |
| `-e <code>` | Evaluate code inline |
| `--emit <bytecode\|ast\|ir\|js\|c\|wasm>` | Print intermediate form |
| `--record <file.xst>` | Record execution trace |
| `--trace-deep` | Serialize complex values as JSON in traces |
| `--plugin <path>` | Load a plugin |
| `--sandbox` | Run in sandboxed mode |

```bash
# quick inline eval
xs -e 'println("hello")'

# check types without running
xs --check mycode.xs

# compile and run
xs build mycode.xs -o mycode.xsc
xs run mycode.xsc

# transpile to JS
xs transpile --target js mycode.xs

# watch mode
xs --watch script.xs
```
