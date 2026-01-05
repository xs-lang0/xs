-- ============================================================
-- test_iterators.xs — comprehensive iterator protocol tests
-- ============================================================

-- ── 1. Generator functions (fn* with yield) ────────────────

-- 1a: Basic generator producing array from yields
fn* count_up(n) {
    var i = 0
    while i < n {
        yield i
        i = i + 1
    }
}

var gen_result = []
for x in count_up(5) { gen_result = gen_result ++ [x] }
assert(gen_result == [0, 1, 2, 3, 4], "generator count_up(5) should produce [0,1,2,3,4]")
println("[PASS] 1a: basic generator fn*")

-- 1b: Generator in for-in loop
var gen_sum = 0
for x in count_up(4) {
    gen_sum = gen_sum + x
}
assert(gen_sum == 6, "for-in over generator: 0+1+2+3 == 6")
println("[PASS] 1b: generator in for-in loop")

-- 1c: Generator with no yields produces empty array
fn* empty_gen() {
    -- nothing
}
var empty_result = []
for x in empty_gen() { empty_result = empty_result ++ [x] }
assert(empty_result == [], "empty generator should produce []")
println("[PASS] 1c: empty generator")

-- 1d: Generator with conditional yields
fn* evens(n) {
    var i = 0
    while i < n {
        if i % 2 == 0 {
            yield i
        }
        i = i + 1
    }
}
var evens_result = []
for x in evens(10) { evens_result = evens_result ++ [x] }
assert(evens_result == [0, 2, 4, 6, 8], "evens generator")
println("[PASS] 1d: conditional yield generator")

-- 1e: Generator lambda (fn* lambda)
let gen_lam = fn*(n) {
    var i = 0
    while i < n {
        yield i * i
        i = i + 1
    }
}
var lam_result = []
for x in gen_lam(4) { lam_result = lam_result ++ [x] }
assert(lam_result == [0, 1, 4, 9], "generator lambda")
println("[PASS] 1e: generator lambda (fn*)")


-- ── 2. Custom iterators (struct with iter/next) ────────────

-- 2a: Counter struct with iter() and next() returning Some/None
struct Counter {
    max: i32,
    cur: i32,
}

impl Counter {
    fn iter(self) -> Counter { self }
    fn next(self) {
        if self.cur >= self.max { return None }
        let val = self.cur
        self.cur = self.cur + 1
        Some(val)
    }
}

let c = Counter { max: 5, cur: 0 }
var iter_sum = 0
for n in c {
    iter_sum = iter_sum + n
}
assert(iter_sum == 10, "custom iterator Counter: 0+1+2+3+4 == 10")
println("[PASS] 2a: custom struct iterator (Counter)")

-- 2b: Custom iterator — Fibonacci sequence (finite)
struct FibIter {
    limit: i32,
    a: i32,
    b: i32,
    count: i32,
}

impl FibIter {
    fn iter(self) { self }
    fn next(self) {
        if self.count >= self.limit { return None }
        let val = self.a
        let next_b = self.a + self.b
        self.a = self.b
        self.b = next_b
        self.count = self.count + 1
        Some(val)
    }
}

let fib = FibIter { limit: 8, a: 0, b: 1, count: 0 }
var fib_items = []
for n in fib {
    fib_items.push(n)
}
assert(fib_items == [0, 1, 1, 2, 3, 5, 8, 13], "Fibonacci iterator")
println("[PASS] 2b: custom Fibonacci iterator")


-- ── 3. Infinite iterators with break ───────────────────────

-- 3a: Counter without limit, break manually
struct InfiniteCounter {
    cur: i32,
}

impl InfiniteCounter {
    fn iter(self) { self }
    fn next(self) {
        let val = self.cur
        self.cur = self.cur + 1
        Some(val)
    }
}

let inf = InfiniteCounter { cur: 0 }
var inf_items = []
for n in inf {
    if n >= 5 { break }
    inf_items.push(n)
}
assert(inf_items == [0, 1, 2, 3, 4], "infinite iterator with break")
println("[PASS] 3a: infinite iterator with break")

