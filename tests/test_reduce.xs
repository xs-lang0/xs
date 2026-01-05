let arr = [1, 2, 3, 4, 5]
let sum = arr.reduce(|acc, x| acc + x, 0)
assert(sum == 15, "reduce sum")

let product = arr.fold(1, |acc, x| acc * x)
assert(product == 120, "fold product")

println("All reduce/fold tests passed!")
