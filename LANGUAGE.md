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

Block comments nest properly, so you can comment out code that already contains block comments.

---

## Statement Separators

Newlines and semicolons both separate statements. Use whichever reads better.

```xs
let a = 1
let b = 2

let x = 10; let y = 20; let z = x + y

-- semicolons work everywhere
if true { let p = 1; let q = 2 }
fn add(a, b) { return a + b }; fn mul(a, b) { return a * b }

-- extra semicolons are harmless
let val = 42;;;
```

---

## Variables

```xs
let x = 42          -- immutable binding (cannot reassign)
var y = "hello"     -- mutable binding (can reassign with =)
const MAX = 100     -- compile-time constant (same as let at runtime)

-- with type annotations
let x: int = 42
var name: str = "XS"
```

`let` bindings cannot be reassigned — that's a semantic error. Use `var` when you need mutation.

`const` is identical to `let` at runtime. It signals intent that this is a compile-time constant.

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

Array destructuring requires an exact length match (no rest pattern in `let`).

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
```

---

## Numeric Literals

```xs
42                  -- decimal integer
0xFF                -- hexadecimal (255)
0b1010              -- binary (10)
0o17                -- octal (15)
1_000_000           -- underscores as visual separators (1000000)

3.14                -- float
1e3                 -- scientific notation (1000.0, type is float)
2.5e-3              -- 0.0025
```

Integers are signed 64-bit (`int64_t`). Overflow wraps silently via two's complement:

```xs
let max = 9223372036854775807   -- 2^63 - 1
let wrapped = max + 1           -- -9223372036854775808
```

Floats are IEEE 754 double-precision.

There is no bigint type.

---

## Strings

Both single and double quotes create strings. Both support interpolation and escape sequences — they're identical.

```xs
let s = "hello world"
let s2 = 'also a string'
```

### Interpolation

Expressions inside `{braces}` are evaluated and embedded in the string.

```xs
let name = "XS"
println("Hello, {name}!")        -- Hello, XS!
println("{1 + 2} is three")      -- 3 is three
println("{name} has {len(name)} chars")  -- XS has 2 chars

