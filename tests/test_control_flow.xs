-- if/elif/else, while, for, loop, break, continue, match, labeled loops

-- if/else
var x = 0
if true { x = 1 }
assert_eq(x, 1)
if false { x = 99 } else { x = 2 }
assert_eq(x, 2)

-- if as expression
let v = if true { 10 } else { 20 }
assert_eq(v, 10)

-- elif
fn classify(n) {
    if n < 0 { return "neg" }
    elif n == 0 { return "zero" }
    elif n < 10 { return "small" }
    else { return "big" }
}
assert_eq(classify(-1), "neg")
assert_eq(classify(0), "zero")
assert_eq(classify(5), "small")
assert_eq(classify(100), "big")

-- while
var w = 0
while w < 5 { w = w + 1 }
assert_eq(w, 5)

-- for..in
var sum = 0
for i in 0..5 { sum = sum + i }
assert_eq(sum, 10)

-- for over array
var items = []
for x in [10, 20, 30] { items.push(x) }
assert_eq(items, [10, 20, 30])

-- for over map
let m = #{"a": 1, "b": 2}
var keys = []
for k in m { keys.push(k) }
assert_eq(keys.sort(), ["a", "b"])

-- loop + break
var l = 0
loop {
    l = l + 1
    if l == 5 { break }
}
assert_eq(l, 5)

-- break with value
let bv = loop {
    break 42
}
assert_eq(bv, 42)

-- continue
var evens = []
for i in 0..10 {
    if i % 2 != 0 { continue }
    evens.push(i)
}
assert_eq(evens, [0, 2, 4, 6, 8])

-- labeled loops
var found_i = -1
var found_j = -1
outer: for i in range(5) {
    for j in range(5) {
        if i * j == 6 {
            found_i = i
            found_j = j
            break outer
        }
    }
}
assert_eq(found_i, 2)
assert_eq(found_j, 3)

-- labeled continue
var visited = []
outer2: for i in range(3) {
    for j in range(3) {
        if j == 1 { continue outer2 }
        visited.push(i * 10 + j)
    }
}
assert_eq(visited, [0, 10, 20])

-- match
let r = match 2 {
    1 => "one"
    2 => "two"
    _ => "other"
}
assert_eq(r, "two")

-- match with guards
let val = 15
let desc = match val {
    x if x < 0 => "negative"
    x if x < 10 => "small"
    x if x < 100 => "medium"
    _ => "large"
}
assert_eq(desc, "medium")

-- match on strings
let lang = "xs"
let greeting = match lang {
    "xs" => "hello from xs!"
    "py" => "hello from python!"
    _ => "unknown"
}
assert_eq(greeting, "hello from xs!")

-- match with tuple destructuring
let pair = (3, 4)
let result = match pair {
    (0, _) => "zero x"
    (_, 0) => "zero y"
    (a, b) => "sum: {a + b}"
    _ => "?"
}
assert_eq(result, "sum: 7")

-- nested if in loops
var count = 0
for i in 0..10 {
    if i % 2 == 0 {
        if i % 3 == 0 {
            count = count + 1
        }
    }
}
assert_eq(count, 2)

println("test_control_flow: all passed")
