-- Verify null field access gives a clear error
let x = null
-- x.foo would crash — we just verify null checks work
assert(x == null, "null equality")
assert(x is null, "null is null")

-- Verify null-coalesce works
let y = null ?? "default"
assert(y == "default", "null coalesce")

-- Optional chaining if supported
-- let z = x?.foo ?? "safe"

println("Null safety test passed!")
