-- Test map methods
let m = {"a": 1, "b": 2, "c": 3}

-- len / length / count / size
assert_eq(m.len(), 3)
assert_eq(m.length(), 3)
assert_eq(m.count(), 3)
assert_eq(m.size(), 3)
println("PASS: len/length/count/size")

-- keys
let ks = m.keys()
assert_eq(len(ks), 3)
println("PASS: keys")

-- values
let vs = m.values()
assert_eq(len(vs), 3)
println("PASS: values")

-- entries / items
let es = m.entries()
assert_eq(len(es), 3)
let is2 = m.items()
assert_eq(len(is2), 3)
println("PASS: entries/items")

-- has / contains_key / has_key
assert_eq(m.has("a"), true)
assert_eq(m.has("z"), false)
assert_eq(m.contains_key("b"), true)
assert_eq(m.has_key("c"), true)
assert_eq(m.has_key("missing"), false)
println("PASS: has/contains_key/has_key")

-- get (with default)
assert_eq(m.get("a"), 1)
assert_eq(m.get("z"), null)
assert_eq(m.get("z", 42), 42)
println("PASS: get with default")

-- set
let m2 = {"x": 10}
m2.set("y", 20)
assert_eq(m2.get("y"), 20)
assert_eq(m2.len(), 2)
println("PASS: set")

-- delete / remove
let m3 = {"a": 1, "b": 2, "c": 3}
m3.delete("b")
assert_eq(m3.len(), 2)
assert_eq(m3.has("b"), false)
let m4 = {"x": 1, "y": 2}
m4.remove("x")
assert_eq(m4.has("x"), false)
println("PASS: delete/remove")

-- is_empty
let m5 = {}
assert_eq(m5.is_empty(), true)
assert_eq(m.is_empty(), false)
println("PASS: is_empty")

-- merge
let ma = {"a": 1, "b": 2}
let mb = {"b": 3, "c": 4}
let merged = ma.merge(mb)
assert_eq(merged.get("a"), 1)
assert_eq(merged.get("b"), 3)
assert_eq(merged.get("c"), 4)
assert_eq(merged.len(), 3)
-- original unchanged
assert_eq(ma.get("b"), 2)
println("PASS: merge")

-- filter
let nums = {"a": 1, "b": 2, "c": 3, "d": 4}
let evens = nums.filter(|k, v| v % 2 == 0)
assert_eq(evens.len(), 2)
assert_eq(evens.get("b"), 2)
assert_eq(evens.get("d"), 4)
println("PASS: filter")

-- map
let doubled = nums.map(|k, v| v * 2)
assert_eq(doubled.get("a"), 2)
assert_eq(doubled.get("c"), 6)
println("PASS: map")

-- clone / copy
let orig = {"x": 10, "y": 20}
let cloned = orig.clone()
cloned.set("z", 30)
assert_eq(orig.has("z"), false)
assert_eq(cloned.has("z"), true)
println("PASS: clone/copy")

println("All map method tests passed!")
