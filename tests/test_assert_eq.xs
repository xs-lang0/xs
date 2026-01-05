assert_eq(1, 1)
assert_eq("hello", "hello")
assert_eq([1, 2, 3], [1, 2, 3])
assert_eq(true, true)

-- Test with message
assert_eq(42, 42, "forty-two")

println("All assert_eq tests passed!")
