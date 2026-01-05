-- Test operator overloading via impl blocks
--
-- XS supports operator overloading: define a method named after the operator
-- (e.g., fn +(self, other)) inside an impl block for a class. The interpreter's
-- eval_binop checks if the left operand is an XS_INST with a method matching
-- the operator string, and dispatches to it.
--
-- Supported operators for overloading: +, -, /, %, **, <, >, <=, >=, &&, ||, ++
--
-- KNOWN LIMITATIONS:
-- 1. fn * cannot be overloaded because `fn *` is parsed as a generator marker
--    (fn*), consuming the `*` token before the operator-name check. (parser.c:2124)
-- 2. == and != cannot be overloaded because eval_binop handles equality via
--    value_equal() *before* checking for user-defined operator methods.
--    Custom == on classes is silently ignored. (interp.c:2787)

class Vec2 {
    x = 0
    y = 0
    fn init(self, x, y) {
        self.x = x
        self.y = y
    }
}

impl Vec2 {
    fn +(self, other) {
        return Vec2(self.x + other.x, self.y + other.y)
    }

    fn -(self, other) {
        return Vec2(self.x - other.x, self.y - other.y)
    }

    fn /(self, other) {
        return Vec2(self.x / other.x, self.y / other.y)
    }

    fn <(self, other) {
        return (self.x * self.x + self.y * self.y) < (other.x * other.x + other.y * other.y)
    }
}

let v1 = Vec2(1, 2)
let v2 = Vec2(3, 4)

-- Test basic class field access
assert(v1.x == 1, "v1.x should be 1")
assert(v1.y == 2, "v1.y should be 2")
assert(v2.x == 3, "v2.x should be 3")
assert(v2.y == 4, "v2.y should be 4")

-- Test + operator overload
let v3 = v1 + v2
assert(v3.x == 4, "v1 + v2: x should be 4")
assert(v3.y == 6, "v1 + v2: y should be 6")

-- Test - operator overload
let v4 = v2 - v1
assert(v4.x == 2, "v2 - v1: x should be 2")
assert(v4.y == 2, "v2 - v1: y should be 2")

-- Test / operator overload
let v5 = Vec2(10, 20) / Vec2(2, 5)
assert(v5.x == 5, "Vec2(10,20) / Vec2(2,5): x should be 5")
assert(v5.y == 4, "Vec2(10,20) / Vec2(2,5): y should be 4")

-- Test < operator overload (magnitude comparison)
assert(v1 < v2, "Vec2(1,2) < Vec2(3,4) by magnitude")

-- Test chaining: (v1 + v2) + v1
let v6 = v1 + v2 + v1
assert(v6.x == 5, "v1 + v2 + v1: x should be 5")
assert(v6.y == 8, "v1 + v2 + v1: y should be 8")

-- Test chaining via intermediate variable
let tmp = v1 + v2
let v7 = tmp + v1
assert(v7.x == 5, "chained via var: x should be 5")
assert(v7.y == 8, "chained via var: y should be 8")

-- Test mixed operations
let v8 = v1 + v2 - Vec2(1, 1)
assert(v8.x == 3, "v1 + v2 - (1,1): x should be 3")
assert(v8.y == 5, "v1 + v2 - (1,1): y should be 5")

println("Operator overload test passed!")
