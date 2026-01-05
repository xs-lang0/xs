fn greet(name) { "Hello, " ++ name }
let x = "{greet("world")}"
println(x)
assert(x == "Hello, world", "nested quotes in interpolation")

let z = "\{literal\}"
assert(z == "\{literal\}", "escaped braces")

println("All string interpolation tests passed!")
