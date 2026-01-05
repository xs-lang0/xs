let arr = [3, 1, 4, 1, 5]
let s = arr.sorted()
assert(s == [1, 1, 3, 4, 5], "default sort")

let desc = arr.sorted(|a, b| b - a)
assert(desc == [5, 4, 3, 1, 1], "sort descending by comparator")

let words = ["banana", "apple", "cherry"]
let by_len = words.sort_by(|w| len(w))
assert(by_len == ["apple", "banana", "cherry"], "sort by key")

println("All sorted tests passed!")
