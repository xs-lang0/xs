-- tests/test_destructuring.xs — comprehensive destructuring tests

-- ===== 1. Basic array destructuring =====
let [a, b, c] = [1, 2, 3]
assert(a == 1, "array destr: a == 1")
assert(b == 2, "array destr: b == 2")
assert(c == 3, "array destr: c == 3")
println("1. Basic array destructuring: PASS")

-- ===== 2. Rest patterns =====
let [head, ..tail] = [10, 20, 30, 40]
assert(head == 10, "rest: head == 10")
assert(len(tail) == 3, "rest: tail has 3 elements")
assert(tail[0] == 20, "rest: tail[0] == 20")
assert(tail[1] == 30, "rest: tail[1] == 30")
assert(tail[2] == 40, "rest: tail[2] == 40")
println("2. Rest patterns: PASS")

-- ===== 3. Nested destructuring =====
let [[x, y], z] = [[1, 2], 3]
assert(x == 1, "nested: x == 1")
assert(y == 2, "nested: y == 2")
assert(z == 3, "nested: z == 3")
println("3. Nested destructuring: PASS")

-- ===== 4. Map/struct destructuring (shorthand) =====
let point = {"x": 10, "y": 20, "z": 30}
let { x: px, y: py } = point
assert(px == 10, "map destr rename: px == 10")
assert(py == 20, "map destr rename: py == 20")
println("4. Map destructuring with rename: PASS")

-- ===== 5. Map destructuring (shorthand field names) =====
let data = {"name": "Alice", "age": 30}
let { name, age } = data
assert(name == "Alice", "map destr shorthand: name == Alice")
assert(age == 30, "map destr shorthand: age == 30")
println("5. Map destructuring shorthand: PASS")

-- ===== 6. Swap via destructuring =====
var sa = 1
var sb = 2
let [sa2, sb2] = [sb, sa]
assert(sa2 == 2, "swap: sa2 == 2")
assert(sb2 == 1, "swap: sb2 == 1")
println("6. Swap via destructuring: PASS")

-- ===== 7. Destructuring in for loops =====
let pairs = [[1, "one"], [2, "two"], [3, "three"]]
var sum = 0
var words = ""
for [num, word] in pairs {
    sum = sum + num
    words = words ++ word ++ " "
}
assert(sum == 6, "for destr: sum == 6")
println("7. Destructuring in for loops: PASS")

-- ===== 8. Default values in map destructuring =====
let partial = {"x": 42}
let { x: dx = 0, y: dy = 99 } = partial
assert(dx == 42, "default: dx == 42 (existing value)")
assert(dy == 99, "default: dy == 99 (default used)")
println("8. Default values with rename: PASS")

-- ===== 9. Default values shorthand =====
let partial2 = {"a": 5}
let { a = 0, b = 10 } = partial2
assert(a == 5, "default shorthand: a == 5")
assert(b == 10, "default shorthand: b == 10")
println("9. Default values shorthand: PASS")

-- ===== 10. Wildcard in array destructuring =====
let [first, _, third] = [100, 200, 300]
assert(first == 100, "wildcard: first == 100")
assert(third == 300, "wildcard: third == 300")
println("10. Wildcard in array destructuring: PASS")

-- ===== 11. var with destructuring =====
var [va, vb] = [10, 20]
assert(va == 10, "var destr: va == 10")
assert(vb == 20, "var destr: vb == 20")
println("11. var destructuring: PASS")

-- ===== 12. Deeper nesting =====
let [p1, [p2, [p3, p4]]] = [1, [2, [3, 4]]]
assert(p1 == 1, "deep: p1 == 1")
assert(p2 == 2, "deep: p2 == 2")
assert(p3 == 3, "deep: p3 == 3")
assert(p4 == 4, "deep: p4 == 4")
println("12. Deep nesting: PASS")

-- ===== 13. Rest with empty tail =====
let [only, ..empty_tail] = [42]
assert(only == 42, "rest empty: only == 42")
assert(len(empty_tail) == 0, "rest empty: tail is empty")
println("13. Rest with empty tail: PASS")

-- ===== 14. Destructuring with entries() =====
let m = {"a": 1, "b": 2}
var key_sum = ""
var val_sum = 0
for [k, v] in entries(m) {
    key_sum = key_sum ++ k
    val_sum = val_sum + v
}
assert(val_sum == 3, "entries: val_sum == 3")
println("14. Destructuring with entries(): PASS")

-- ===== 15. Single-element array destructuring =====
let [solo] = [99]
assert(solo == 99, "single: solo == 99")
println("15. Single-element array destructuring: PASS")

-- ===== 16. Map destructuring from struct =====
struct Point2D { x, y }
let pt = Point2D { x: 5, y: 10 }
let { x: sx, y: sy } = pt
assert(sx == 5, "struct destr: sx == 5")
assert(sy == 10, "struct destr: sy == 10")
println("16. Struct destructuring: PASS")

-- ===== 17. Nested array + map destructuring =====
let items = [{"name": "foo", "val": 1}, {"name": "bar", "val": 2}]
var names = ""
for item in items {
    let { name: n, val: v } = item
    names = names ++ n ++ " "
}
println("17. Nested array + map destructuring: PASS")

-- ===== 18. Multiple rest elements =====
let [r1, r2, ..rest2] = [1, 2, 3, 4, 5]
assert(r1 == 1, "multi rest: r1 == 1")
assert(r2 == 2, "multi rest: r2 == 2")
assert(len(rest2) == 3, "multi rest: rest2 has 3 elements")
assert(rest2[0] == 3, "multi rest: rest2[0] == 3")
println("18. Multiple fixed + rest: PASS")

-- ===== 19. Match with destructuring =====
let pair = [3, 4]
let result = match pair {
    [0, y] => "zero-" ++ str(y),
    [x, 4] => "x=" ++ str(x) ++ "-four",
    _ => "other",
}
assert(result == "x=3-four", "match destr: " ++ result)
println("19. Match with destructuring: PASS")

-- ===== 20. Function parameter destructuring =====
fn sum_pair([a, b]) { a + b }
assert(sum_pair([3, 7]) == 10, "fn param destr: sum == 10")
println("20. Function parameter destructuring: PASS")

println("")
println("All destructuring tests passed!")
