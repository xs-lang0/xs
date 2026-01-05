let tup = (10, "hello", true)
assert(tup.0 == 10, "tuple.0")
assert(tup.1 == "hello", "tuple.1")
assert(tup.2 == true, "tuple.2")

-- Index access should also work
assert(tup[0] == 10, "tuple[0]")
assert(tup[1] == "hello", "tuple[1]")

-- Nested tuples
let nested = ((1, 2), (3, 4))
assert(nested.0.0 == 1, "nested tuple")
assert(nested.1.1 == 4, "nested tuple 2")

println("Tuple access tests passed!")
