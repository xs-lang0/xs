let a = false
let b = true
let x = if a { 1 } elif b { 2 } else { 3 }
assert(x == 2, "elif in expression position")

let y = if false { "a" } elif false { "b" } elif true { "c" } else { "d" }
assert(y == "c", "multiple elif in expression")

fn classify(n) {
    if n < 0 { "negative" } elif n == 0 { "zero" } else { "positive" }
}
assert(classify(-1) == "negative", "classify -1")
assert(classify(0) == "zero", "classify 0")
assert(classify(1) == "positive", "classify 1")

println("All elif expression tests passed!")
