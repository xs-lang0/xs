-- tests/test_number_methods.xs — exercise all number methods

-- abs()
assert((-5).abs() == 5, "int abs negative")
assert((3).abs() == 3, "int abs positive")
assert((0).abs() == 0, "int abs zero")
assert((-3.14).abs() == 3.14, "float abs negative")

-- pow(exp)
assert((2).pow(10) == 1024, "int pow")
assert((3).pow(0) == 1, "pow zero")
assert((5).pow(1) == 5, "pow one")

-- sqrt()
assert((9).sqrt() == 3.0, "sqrt 9")
assert((0).sqrt() == 0.0, "sqrt 0")

-- min / max
assert((3).min(5) == 3, "min smaller")
assert((7).min(2) == 2, "min larger")
assert((3).max(5) == 5, "max smaller")
assert((7).max(2) == 7, "max larger")

-- clamp(lo, hi)
assert((5).clamp(1, 10) == 5, "clamp within")
assert((-3).clamp(0, 10) == 0, "clamp below")
assert((20).clamp(0, 10) == 10, "clamp above")

-- floor / ceil / round
assert((3.7).floor() == 3, "floor 3.7")
assert((3.2).ceil() == 4, "ceil 3.2")
assert((3.5).round() == 4, "round 3.5")
assert((3.4).round() == 3, "round 3.4")
assert((-2.7).floor() == -3, "floor -2.7")
assert((-2.2).ceil() == -2, "ceil -2.2")

-- to_string / to_str
assert((42).to_string() == "42", "to_string int")
assert((42).to_str() == "42", "to_str int")

-- to_char
assert((65).to_char() == "A", "to_char 65 -> A")
assert((97).to_char() == "a", "to_char 97 -> a")

-- is_even / is_odd
assert((4).is_even() == true, "4 is_even")
assert((3).is_even() == false, "3 not is_even")
assert((3).is_odd() == true, "3 is_odd")
assert((4).is_odd() == false, "4 not is_odd")
assert((0).is_even() == true, "0 is_even")

-- is_nan / is_inf
assert((1.0).is_nan() == false, "1.0 not nan")
assert((1.0).is_inf() == false, "1.0 not inf")
assert((42).is_nan() == false, "int not nan")
assert((42).is_inf() == false, "int not inf")

-- sign()
assert((5).sign() == 1, "sign positive")
assert((-3).sign() == -1, "sign negative")
assert((0).sign() == 0, "sign zero")
assert((2.5).sign() == 1, "sign float positive")
assert((-0.5).sign() == -1, "sign float negative")

-- digits()
assert((123).digits() == [1, 2, 3], "digits 123")
assert((0).digits() == [0], "digits 0")
assert((9).digits() == [9], "digits single")
assert((1000).digits() == [1, 0, 0, 0], "digits 1000")
assert((-42).digits() == [4, 2], "digits negative abs")

-- to_hex()
assert((255).to_hex() == "0xff", "to_hex 255")
assert((0).to_hex() == "0x0", "to_hex 0")
assert((16).to_hex() == "0x10", "to_hex 16")
assert((-1).to_hex() == "-0x1", "to_hex -1")

-- to_bin()
assert((10).to_bin() == "0b1010", "to_bin 10")
assert((0).to_bin() == "0b0", "to_bin 0")
assert((1).to_bin() == "0b1", "to_bin 1")
assert((255).to_bin() == "0b11111111", "to_bin 255")
assert((-5).to_bin() == "-0b101", "to_bin -5")

-- to_oct()
assert((8).to_oct() == "0o10", "to_oct 8")
assert((0).to_oct() == "0o0", "to_oct 0")
assert((255).to_oct() == "0o377", "to_oct 255")
assert((-8).to_oct() == "-0o10", "to_oct -8")

-- to_int / to_float
assert((3.9).to_int() == 3, "to_int from float")
assert((5).to_float() == 5.0, "to_float from int")

println("All number method tests passed!")
