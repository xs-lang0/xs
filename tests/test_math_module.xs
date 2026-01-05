-- Test math module: all functions and constants from XS Language Reference Section 26

import math

var passed = 0
var failed = 0

fn approx(a, b) {
    let diff = abs(a - b)
    if diff < 0.0001 {
        return true
    }
    return false
}

fn assert_approx(actual, expected, label) {
    if approx(actual, expected) {
        passed = passed + 1
    } else {
        print("FAIL: " ++ label ++ " expected=" ++ str(expected) ++ " got=" ++ str(actual))
        failed = failed + 1
    }
}

fn assert_eq(actual, expected, label) {
    if actual == expected {
        passed = passed + 1
    } else {
        print("FAIL: " ++ label ++ " expected=" ++ str(expected) ++ " got=" ++ str(actual))
        failed = failed + 1
    }
}

fn assert_true(val, label) {
    if val {
        passed = passed + 1
    } else {
        print("FAIL: " ++ label ++ " expected true")
        failed = failed + 1
    }
}

fn assert_false(val, label) {
    if !val {
        passed = passed + 1
    } else {
        print("FAIL: " ++ label ++ " expected false")
        failed = failed + 1
    }
}

-- ============================
-- Constants
-- ============================
assert_approx(math.pi, 3.14159265358979, "math.pi")
assert_approx(math.e, 2.71828182845904, "math.e")
assert_approx(math.tau, 6.28318530717958, "math.tau")
assert_true(math.is_inf(math.inf), "math.inf is infinity")
assert_true(math.is_nan(math.nan), "math.nan is NaN")

-- uppercase aliases
assert_approx(math.PI, 3.14159265358979, "math.PI")
assert_approx(math.E, 2.71828182845904, "math.E")
assert_approx(math.TAU, 6.28318530717958, "math.TAU")

-- ============================
-- Trigonometric functions
-- ============================
assert_approx(math.sin(0.0), 0.0, "sin(0)")
assert_approx(math.sin(math.pi / 2.0), 1.0, "sin(pi/2)")
assert_approx(math.cos(0.0), 1.0, "cos(0)")
assert_approx(math.cos(math.pi), -1.0, "cos(pi)")
assert_approx(math.tan(0.0), 0.0, "tan(0)")
assert_approx(math.asin(1.0), math.pi / 2.0, "asin(1)")
assert_approx(math.acos(1.0), 0.0, "acos(1)")
assert_approx(math.atan(0.0), 0.0, "atan(0)")
assert_approx(math.atan2(1.0, 1.0), math.pi / 4.0, "atan2(1,1)")

-- Hyperbolic
assert_approx(math.sinh(0.0), 0.0, "sinh(0)")
assert_approx(math.cosh(0.0), 1.0, "cosh(0)")
assert_approx(math.tanh(0.0), 0.0, "tanh(0)")

-- Inverse hyperbolic
assert_approx(math.asinh(0.0), 0.0, "asinh(0)")
assert_approx(math.acosh(1.0), 0.0, "acosh(1)")
assert_approx(math.atanh(0.0), 0.0, "atanh(0)")

-- ============================
-- Exponents and logarithms
-- ============================
assert_approx(math.sqrt(4.0), 2.0, "sqrt(4)")
assert_approx(math.cbrt(27.0), 3.0, "cbrt(27)")
assert_approx(math.exp(0.0), 1.0, "exp(0)")
assert_approx(math.exp(1.0), math.e, "exp(1)")
assert_approx(math.expm1(0.0), 0.0, "expm1(0)")
assert_approx(math.log(math.e), 1.0, "log(e)")
assert_approx(math.log2(8.0), 3.0, "log2(8)")
assert_approx(math.log10(1000.0), 3.0, "log10(1000)")
assert_approx(math.log1p(0.0), 0.0, "log1p(0)")

-- ============================
-- Rounding
-- ============================
assert_approx(math.floor(3.7), 3.0, "floor(3.7)")
assert_approx(math.floor(-3.2), -4.0, "floor(-3.2)")
assert_approx(math.ceil(3.2), 4.0, "ceil(3.2)")
assert_approx(math.ceil(-3.7), -3.0, "ceil(-3.7)")
assert_approx(math.round(3.5), 4.0, "round(3.5)")
assert_approx(math.trunc(3.7), 3.0, "trunc(3.7)")
assert_approx(math.trunc(-3.7), -3.0, "trunc(-3.7)")