-- escape the brace to get a literal {
println("\{not interpolated}")   -- {not interpolated}
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

### Raw Strings

No escape processing, no interpolation.

```xs
let pattern = r"\d+\.\d+"
println(pattern)                 -- \d+\.\d+

let x = 42
let raw = r"no {x} here \n raw"
println(raw)                     -- no {x} here \n raw
```

Triple-quoted raw strings also work: `r"""..."""`

### Color Strings

Embed ANSI terminal colors at parse time. Format: `c"style;style;...;text"` — the last segment is the text, everything before it is styling.

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
| 256-color | `fg256,N`, `bg256,N` (N = 0–255) |
| Truecolor | `rgb,R,G,B`, `bgrgb,R,G,B` |

Color strings support interpolation in the text: `c"bold;x = {x}"`.

### String Concatenation

```xs
"hello" ++ " world"             -- "hello world"
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
"".is_empty()                    -- true

-- parsing
"42".parse_int()                 -- 42
"3.14".parse_float()             -- 3.14
"FF".parse_int(16)               -- 255

-- splitting
"hello".split_at(2)              -- ("he", "llo")
"hello".char_at(1)               -- "e"

-- truncation (total length including suffix)
"long text".truncate(7, "...")    -- "long..."
"long text".truncate(4)           -- "l..."

-- joining (called on the separator)
",".join(["a", "b", "c"])        -- "a,b,c"
```

**`.len()` counts bytes**, not Unicode codepoints. For ASCII strings byte count equals character count.

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
arr.sort()                       -- sort in-place
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
arr.find(fn(x) { x > 3 })
arr.any(fn(x) { x > 4 })
arr.all(fn(x) { x > 0 })

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

**Mutating vs non-mutating:** `.reverse()` and `.sort()` modify in-place and return `null`. `.reversed()` and `.sorted()` return a new array, leaving the original alone.

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
m.set("key", "val")              -- set entry
m.delete("key")                  -- remove entry
m.merge(other_map)               -- merge (right side wins on key conflict)

-- spread
let m2 = #{...m, "extra": true}
```

Map literals use `#{}` — the `#` distinguishes them from blocks. Keys can be strings or integers.

---

## Ranges

```xs
0..10                            -- exclusive range (0 through 9)
1..=5                            -- inclusive range (1 through 5)

let r = 0..5
println(type(r))                 -- range
println(len(r))                  -- 5

-- iteration
for i in 0..5 { print("{i} ") } -- 0 1 2 3 4
for i in 1..=3 { print("{i} ") } -- 1 2 3

-- membership test
println(3 in 1..5)               -- true
```

`0..10` does **not** include 10. Use `0..=10` for inclusive.

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

Short-circuit: `and` stops if the left side is falsy; `or` stops if the left side is truthy. The result is the last evaluated operand (not necessarily `true`/`false`).

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
println(42 as float)             -- 42 (type is now float)
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
12. `+` `-` `++` (add, subtract, concat)
13. `*` `/` `%` (multiply, divide, modulo)
14. `**` (power, right-associative)
15. `as` (cast)
16. Unary: `-` `!` `not` `~` (prefix)
17. Postfix: `?` `.` `?.` `[]` `()` (access, call)

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

Braces are always required. No braceless `if`.

`if` works as an expression (returns the value of the taken branch):

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

-- over map entries with destructuring
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

Break or continue an outer loop from a nested one.

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

`match` is an expression — it returns the value of the matched arm.

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

-- variable binding
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

-- @ capture (bind and match simultaneously)
match value {
    n @ 1..=10 => "small: {n}"
    n          => "other: {n}"
}
```

The semantic analyzer checks that `match` covers all possible cases. A wildcard `_` or variable pattern makes any match exhaustive.

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

**Implicit return** only applies to the last expression in the block. For early returns or conditional returns, use `return` explicitly.

### Closures

Closures capture variables **by reference** through an environment chain. Mutations inside a closure are visible to the outer scope and vice versa.

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

Nested closures work — each level captures its parent's environment:

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

Functions can call each other before both are fully defined (they're hoisted):

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

## Structs

```xs
struct Point { x, y }

-- create an instance
let p = Point { x: 10, y: 20 }
println(p.x)                    -- 10
println(p.y)                    -- 20

-- with type annotations
struct Config {
    host: str,
    port: int,
    debug: bool
}
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
```

Methods that access instance data must take `self` as the first parameter. `self` is not implicit.

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
```

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

---

## Type System

XS has gradual typing. Code runs without any annotations, but you can add them for safety.

### Adding Type Annotations

```xs
-- on variables
let count: int = 42
var name: str = "XS"

-- on function parameters and return type
fn add(a: int, b: int) -> int {
    return a + b
}

-- on struct fields
struct Config {
    host: str,
    port: int
}
```

### Available Type Names

`int`, `i32`, `i64`, `float`, `f64`, `str`, `bool`, `null`, `array`, `map`, `tuple`, `fn`

Optional types: `int?` (int or null)

Array types: `[int]`

Function types: `fn(int) -> int`

### Enforcement Behavior

Type annotations are checked by the semantic analyzer. If you have annotations and there's a mismatch:

```xs
let x: int = "hello"            -- error: type mismatch: expected 'int', got 'str'
```

This error fires both with `xs --check` and at normal runtime. The semantic analyzer runs before execution.

Return type mismatches are caught at runtime:

```xs
fn foo(x: int) -> str { return x }
foo(42)                          -- runtime error: expected return type 'str', got 'int'
```

### Type Checking Modes

```bash
xs script.xs            # normal: annotations checked by semantic analyzer
xs --check script.xs    # check-only: type check without running
xs --strict script.xs   # strict: require annotations everywhere
```

### Type Aliases

```xs
type UserId = i64                -- parsed but limited usage currently
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
    println(e)
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
-- output:
-- body
-- first
-- second
-- third
```

Defers run even if an exception is thrown.

### Division by Zero

Division by zero doesn't crash — it prints a runtime warning to stderr and returns `null`:

```xs
let d = 10 / 0                  -- prints warning, d is null
let m = 10 % 0                  -- prints warning, m is null
```

### When to Use What

| Mechanism | Catchable? | Use case |
|-----------|------------|----------|
| `throw expr` | Yes | Recoverable errors: bad input, validation, missing data |
| `panic(msg)` | No | Unrecoverable: invariant violations, impossible states |
| `todo(msg)` | No | Placeholder for unimplemented code |
| `unreachable()` | No | Code that should never execute |

---

## Algebraic Effects

Effects let you "perform" an operation without knowing how it will be handled — the handler decides. Think of it as exceptions you can resume from.

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

-- handle effects
let result = handle greet() {
    Ask.prompt(msg) => resume("World")
}
println(result)                  -- Hello, World!
```

`resume` returns a value to the `perform` site — execution continues from where it left off.

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

---

## Concurrency

### Spawn

`spawn` runs a block as a task. In the current interpreter, it executes **immediately** (cooperative, not preemptive).

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
let m2 = #{...m, "b": 2}        -- {a: 1, b: 2}

-- struct spread (update syntax)
let p2 = Point { ...p, y: 30 }
```

---

## Built-in Functions

### I/O

| Function | Description |
|----------|-------------|
| `println(args...)` | Print with newline. Supports `{}` format placeholders |
| `print(args...)` | Print without trailing newline |
| `eprint(args...)` | Print to stderr without newline |
| `eprintln(args...)` | Print to stderr with newline |
| `input(prompt?)` | Read line from stdin |
| `clear()` | Clear terminal screen |

### Type Checking

| Function | Description |
|----------|-------------|
| `type(val)` | Type name as lowercase string: `"int"`, `"str"`, `"array"`, etc. |
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
println(int(3.9))                -- 3  (toward zero, not rounded)
println(int(-3.9))               -- -3
println(chr(65))                 -- A
println(ord("A"))                -- 65
```

### Math

| Function | Description |
|----------|-------------|
| `abs(x)` | Absolute value |
| `min(a, b)` | Minimum |
| `max(a, b)` | Maximum |
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
| `range(n)` | Range `0..n` |
| `array()` | Empty array |
| `map()` | Empty map |
| `sorted(arr)` | Sorted copy |
| `sum(arr)` | Sum of numeric array |
| `enumerate(arr)` | Array of `(index, value)` tuples |
| `zip(a, b)` | Zip two arrays into tuples |
| `flatten(arr)` | Flatten one level |
| `keys(map)` | Map keys as array |
| `values(map)` | Map values as array |
| `entries(map)` | Map entries as `(key, value)` tuples |
| `chars(str)` | String to array of characters |
| `bytes(str)` | String to array of byte values |
| `contains(coll, val)` | Membership test (for strings/maps — see note) |

```xs
println(enumerate([10, 20, 30]))  -- [(0, 10), (1, 20), (2, 30)]
println(zip([1, 2], [3, 4]))     -- [(1, 3), (2, 4)]
println(range(5))                -- 0..5
```

### Functional

| Function | Description |
|----------|-------------|
| `map(arr, fn)` | Apply fn to each element |
| `filter(arr, fn)` | Keep elements where fn returns true |
| `reduce(arr, fn, init)` | Reduce array to single value |

```xs
println(map([1, 2, 3], fn(x) { x * 2 }))    -- [2, 4, 6]
println(filter([1, 2, 3, 4], fn(x) { x > 2 }))  -- [3, 4]
println(reduce([1, 2, 3], fn(a, b) { a + b }, 0))  -- 6
```

### Debugging and Testing

| Function | Description |
|----------|-------------|
| `assert(cond, msg?)` | Assert truthy. Panics on failure (not catchable) |
| `assert_eq(a, b)` | Assert `a == b`. Shows both values on failure |
| `dbg(val)` | Debug-print to stderr with type info, returns val |
| `pprint(val)` | Pretty-print with indentation |
| `repr(val)` | Debug string representation |
| `panic(msg)` | Print to stderr and exit (not catchable) |
| `todo(msg?)` | Mark unimplemented; panics |
| `unreachable()` | Mark unreachable; panics if reached |
| `exit(code)` | Exit with given code |

### Copying

| Function | Description |
|----------|-------------|
| `copy(val)` / `clone(val)` | Copy a value (shallow for nested structures) |

### Result/Option Constructors

```xs
println(Ok(42))                  -- Ok(42)
println(Err("bad"))              -- Err(bad)
println(Some(10))                -- Some(10)
println(None())                  -- null
```

### String Formatting

```xs
println(format("hello {} you are {}", "world", 42))
-- output: hello world you are 42
```

`sprintf` is an alias for `format`.

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

Import with `import <module>` and access via `module.member()`.

### `math`

**Constants:** `PI`, `E`, `TAU`, `INF`, `NAN`, `inf`, `nan`

**Functions:** `sqrt`, `cbrt`, `pow`, `abs`, `floor`, `ceil`, `round`, `trunc`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sinh`, `cosh`, `tanh`, `exp`, `log`, `log2`, `log10`, `hypot`, `min`, `max`, `clamp`, `lerp`, `sign`, `gcd`, `lcm`, `factorial`, `degrees`, `radians`, `is_nan`, `is_inf`

```xs
import math
println(math.sqrt(16))           -- 4
println(math.gcd(12, 8))         -- 4
println(math.factorial(5))       -- 120
println(math.clamp(15, 0, 10))   -- 10
println(math.sign(-5))           -- -1
println(math.degrees(math.PI))   -- 180
println(math.radians(180))       -- 3.14159
```

### `time`

`now()`, `clock()`, `sleep(secs)`, `sleep_ms(ms)`, `millis()`, `time_since(t)`, `format(t, fmt)`, `parse(s, fmt)`, `year(t)`, `month(t)`, `day(t)`, `hour(t)`, `minute(t)`, `second(t)`, `stopwatch()`

### `io`

`read_file(path)`, `write_file(path, data)`, `append_file(path, data)`, `read_lines(path)`, `read_json(path)`, `write_json(path, data)`, `exists(path)`, `size(path)`, `delete_file(path)`, `copy_file(src, dst)`, `rename_file(old, new)`, `make_dir(path)`, `list_dir(path)`, `is_file(path)`, `is_dir(path)`, `read_line()`, `wait_for_key()`

### `os`

`platform`, `sep`, `pid()`, `args()`, `cwd()`, `chdir(path)`, `home()`, `cpu_count()`, `exit(code)`, `env.get(key)`, `env.set(key, val)`, `env.has(key)`, `env.all()`

```xs
import os
println(os.platform)             -- linux (or macos, windows)
println(os.cwd())                -- current directory
```

### `json`

`parse(str)`, `stringify(val)`, `pretty(val)`, `valid(str)`

```xs
import json
let s = json.stringify(#{"a": 1})
println(s)                       -- {"a":1}
let m = json.parse(s)
println(m)                       -- {a: 1}
```

### `string`

`pad_left(s, n, ch)`, `pad_right(s, n, ch)`, `center(s, n, ch)`, `truncate(s, n)`, `escape_html(s)`, `words(s)`, `levenshtein(a, b)`, `is_numeric(s)`, `camel_to_snake(s)`, `snake_to_camel(s)`

```xs
import string
println(string.words("hello world foo"))    -- [hello, world, foo]
println(string.camel_to_snake("helloWorld")) -- hello_world
println(string.levenshtein("kitten", "sitting"))  -- 3
```

### `path`

`join(parts...)`, `basename(p)`, `dirname(p)`, `ext(p)`, `stem(p)`, `sep`

```xs
import path
println(path.basename("/foo/bar/baz.txt"))  -- baz.txt
println(path.dirname("/foo/bar/baz.txt"))   -- /foo/bar
println(path.ext("/foo/bar/baz.txt"))       -- .txt
```

### `collections`

`Counter(arr)`, `Stack()`, `PriorityQueue()`, `Deque()`, `Set(arr)`, `OrderedMap()`

```xs
import collections
let s = collections.Set([1, 2, 3, 2, 1])
println(s)                       -- Set with unique values
```

### `random`

`int(min, max)`, `float()`, `bool()`, `choice(arr)`, `shuffle(arr)`, `sample(arr, n)`, `seed(n)`

```xs
import random
println(random.int(1, 10))       -- random int between 1 and 10
println(random.float())          -- random float 0.0–1.0
println(random.bool())           -- random boolean
```

### `hash`

`md5(data)`, `sha1(data)`, `sha256(data)`, `sha512(data)`, `hmac(key, data)`

```xs
import hash
println(hash.sha256("hello"))    -- 2cf24dba5fb0a30e26e83b2ac5b9e29e...
```

### `crypto`

`sha256(data)`, `md5(data)`, `random_bytes(n)`, `random_int(min, max)`, `uuid4()`

```xs
import crypto
println(crypto.uuid4())          -- e.g. 8cbe806c-cd27-4d93-afd1-dbfa3f1b4f93
```

### `encode`

`base64_encode(data)`, `base64_decode(data)`, `hex_encode(data)`, `hex_decode(data)`, `url_encode(s)`, `url_decode(s)`

```xs
import encode
println(encode.base64_encode("hello"))  -- aGVsbG8=
println(encode.hex_encode("AB"))        -- 4142
```

### `re`

`match(pattern, str)`, `replace(pattern, str, repl)`, `split(pattern, str)`

```xs
import re
println(re.match("\\d+", "abc 123 def"))     -- 123
println(re.replace("\\d+", "abc 123", "N"))   -- abc N
println(re.split("\\s+", "a b c"))            -- [a, b, c]
```

### `fmt`

`number(n, decimals)`, `hex(n)`, `bin(n)`, `pad(s, n)`, `comma(n)`, `filesize(n)`, `ordinal(n)`, `pluralize(word, n)`

```xs
import fmt
println(fmt.hex(255))            -- 0xff
println(fmt.bin(10))             -- 0b1010
println(fmt.comma(1000000))      -- 1,000,000
```

### `csv`

`parse(str)`, `stringify(rows)`

### `url`

`parse(str)`, `encode(s)`, `decode(s)`

### `reflect`

`type_of(val)`, `fields(val)`, `methods(val)`, `is_instance(val, type)`

### `log`

`debug(msg)`, `info(msg)`, `warn(msg)`, `error(msg)`, `set_level(level)`

### `test`

`assert(cond)`, `assert_eq(a, b)`, `assert_ne(a, b)`, `run(name, fn)`, `summary()`

### `net`

`tcp_connect(host, port)`, `tcp_listen(port)`, `resolve(host)`

### `async`

`spawn(fn)`, `sleep(secs)`, `channel()`, `select(channels)`, `all(tasks)`, `race(tasks)`, `resolve(val)`, `reject(err)`

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

## Numeric Behavior

### Integer Division — Truncation Toward Zero

```xs
println(7 / 3)                   -- 2
println((-7) / 3)                -- -2  (toward zero, NOT -3)
println(7 / (-3))                -- -2
```

### Floor Division — Toward Negative Infinity

```xs
println(5 // 2)                  -- 2
println(-7 // 2)                 -- -4
println(7 // -2)                 -- -4
```

### Modulo — Sign Follows Dividend

```xs
println(7 % 3)                   -- 1
println((-7) % 3)                -- -1  (sign follows -7)
println(7 % (-3))                -- 1   (sign follows 7)
```

### Float-to-Integer Conversion

`int(x)` truncates toward zero:

```xs
println(int(3.9))                -- 3
println(int(-3.9))               -- -3
```

---

## Execution Backends

```bash
xs script.xs                     # tree-walker interpreter (default)
xs --vm script.xs                # bytecode VM (faster)
xs --jit script.xs               # JIT compilation
xs --backend vm script.xs        # same as --vm
```

Both backends produce identical results for correct programs. The VM is faster for compute-heavy code.

---

## Other CLI Commands

```bash
xs run <file.xs>                 # run a script
xs repl                          # interactive REPL
xs test [pattern]                # run tests
xs check <file.xs>               # type-check only
xs lint [file|dir] [--fix]       # lint source files
xs fmt [file|dir] [--check]      # format source
xs doc [dir]                     # generate documentation
xs transpile --target <js|c|wasm32|wasi> <file.xs>
xs new <name>                    # scaffold project
xs lsp                           # LSP server
xs dap                           # DAP debug server
```

**Flags:** `--check`, `--strict`, `--lenient`, `--optimize`, `--watch`, `--no-color`, `-e <code>`, `--emit <bytecode|ast|ir|js|c|wasm>`, `--record <file.xst>`, `--plugin <path>`, `--sandbox`
