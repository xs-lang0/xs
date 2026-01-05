let arr = [1, 2, 3, 4, 5]
assert(6 not in arr, "6 not in array")
assert(!(1 not in arr), "1 is in array")

let m = {"a": 1, "b": 2}
assert("c" not in m, "c not in map")
assert(!("a" not in m), "a is in map")

let s = "hello world"
assert("xyz" not in s, "xyz not in string")
assert(!("hello" not in s), "hello is in string")

println("All 'not in' tests passed!")
