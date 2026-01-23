-- functional.xs: closures, higher-order functions, pipe, comprehensions

println("--- Closures ---")

-- closures capture by reference, so state persists
fn make_counter(start) {
    var n = start
    return #{
        "inc": fn() { n = n + 1; return n },
        "dec": fn() { n = n - 1; return n },
        "get": fn() { return n }
    }
}

let counter = make_counter(0)
counter["inc"]()
counter["inc"]()
counter["inc"]()
counter["dec"]()
println("counter: {counter["get"]()}")
assert_eq(counter["get"](), 2)

-- closure as a tiny state machine
fn make_toggler() {
    var on = false
    return fn() {
        on = not on
        return on
    }
}

let toggle = make_toggler()
assert_eq(toggle(), true)
assert_eq(toggle(), false)
assert_eq(toggle(), true)
println("toggler works: {toggle()}")

println("\n--- Higher-Order Functions ---")

let nums = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]

-- map, filter, reduce
let doubled = nums.map(fn(x) { x * 2 })
let evens = nums.filter(fn(x) { x % 2 == 0 })
let sum = nums.reduce(fn(acc, x) { acc + x }, 0)

println("doubled: {doubled}")
println("evens: {evens}")
println("sum: {sum}")
assert_eq(sum, 55)
assert_eq(evens, [2, 4, 6, 8, 10])

-- any / all
let has_big = nums.any(fn(x) { x > 7 })
let all_pos = nums.all(fn(x) { x > 0 })
println("any > 7? {has_big}")
println("all positive? {all_pos}")
assert(has_big, "should have big")
assert(all_pos, "should all be positive")

-- chaining higher-order methods
let result = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
    .filter(fn(x) { x % 2 == 0 })
    .map(fn(x) { x * x })
    .reduce(fn(acc, x) { acc + x }, 0)

println("sum of squares of evens: {result}")
assert_eq(result, 220)

-- arrow lambdas are nice for short stuff
let squares = nums.map((x) => x * x)
println("squares: {squares}")

println("\n--- Pipe Operator ---")

fn add_one(x) = x + 1
fn double(x) = x * 2
fn negate(x) = -x

-- |> passes the left side as first arg to the right side
let piped = 5 |> double |> add_one |> negate
println("5 |> double |> add_one |> negate = {piped}")
assert_eq(piped, -11)

-- works great for data transformations
fn to_words(s) { return s.split(" ") }
fn shout(arr) { return arr.map(fn(w) { w.upper() }) }
fn rejoin(arr) { return arr.join("! ") }

let yelled = "hello world" |> to_words |> shout |> rejoin
println(yelled)
assert_eq(yelled, "HELLO! WORLD")

println("\n--- List Comprehensions ---")

-- basic
let sq = [x * x for x in 1..=5]
println("squares: {sq}")
assert_eq(sq, [1, 4, 9, 16, 25])

-- with filter
let odd_sq = [x * x for x in 1..=10 if x % 2 != 0]
println("odd squares: {odd_sq}")
assert_eq(odd_sq, [1, 9, 25, 49, 81])

-- fizzbuzz via comprehension + match
fn fizzbuzz(n) {
    return match (n % 3, n % 5) {
        (0, 0) => "FizzBuzz"
        (0, _) => "Fizz"
        (_, 0) => "Buzz"
        _ => str(n)
    }
}

let fb = [fizzbuzz(n) for n in 1..=15]
println("fizzbuzz: {fb}")
assert_eq(fb[2], "Fizz")
assert_eq(fb[4], "Buzz")
assert_eq(fb[14], "FizzBuzz")

println("\n--- Putting It Together ---")

-- build a tiny functional toolkit
fn compose(f, g) {
    return fn(x) { g(f(x)) }
}

let process = compose(
    fn(x) { x * 2 },
    fn(x) { x + 1 }
)
println("compose(double, inc)(10) = {process(10)}")
assert_eq(process(10), 21)

-- memoize with a closure over a map
fn memoize(f) {
    var cache = #{}
    return fn(x) {
        let key = str(x)
        if cache.has(key) { return cache[key] }
        let result = f(x)
        cache[key] = result
        return result
    }
}

var call_count = 0
let slow_square = memoize(fn(x) {
    call_count = call_count + 1
    return x * x
})

slow_square(5)
slow_square(5)
slow_square(5)
println("called {call_count} time(s) for 3 lookups")
assert_eq(call_count, 1)
assert_eq(slow_square(5), 25)

println("\nAll good!")
