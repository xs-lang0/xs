-- Test: Integer division (truncate toward zero — C behavior for /)
assert(7 / 3 == 2, "int div positive")
assert((-7) / 3 == -2, "int div negative truncates toward zero")
assert(7 / (-3) == -2, "int div negative divisor truncates toward zero")

-- Test: Modulo (mathematical — result sign matches divisor)
assert(7 % 3 == 1, "mod positive")
assert((-7) % 3 == 2, "mod negative numerator gives positive result")
assert(7 % (-3) == -2, "mod negative divisor gives negative result")
assert((-7) % (-3) == -1, "mod both negative")

-- Test: Float division
assert(7.0 / 2.0 == 3.5, "float division")

-- Test: Basic arithmetic still works
assert(3 + 4 == 7, "addition")
assert(10 - 3 == 7, "subtraction")
assert(6 * 7 == 42, "multiplication")

println("Numeric semantics tests passed!")
