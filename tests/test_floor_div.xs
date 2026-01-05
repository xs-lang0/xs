-- Floor division tests (// is floor division, -- is line comment)
assert(7 // 3 == 2, "floor div positive")
assert((-7) // 3 == -3, "floor div negative rounds down")
assert(10 // 3 == 3, "floor div 10/3")
assert((-10) // 3 == -4, "floor div -10/3 rounds down")
assert(7.0 // 2.0 == 3.0, "floor div float")

-- Verify regular division still works
assert(7 / 3 == 2, "int division truncates")
assert(10 / 3 == 3, "int division")

-- Verify block comments still work
{- This is a block comment -}
let x = 42
assert(x == 42, "block comments work")

println("Floor division tests passed!")
