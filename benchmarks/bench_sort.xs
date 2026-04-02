-- sorting benchmark

fn quicksort(arr) {
    if len(arr) <= 1 { return arr }
    let pivot = arr[0]
    var left = []
    var right = []
    var i = 1
    while i < len(arr) {
        if arr[i] < pivot { left.push(arr[i]) }
        else { right.push(arr[i]) }
        i = i + 1
    }
    return quicksort(left).concat([pivot]).concat(quicksort(right))
}

var data = []
var seed = 42
for i in 0..1000 {
    seed = (seed * 1103515245 + 12345) % (2 ** 31)
    data.push(seed % 10000)
}

let sorted = quicksort(data)
assert_eq(len(sorted), 1000)

-- verify sorted
var ok = true
var j = 1
while j < len(sorted) {
    if sorted[j] < sorted[j - 1] { ok = false }
    j = j + 1
}
assert(ok, "array should be sorted")
println("sorted 1000 elements")
