let double = |x| x * 2
assert(double(5) == 10, "arrow lambda")

let add = |a, b| a + b
assert(add(3, 4) == 7, "multi-param arrow lambda")

let arr = [1, 2, 3]
let doubled = arr.map(|x| x * 2)
assert(doubled == [2, 4, 6], "arrow lambda in map")

println("All arrow lambda tests passed!")
