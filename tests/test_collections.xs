-- arrays, tuples, maps, ranges

-- arrays
let a = [1, 2, 3]
assert_eq(len(a), 3)
assert_eq(a[0], 1)
assert_eq(a[2], 3)

var b = [10, 20]
b.push(30)
assert_eq(len(b), 3)
assert_eq(b[2], 30)
assert_eq(b.pop(), 30)
assert_eq(len(b), 2)

-- array methods
assert_eq([3, 1, 2].sort(), [1, 2, 3])
assert_eq([1, 2, 3].reverse(), [3, 2, 1])
assert_eq([1, 2, 3].contains(2), true)
assert_eq([1, 2, 3].contains(5), false)
assert_eq([1, 2, 3].first(), 1)
assert_eq([1, 2, 3].last(), 3)
assert_eq([1, 2, 3].slice(1, 3), [2, 3])
assert_eq([1, 2, 3].index_of(2), 1)

-- higher-order array methods
assert_eq([1, 2, 3].map(fn(x) { x * 2 }), [2, 4, 6])
assert_eq([1, 2, 3, 4].filter(fn(x) { x > 2 }), [3, 4])
assert_eq([1, 2, 3].reduce(fn(acc, x) { acc + x }, 0), 6)
assert([1, 2, 3].any(fn(x) { x > 2 }), "any true")
assert(![1, 2, 3].any(fn(x) { x > 5 }), "any false")
assert([2, 4, 6].all(fn(x) { x % 2 == 0 }), "all true")

-- list comprehensions
assert_eq([x * x for x in 0..5], [0, 1, 4, 9, 16])
assert_eq([x for x in 0..10 if x % 2 == 0], [0, 2, 4, 6, 8])

-- spread
let c = [1, 2, 3]
let d = [...c, 4, 5]
assert_eq(d, [1, 2, 3, 4, 5])

-- nested arrays
let nested = [[1, 2], [3, 4]]
assert_eq(nested[0][1], 2)
assert_eq(nested[1][0], 3)

-- tuples
let t = (1, "hello", true)
assert_eq(t.0, 1)
assert_eq(t.1, "hello")
assert_eq(t.2, true)
assert_eq(len(t), 3)

-- maps
let m = #{"a": 1, "b": 2, "c": 3}
assert_eq(m["a"], 1)
assert_eq(m["c"], 3)
assert_eq(len(m), 3)
assert(m.keys().contains("a"), "map keys")
assert(m.values().contains(2), "map values")

-- ranges
let r = 0..5
assert_eq(len(r), 5)
var sum = 0
for i in r { sum = sum + i }
assert_eq(sum, 10)

-- inclusive range
var sum2 = 0
for i in 1..=5 { sum2 = sum2 + i }
assert_eq(sum2, 15)

-- in operator
assert(2 in [1, 2, 3], "in array")
assert(!(5 in [1, 2, 3]), "not in array")
assert("a" in #{"a": 1}, "in map")
assert("ell" in "hello", "in string")
assert(3 in 1..5, "in range")

println("test_collections: all passed")