-- 3b: Infinite generator with break (generators eagerly collect, so use struct)
-- Generators in XS eagerly collect all yields, so for truly infinite
-- sequences, use the struct-based iterator protocol with Some/None.
-- This test verifies the break mechanism works correctly.
let inf2 = InfiniteCounter { cur: 10 }
var inf2_sum = 0
for n in inf2 {
    if n >= 15 { break }
    inf2_sum = inf2_sum + n
}
assert(inf2_sum == 10 + 11 + 12 + 13 + 14, "infinite counter break sum")
println("[PASS] 3b: infinite iterator sum with break")


-- ── 4. Iterator adaptors (chaining filter/map/reduce) ──────

-- 4a: range(...).filter(...).map(...)
let chain_result = range(10).filter(|x| x > 5).map(|x| x * 2)
assert(chain_result == [12, 14, 16, 18], "range.filter.map chain")
println("[PASS] 4a: range.filter.map chain")

-- 4b: Array filter + map chain
let arr_chain = [1, 2, 3, 4, 5, 6, 7, 8].filter(|x| x % 2 == 0).map(|x| x * 10)
assert(arr_chain == [20, 40, 60, 80], "array.filter.map chain")
println("[PASS] 4b: array.filter.map chain")

-- 4c: range with reduce/fold
let sum_result = range(1, 6).reduce(|a, b| a + b)
assert(sum_result == 15, "range(1,6).reduce sum == 15")
println("[PASS] 4c: range.reduce")

-- 4d: range with any/all
let has_even = range(1, 6).any(|x| x % 2 == 0)
assert(has_even == true, "range(1,6) has even numbers")
let all_pos = range(1, 6).all(|x| x > 0)
assert(all_pos == true, "range(1,6) all positive")
println("[PASS] 4d: range.any/all")

-- 4e: range with find
let found = range(1, 20).find(|x| x > 10 and x % 3 == 0)
assert(found == 12, "find first >10 divisible by 3")
println("[PASS] 4e: range.find")

-- 4f: Chained operations on range with step
let step_chain = range(0, 20, 3).filter(|x| x > 5).map(|x| x + 100)
assert(step_chain == [106, 109, 112, 115, 118], "step range chain")
println("[PASS] 4f: step range chained operations")


-- ── 5. Enumerate with offset ───────────────────────────────

-- 5a: Default enumerate (start=0)
let e0 = enumerate(["a", "b", "c"])
assert(e0 == [(0, "a"), (1, "b"), (2, "c")], "enumerate default start=0")
println("[PASS] 5a: enumerate default")

-- 5b: Enumerate with start offset
let e1 = enumerate(["x", "y", "z"], 1)
assert(e1 == [(1, "x"), (2, "y"), (3, "z")], "enumerate start=1")
println("[PASS] 5b: enumerate with start=1")

-- 5c: Enumerate with large offset
let e100 = enumerate(["hello"], 100)
assert(e100 == [(100, "hello")], "enumerate start=100")
println("[PASS] 5c: enumerate with start=100")

-- 5d: Method-style enumerate with offset
let e_method = ["a", "b"].enumerate(5)
assert(e_method == [(5, "a"), (6, "b")], "method enumerate start=5")
println("[PASS] 5d: method-style enumerate with offset")

-- 5e: Enumerate in for-in loop
var enum_pairs = []
for (idx, val) in enumerate(["p", "q", "r"], 10) {
    enum_pairs.push(str(idx) + ":" + val)
}
assert(enum_pairs == ["10:p", "11:q", "12:r"], "enumerate for-in with offset")
println("[PASS] 5e: enumerate in for-in with offset")


-- ── 6. Step ranges ─────────────────────────────────────────

-- 6a: Basic step range
var step_items = []
for x in range(0, 10, 2) {
    step_items.push(x)
}
assert(step_items == [0, 2, 4, 6, 8], "range(0,10,2)")
println("[PASS] 6a: step range(0,10,2)")

-- 6b: Negative step range
var neg_items = []
for x in range(10, 0, -2) {
    neg_items.push(x)
}
assert(neg_items == [10, 8, 6, 4, 2], "range(10,0,-2)")
println("[PASS] 6b: negative step range(10,0,-2)")

-- 6c: Step range of 3
var step3_items = []
for x in range(0, 15, 3) {
    step3_items.push(x)
}
assert(step3_items == [0, 3, 6, 9, 12], "range(0,15,3)")
println("[PASS] 6c: step range(0,15,3)")

-- 6d: Step range contains
let r = range(0, 10, 2)
assert(r.contains(4) == true, "step range contains 4")
assert(r.contains(5) == false, "step range not contains 5")
println("[PASS] 6d: step range contains")

