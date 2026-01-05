assert(3.14 is float, "3.14 is float")
assert(42 is int, "42 is int")
assert("hello" is str, "hello is str")
assert(true is bool, "true is bool")
assert([1,2] is array, "array is array")
assert(null is null, "null is null")
assert(!(42 is float), "42 is not float")

fn foo() { 1 }
assert(foo is fn, "foo is fn")

println("All 'is' operator tests passed!")