-- ============================
-- Utility functions
-- ============================
assert_approx(math.abs(-5.0), 5.0, "abs(-5)")
assert_approx(math.pow(2.0, 10.0), 1024.0, "pow(2,10)")
assert_approx(math.hypot(3.0, 4.0), 5.0, "hypot(3,4)")
assert_eq(math.gcd(12, 8), 4, "gcd(12,8)")
assert_eq(math.lcm(4, 6), 12, "lcm(4,6)")
assert_eq(math.factorial(5), 120, "factorial(5)")
assert_eq(math.factorial(0), 1, "factorial(0)")

-- is_nan / is_inf
-- 0.0/0.0 is now a runtime error, so test is_nan with math.nan instead
assert_true(math.is_nan(math.nan), "is_nan(math.nan)")
assert_false(math.is_nan(1.0), "is_nan(1.0)")
assert_true(math.is_inf(math.inf), "is_inf(inf)")
assert_false(math.is_inf(1.0), "is_inf(1.0)")

-- clamp / lerp / sign
assert_approx(math.clamp(5.0, 0.0, 10.0), 5.0, "clamp(5,0,10)")
assert_approx(math.clamp(-1.0, 0.0, 10.0), 0.0, "clamp(-1,0,10)")
assert_approx(math.clamp(15.0, 0.0, 10.0), 10.0, "clamp(15,0,10)")
assert_approx(math.lerp(0.0, 10.0, 0.5), 5.0, "lerp(0,10,0.5)")
assert_eq(math.sign(5.0), 1, "sign(5)")
assert_eq(math.sign(-3.0), -1, "sign(-3)")
assert_eq(math.sign(0.0), 0, "sign(0)")

-- degrees / radians
assert_approx(math.degrees(math.pi), 180.0, "degrees(pi)")
assert_approx(math.radians(180.0), math.pi, "radians(180)")

-- fmod
assert_approx(math.fmod(10.0, 3.0), 1.0, "fmod(10,3)")

-- modf
let mparts = math.modf(3.75)
assert_approx(mparts[0], 3.0, "modf(3.75) integer part")
assert_approx(mparts[1], 0.75, "modf(3.75) fractional part")

-- copysign
assert_approx(math.copysign(1.0, -1.0), -1.0, "copysign(1,-1)")
assert_approx(math.copysign(-1.0, 1.0), 1.0, "copysign(-1,1)")

-- isclose
assert_true(math.isclose(1.0, 1.0000000001), "isclose(1.0, 1.0+eps)")
assert_false(math.isclose(1.0, 2.0), "isclose(1.0, 2.0)")

-- frexp
let fr = math.frexp(8.0)
assert_approx(fr[0], 0.5, "frexp(8) mantissa")
assert_eq(fr[1], 4, "frexp(8) exponent")

-- ldexp
assert_approx(math.ldexp(0.5, 4), 8.0, "ldexp(0.5, 4)")

-- ============================
-- Combinatorial
-- ============================
assert_eq(math.comb(5, 2), 10, "comb(5,2)")
assert_eq(math.comb(10, 3), 120, "comb(10,3)")
assert_eq(math.perm(5, 2), 20, "perm(5,2)")
assert_eq(math.perm(5), 120, "perm(5) = 5!")

-- ============================
-- Aggregate
-- ============================
assert_approx(math.prod([1.0, 2.0, 3.0, 4.0]), 24.0, "prod([1,2,3,4])")
assert_approx(math.sum([1.0, 2.0, 3.0, 4.0]), 10.0, "sum([1,2,3,4])")
assert_approx(math.min([3.0, 1.0, 4.0, 1.5]), 1.0, "min([3,1,4,1.5])")
assert_approx(math.max([3.0, 1.0, 4.0, 1.5]), 4.0, "max([3,1,4,1.5])")
assert_approx(math.mean([2.0, 4.0, 6.0, 8.0]), 5.0, "mean([2,4,6,8])")

-- ============================
-- Special functions
-- ============================
assert_approx(math.erf(0.0), 0.0, "erf(0)")
assert_approx(math.erfc(0.0), 1.0, "erfc(0)")
assert_approx(math.gamma(5.0), 24.0, "gamma(5) = 4!")
assert_approx(math.lgamma(1.0), 0.0, "lgamma(1)")

-- ============================
-- Summary
-- ============================
print("")
print("Math module tests: " ++ str(passed) ++ " passed, " ++ str(failed) ++ " failed")
if failed == 0 {
    print("ALL TESTS PASSED")
} else {
    print("SOME TESTS FAILED")
}
