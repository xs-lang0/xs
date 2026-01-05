-- Test argv exists and is an array
let args = argv
assert(args is array, "argv is array")
println("argv length: {len(args)}")

-- argv contains only extra arguments (not the script name)
-- When run as: ./xs script.xs arg1 arg2
-- argv == ["arg1", "arg2"]
if len(args) >= 2 {
    assert(args[0] == "arg1", "first arg is arg1")
    assert(args[1] == "arg2", "second arg is arg2")
    println("argv[0] = {args[0]}")
    println("argv[1] = {args[1]}")
}

-- argv is empty when no extra arguments are passed
-- (we test this by checking it's at least an array)

-- Test semicolons as statement separators
let a = 1; let b = 2; let c = a + b
assert(c == 3, "semicolons work as separators")

-- Multiple statements on one line
let x = 10; let y = 20; assert(x + y == 30, "inline statements")

-- Semicolons inside blocks
if true { let p = 1; let q = 2; assert(p + q == 3, "semicolons in if block") }

-- Semicolons with function definitions
fn add(a, b) { return a + b }; fn mul(a, b) { return a * b }
assert(add(2, 3) == 5, "fn after semicolon"); assert(mul(2, 3) == 6, "fn after semicolon 2")

-- Semicolons with while
var i = 0; while i < 3 { i = i + 1 }; assert(i == 3, "semicolon after while")

-- Semicolons with for
var sum = 0; for n in [1, 2, 3] { sum = sum + n }; assert(sum == 6, "semicolon after for")

-- Multiple semicolons (should be harmless)
let z = 42;;; assert(z == 42, "extra semicolons ok")

println("argv and semicolons test passed!")