-- 6e: Step range to_array
let step_arr = range(0, 10, 3).to_array()
assert(step_arr == [0, 3, 6, 9], "step range to_array")
println("[PASS] 6e: step range to_array")


-- ── 7. For-in over various types ───────────────────────────

-- 7a: Array iteration
var arr_out = []
for x in [10, 20, 30] {
    arr_out.push(x)
}
assert(arr_out == [10, 20, 30], "for-in array")
println("[PASS] 7a: for-in array")

-- 7b: String iteration
var chars = []
for ch in "abc" {
    chars.push(ch)
}
assert(chars == ["a", "b", "c"], "for-in string")
println("[PASS] 7b: for-in string")

-- 7c: Range iteration
var range_out = []
for x in 0..5 {
    range_out.push(x)
}
assert(range_out == [0, 1, 2, 3, 4], "for-in 0..5")
println("[PASS] 7c: for-in range 0..5")

-- 7d: Inclusive range
var inc_out = []
for x in 0..=3 {
    inc_out.push(x)
}
assert(inc_out == [0, 1, 2, 3], "for-in 0..=3")
println("[PASS] 7d: for-in inclusive range")

-- 7e: Tuple iteration
var tup_out = []
for x in (100, 200, 300) {
    tup_out.push(x)
}
assert(tup_out == [100, 200, 300], "for-in tuple")
println("[PASS] 7e: for-in tuple")

-- 7f: For-in with break
var break_out = []
for x in range(100) {
    if x >= 3 { break }
    break_out.push(x)
}
assert(break_out == [0, 1, 2], "for-in with break")
println("[PASS] 7f: for-in with break")

-- 7g: For-in with continue
var cont_out = []
for x in range(6) {
    if x % 2 == 1 { continue }
    cont_out.push(x)
}
assert(cont_out == [0, 2, 4], "for-in with continue")
println("[PASS] 7g: for-in with continue")


-- ── 8. Destructuring in for-in ─────────────────────────────

-- 8a: Tuple destructuring
let pairs = [(1, "a"), (2, "b"), (3, "c")]
var keys = []
var vals = []
for (k, v) in pairs {
    keys.push(k)
    vals.push(v)
}
assert(keys == [1, 2, 3], "destructure keys")
assert(vals == ["a", "b", "c"], "destructure vals")
println("[PASS] 8a: tuple destructuring in for-in")

-- 8b: Enumerate + destructuring
var idx_vals = []
for (i, v) in enumerate([10, 20, 30]) {
    idx_vals.push(i * 100 + v)
}
assert(idx_vals == [10, 120, 230], "enumerate + destructure")
println("[PASS] 8b: enumerate + destructuring")


-- ── 9. Nested for-in loops ─────────────────────────────────

var matrix_out = []
for row in [[1,2],[3,4]] {
    for col in row {
        matrix_out.push(col)
    }
}
assert(matrix_out == [1, 2, 3, 4], "nested for-in")
println("[PASS] 9: nested for-in loops")


-- ── 10. Generator + for-in combined patterns ───────────────

-- 10a: Generator producing tuples
fn* indexed_squares(n) {
    var i = 0
    while i < n {
        yield (i, i * i)
        i = i + 1
    }
}
var sq_items = []
for (idx, sq) in indexed_squares(4) {
    sq_items.push(str(idx) + "=" + str(sq))
}
assert(sq_items == ["0=0", "1=1", "2=4", "3=9"], "generator tuples")
println("[PASS] 10a: generator producing tuples")

-- 10b: Collected generator results can be chained with array methods
var gen_arr = []
for x in count_up(10) { gen_arr = gen_arr ++ [x] }
let gen_filtered = gen_arr.filter(|x| x % 3 == 0)
assert(gen_filtered == [0, 3, 6, 9], "generator result filtered")
println("[PASS] 10b: generator result + filter")

-- 10c: Collected generator result + map + reduce
var gen_arr2 = []
for x in count_up(5) { gen_arr2 = gen_arr2 ++ [x] }
let gen_sum = gen_arr2.map(|x| x * x).reduce(|a, b| a + b)
assert(gen_sum == 30, "gen sum of squares: 0+1+4+9+16=30")
println("[PASS] 10c: generator + map + reduce")


-- ═══════════════════════════════════════════════════════════
println("")
println("All iterator tests passed!")
