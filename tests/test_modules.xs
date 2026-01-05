module utils {
    fn double(x) { x * 2 }
    fn square(x) { x * x }
}

assert(utils.double(5) == 10, "module function")
assert(utils.square(4) == 16, "module function 2")

println("Module block test passed!")
