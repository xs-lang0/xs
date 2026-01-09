-- core language: arithmetic, variables, types, operators

-- arithmetic
assert_eq(2 + 3, 5)
assert_eq(10 - 4, 6)
assert_eq(3 * 7, 21)
assert_eq(10 / 2, 5)
assert_eq(10 % 3, 1)
assert_eq(2 ** 10, 1024)
assert_eq(7 // 2, 3)
assert_eq(-7 // 2, -4)
assert_eq(-5, -5)
assert_eq(1 + 2 * 3, 7)
assert_eq((1 + 2) * 3, 9)

-- float arithmetic
assert(1.5 + 2.5 == 4.0, "float add")
assert(3.0 / 2.0 == 1.5, "float div")
assert(2.0 ** 0.5 > 1.41, "sqrt-ish")

-- mixed int/float
assert(1 + 0.5 == 1.5, "mixed add")

-- let/var/const
let x = 42
assert_eq(x, 42)
var y = 10
y = 20
assert_eq(y, 20)
const MAX = 100
assert_eq(MAX, 100)

-- comparisons
assert(5 == 5, "eq")
assert(5 != 3, "neq")
assert(3 < 5, "lt")
assert(5 > 3, "gt")
assert(5 <= 5, "le")
assert(5 >= 5, "ge")

-- spaceship
assert_eq(5 <=> 3, 1)
assert_eq(3 <=> 5, -1)
assert_eq(5 <=> 5, 0)

-- logical
assert(true && true, "and tt")
assert(!(true && false), "and tf")
assert(true || false, "or tf")
assert(!false, "not")
assert(true and true, "word and")
assert(false or true, "word or")
assert(not false, "word not")

-- null coalesce
assert_eq(null ?? 42, 42)
assert_eq(10 ?? 99, 10)

-- bitwise
assert_eq(0xFF & 0x0F, 15)
assert_eq(0x0F | 0xF0, 255)
assert_eq(0x0F ^ 0xFF, 0xF0)
assert_eq(1 << 3, 8)
assert_eq(8 >> 2, 2)

-- type function
assert_eq(type(42), "int")
assert_eq(type(3.14), "float")
assert_eq(type("hi"), "str")
assert_eq(type(true), "bool")
assert_eq(type(null), "null")
assert_eq(type([]), "array")
assert_eq(type(#{}), "map")
assert_eq(type((1,2)), "tuple")

-- is operator
assert(42 is int, "is int")
assert("hi" is str, "is str")
assert(3.14 is float, "is float")
assert(true is bool, "is bool")

-- numeric literals
assert_eq(0xFF, 255)
assert_eq(0b1010, 10)
-- assert_eq(0o17, 15) -- TODO: octal literals broken in lexer
assert_eq(1_000_000, 1000000)

-- division by zero
let d = 10 / 0
assert_eq(d, null)
let m = 10 % 0
assert_eq(m, null)

println("test_core: all passed")
