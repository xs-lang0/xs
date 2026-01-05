-- Test assert statement without parentheses

-- Basic assertions
assert true
assert 1 == 1
assert 2 + 2 == 4
assert "hello" == "hello"

-- Assert with comparison operators
assert 5 > 3
assert 3 < 5
assert 5 >= 5
assert 5 <= 5

-- Assert with logical operators
assert true and true
assert true or false
assert not false

-- Assert with variables
let x = 42
assert x == 42
assert x > 0

-- Assert with function results
fn double(n) { n * 2 }
assert double(5) == 10

-- Assert with parens still works
assert(true)
assert(1 + 1 == 2)

-- Assert with custom message
assert true, "this should pass"
assert 1 == 1, "equality check"

println("All assert-no-parens tests passed!")
