-- ==========================================
-- XS Language Integration Test
-- ==========================================

-- 1. String features
let n = 42
let msg = "The answer is {n}"
assert(msg == "The answer is 42", "interpolation")

let raw = r"\n not escaped"
assert(contains(raw, "\\n"), "raw string")

let sep = 1_000_000
assert(sep == 1000000, "numeric separator")

-- 2. Closures and HOF
let nums = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
let evens = nums.filter(fn(x) { x % 2 == 0 })
let doubled = evens.map(fn(x) { x * 2 })
let total = doubled.reduce(fn(acc, x) { acc + x }, 0)
assert(total == 60, "HOF pipeline: {total}")

-- 3. Pattern matching
fn classify(x) {
    match x {
        n @ 0..10   => "small({n})",
        n @ 10..100 => "medium({n})",
        _           => "large",
    }
}
assert(classify(5) == "small(5)", "range pattern capture")
assert(classify(50) == "medium(50)", "range pattern medium")
assert(classify(200) == "large", "wildcard")

-- 4. Traits and operator overloading
trait Describable {
    fn describe(self)
}

struct Point { x, y }
impl Point {
    fn +(self, other) { Point { x: self.x + other.x, y: self.y + other.y } }
}
impl Describable for Point {
    fn describe(self) { "Point({self.x}, {self.y})" }
}

let p1 = Point { x: 1, y: 2 }
let p2 = Point { x: 3, y: 4 }
let p3 = p1 + p2
assert(p3.describe() == "Point(4, 6)", "operator + trait: {p3.describe()}")

-- 5. Enums with methods and constructors
enum Shape { Circle(r), Rect(w, h) }
impl Shape {
    fn area(self) {
        match self {
            Shape::Circle(r) => r * r * 3,
            Shape::Rect(w, h) => w * h,
        }
    }
}
let circle = Shape::Circle(5)
let rect = Shape::Rect(4, 6)
assert(circle.area() == 75, "circle area: {circle.area()}")
assert(rect.area() == 24, "rect area: {rect.area()}")

-- 6. Modules
module utils {
    fn clamp(x, lo, hi) {
        if x < lo { lo }
        elif x > hi { hi }
        else { x }
    }
    fn abs(x) { if x < 0 { 0 - x } else { x } }
}
assert(utils.clamp(5, 0, 10) == 5, "clamp middle")
assert(utils.clamp(-5, 0, 10) == 0, "clamp low")
assert(utils.abs(-7) == 7, "abs")

-- 7. Error handling with ?
fn safe_sqrt(x) {
    if x < 0 { return Err("negative input") }
    Ok(x * x)
}

fn compute(x) {
    let sq = safe_sqrt(x)?
    Ok(sq + 1)
}

let ok_result = compute(5)
let err_result = compute(-1)
assert(ok_result == Ok(26), "ok result: {ok_result}")
assert(err_result == Err("negative input"), "err result: {err_result}")

-- 8. Generators
fn* squares(n) {
    var i = 1
    while i <= n {
        yield i * i
        i = i + 1
    }
}
let sq_vals = squares(5)
assert(sq_vals == [1, 4, 9, 16, 25], "generator squares: {sq_vals}")

-- 9. Algebraic effects composing with error handling
effect Emit {
    fn emit(val)
}

fn sum_with_log(items) {
    var total = 0
    for item in items {
        perform Emit.emit("adding {item}")
        total = total + item
    }
    total
}

var log_entries = []
let sum_result = handle sum_with_log([1, 2, 3, 4, 5]) {
    Emit.emit(msg) => {
        log_entries = log_entries ++ [msg]
        resume null
    }
}
assert(sum_result == 15, "effect sum: {sum_result}")
assert(log_entries.len() == 5, "log count: {log_entries.len()}")

-- 10. Async/await
async fn double_async(x) { x * 2 }
let async_result = await double_async(21)
assert(async_result == 42, "async await: {async_result}")

-- 11. Nursery
var nursery_results = []
nursery {
    spawn { nursery_results = nursery_results ++ ["a"] }
    spawn { nursery_results = nursery_results ++ ["b"] }
    spawn { nursery_results = nursery_results ++ ["c"] }
}
assert(nursery_results.len() == 3, "nursery tasks: {nursery_results.len()}")

-- 12. Channels
let ch = channel(5)
for i in range(1, 4) { ch.send(i * 10) }
var ch_sum = 0
while !ch.is_empty() { ch_sum = ch_sum + ch.recv() }
assert(ch_sum == 60, "channel sum: {ch_sum}")

-- 13. Defer and try/catch
fn risky() {
    defer { println("cleanup ran") }
    try {
        throw "simulated error"
    } catch e {
        println("caught: {e}")
    }
}
risky()

-- 14. Comprehensions and variadic
let comp = [x * x for x in range(1, 6)]
assert(comp == [1, 4, 9, 16, 25], "comprehension: {comp}")

fn variadic_sum(...args) {
    args.reduce(fn(acc, x) { acc + x }, 0)
}
assert(variadic_sum(1, 2, 3, 4, 5) == 15, "variadic sum")

println("ALL INTEGRATION TESTS PASSED")
