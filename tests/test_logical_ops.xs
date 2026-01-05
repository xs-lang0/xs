-- Test word-form operators
assert(true and true, "and true")
assert(!(true and false), "and false")
assert(true or false, "or true")
assert(!(false or false), "or false")
assert(not false, "not false")
assert(!(not true), "not true")

-- Test symbol-form operators
assert(true && true, "&&  true")
assert(!(true && false), "&& false")
assert(true || false, "|| true")
assert(!(false || false), "|| false")
assert(!false, "! false")

-- Test mixing (should work)
assert(true and true || false, "mixed 1")
assert(not false && true, "mixed 2")

println("Logical operators test passed!")
