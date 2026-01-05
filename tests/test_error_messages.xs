-- Verify defined functions work fine
fn test_defined() { 42 }
assert(test_defined() == 42, "defined function works")
println("Error message test passed!")
