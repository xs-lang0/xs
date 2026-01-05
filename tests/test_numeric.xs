-- === Integer division (/) truncates toward zero ===
assert(7 / 3 == 2, "int division 7/3 == 2")
assert((-7) / 3 == -2, "int division (-7)/3 == -2 (truncates toward zero)")
assert(7 / (-3) == -2, "int division 7/(-3) == -2")
assert((-7) / (-3) == 2, "int division (-7)/(-3) == 2")

-- === Floor division (//) ===
assert(7 // 3 == 2, "floor div 7//3 == 2")
assert((-7) // 3 == -3, "floor div (-7)//3 == -3")

-- === Float to int cast truncates toward zero ===
let f1 = 3.9
let f2 = -3.9
let f3 = 3.1
let f4 = -3.1
assert(int(f1) == 3, "int(3.9) == 3")
assert(int(f2) == -3, "int(-3.9) == -3 (truncates toward zero)")
assert(int(f3) == 3, "int(3.1) == 3")
assert(int(f4) == -3, "int(-3.1) == -3")

-- === Modulo uses mathematical semantics (result sign matches divisor) ===
assert(7 % 3 == 1, "7 % 3 == 1")
assert((-7) % 3 == 2, "(-7) % 3 == 2 (sign matches divisor)")
assert(7 % (-3) == -2, "7 % (-3) == -2 (sign matches divisor)")

-- === Large integers (i64) ===
let big = 9999999999999
assert(big > 0, "large int works")
assert(big == 9999999999999, "large int value preserved")

-- === Integer overflow is now detected and reported ===
let max_i64 = 9223372036854775807
assert(max_i64 > 0, "i64 max is positive")

println("Numeric behavior test passed!")
