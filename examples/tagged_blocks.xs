-- tagged blocks: user-defined control structures

-- simple: run a block twice
tag twice() {
    yield;
    yield;
}

println("--- twice ---")
twice() {
    println("hello!")
}

-- retry: try a block N times, catching errors
tag retry(n) {
    var attempts = 0
    loop {
        try {
            let result = yield;
            return result
        } catch e {
            attempts = attempts + 1
            if attempts >= n {
                throw "failed after {n} attempts: {e}"
            }
        }
    }
}

println("\n--- retry ---")
var counter = 0
retry(3) {
    counter = counter + 1
    println("attempt {counter}")
    if counter < 3 {
        throw "not yet"
    }
    println("success on attempt {counter}")
}

-- suppress: swallow errors from a block
tag suppress() {
    try {
        let result = yield;
        return result
    } catch e {
        return null
    }
}

println("\n--- suppress ---")
let result = suppress() {
    throw "this error is swallowed"
}
println("result: {result}")
println("no crash!")

-- with_default: provide a fallback value if block returns null
tag with_default(fallback) {
    let val = yield;
    if val == null {
        return fallback
    }
    return val
}

println("\n--- with_default ---")
let val1 = with_default(42) {
    null
}
println("val1 = {val1}")

let val2 = with_default(42) {
    99
}
println("val2 = {val2}")

-- composing tags: measure + retry
tag timed() {
    import time
    let start = time.clock()
    let result = yield;
    let elapsed = time.clock() - start
    println("  (took {elapsed}s)")
    return result
}

println("\n--- timed ---")
timed() {
    var _sum = 0
    for i in 0..10000 { _sum = _sum + i }
    println("sum = {_sum}")
}
