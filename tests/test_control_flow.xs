-- ===========================================
-- Test: Complex Control Flow
-- ===========================================

var passed = 0
var failed = 0

fn check(cond, label) {
    if cond {
        passed = passed + 1
    } else {
        println("FAIL: " ++ label)
        failed = failed + 1
    }
}

-- === 1. Simple for loop ===
var sum1 = 0
for i in range(10) {
    sum1 = sum1 + i
}
check(sum1 == 45, "for loop sum 0..9 = 45")

-- === 2. While loop ===
var sum2 = 0
var i2 = 0
while i2 < 10 {
    sum2 = sum2 + i2
    i2 = i2 + 1
}
check(sum2 == 45, "while loop sum 0..9 = 45")

-- === 3. Break in for loop ===
var break_sum = 0
for i in range(100) {
    if i == 5 { break }
    break_sum = break_sum + i
}
check(break_sum == 10, "break at 5, sum 0..4 = 10")

-- === 4. Continue in for loop ===
var cont_sum = 0
for i in range(10) {
    if i % 2 == 0 { continue }
    cont_sum = cont_sum + i
}
check(cont_sum == 25, "continue even, sum odds = 25")

-- === 5. Nested loops with inner break ===
var inner_breaks = 0
for i in range(5) {
    for j in range(10) {
        if j == 3 { break }
    }
    inner_breaks = inner_breaks + 1
}
check(inner_breaks == 5, "inner break doesnt affect outer")

-- === 6. Nested loops with inner continue ===
var inner_cont_count = 0
for i in range(3) {
    for j in range(5) {
        if j == 2 { continue }
        inner_cont_count = inner_cont_count + 1
    }
}
check(inner_cont_count == 12, "inner continue: 3 * 4 = 12")

-- === 7. Labeled break exits outer loop ===
var lb_i = -1
var lb_j = -1
outer: for i in range(5) {
    for j in range(5) {
        if i * j == 6 {
            lb_i = i
            lb_j = j
            break outer
        }
    }
}
check(lb_i == 2, "labeled break outer i = 2")
check(lb_j == 3, "labeled break outer j = 3")

-- === 8. Labeled continue skips outer iteration ===
var lc_visited = []
skip: for i in range(4) {
    for j in range(4) {
        if j == 1 { continue skip }
        lc_visited.push(i)
    }
}
check(lc_visited.len() == 4, "labeled continue visits 4 times")

-- === 9. Triple-nested with labeled break ===
var tn_i = -1
var tn_j = -1
var tn_k = -1
top: for i in range(3) {
    for j in range(3) {
        for k in range(3) {
            if i + j + k == 4 {
                tn_i = i
                tn_j = j
                tn_k = k
                break top
            }
        }
    }
}
check(tn_i == 0, "triple nested i = 0")
check(tn_j == 2, "triple nested j = 2")
check(tn_k == 2, "triple nested k = 2")

-- === 10. Labeled break with while loop ===
var wb_i = 0
var wb_j = 0
outer_w: while wb_i < 5 {
    wb_j = 0
    while wb_j < 5 {
        if wb_i + wb_j == 7 {
            break outer_w
        }
        wb_j = wb_j + 1
    }
    wb_i = wb_i + 1
}
check(wb_i + wb_j == 7, "labeled break while: i+j = 7")

-- === 11. Break with value from loop ===
let bv_result = loop {
    break 42
}
check(bv_result == 42, "break with value from loop")

-- === 12. Break with computed value ===
var bv_i = 0
let found = loop {
    if bv_i == 5 { break bv_i * 10 }
    bv_i = bv_i + 1
}
check(found == 50, "break with computed value 50")

-- === 13. If/elif/else chain ===
fn classify(x) {
    if x < 0 { "negative" }
    elif x == 0 { "zero" }
    elif x < 10 { "small" }
    elif x < 100 { "medium" }
    else { "large" }
}
check(classify(-5) == "negative", "classify negative")
check(classify(0) == "zero", "classify zero")
check(classify(5) == "small", "classify small")
check(classify(50) == "medium", "classify medium")
check(classify(500) == "large", "classify large")

-- === 14. If as expression ===
let if_val = if true { 1 } else { 2 }
check(if_val == 1, "if-expression true branch")

let if_val2 = if false { 1 } else { 2 }
check(if_val2 == 2, "if-expression false branch")

-- === 15. Return from function ===
fn early_return(x) {
    if x < 0 { return "negative" }
    if x == 0 { return "zero" }
    return "positive"
}
check(early_return(-1) == "negative", "early return negative")
check(early_return(0) == "zero", "early return zero")
check(early_return(1) == "positive", "early return positive")

-- === 16. Return from nested function ===
fn outer_fn() {
    fn inner_fn(x) {
        if x > 10 { return x * 2 }
        return x
    }
    let a = inner_fn(5)
    let b = inner_fn(15)
    return a + b
}
check(outer_fn() == 35, "return from nested fn: 5 + 30 = 35")

-- === 17. Defer basic ordering ===
var defer_log = []
fn test_defer_order() {
    defer { defer_log.push("first") }
    defer { defer_log.push("second") }
    defer_log.push("body")
}
test_defer_order()
check(defer_log.len() == 3, "defer log has 3 entries")
check(defer_log[0] == "body", "defer: body runs first")
check(defer_log[1] == "second", "defer: second deferred runs next (LIFO)")
check(defer_log[2] == "first", "defer: first deferred runs last (LIFO)")

-- === 18. Defer runs even after throw ===
var defer_throw_log = []
fn test_defer_throw() {
    defer { defer_throw_log.push("deferred") }
    defer_throw_log.push("before throw")
    throw "error!"
    defer_throw_log.push("after throw")
}
try {
    test_defer_throw()
} catch e {
    defer_throw_log.push("caught: " ++ str(e))
}
check(defer_throw_log[0] == "before throw", "defer+throw: before")
check(defer_throw_log[1] == "deferred", "defer+throw: deferred ran")
check(defer_throw_log[2] == "caught: error!", "defer+throw: caught")

-- === 19. Defer runs on normal return ===
var defer_return_log = []
fn test_defer_return() {
    defer { defer_return_log.push("deferred") }
    defer_return_log.push("body")
    return 42
}
let dr_result = test_defer_return()
check(dr_result == 42, "defer return value")
check(defer_return_log[0] == "body", "defer return: body first")
check(defer_return_log[1] == "deferred", "defer return: deferred ran")

-- === 20. Try/catch/finally all interact ===
var tcf_log = []
fn test_tcf() {
    try {
        tcf_log.push("try")
        throw "tcf error"
        tcf_log.push("unreachable")
    } catch e {
        tcf_log.push("catch: " ++ str(e))
    } finally {
        tcf_log.push("finally")
    }
}
test_tcf()
check(tcf_log.len() == 3, "tcf has 3 entries")
check(tcf_log[0] == "try", "tcf: try ran")
check(tcf_log[1] == "catch: tcf error", "tcf: catch ran")
check(tcf_log[2] == "finally", "tcf: finally ran")

-- === 21. Finally runs without exception ===
var finally_no_err = []
try {
    finally_no_err.push("try")
} catch e {
    finally_no_err.push("catch")
} finally {
    finally_no_err.push("finally")
}
check(finally_no_err.len() == 2, "finally no err: 2 entries")
check(finally_no_err[0] == "try", "finally no err: try")
check(finally_no_err[1] == "finally", "finally no err: finally")

-- === 22. Match basic patterns ===
fn describe(x) {
    match x {
        0 => "zero",
        1 => "one",
        2 => "two",
        _ => "other",
    }
}
check(describe(0) == "zero", "match zero")
check(describe(1) == "one", "match one")
check(describe(2) == "two", "match two")
check(describe(99) == "other", "match wildcard")

-- === 23. Match with guard-like conditions (via nested if) ===
fn sign(x) {
    if x > 0 { "positive" }
    elif x < 0 { "negative" }
    else { "zero" }
}
check(sign(5) == "positive", "sign positive")
check(sign(-3) == "negative", "sign negative")
check(sign(0) == "zero", "sign zero")

-- === 24. For loop over array ===
let arr = [10, 20, 30, 40, 50]
var arr_sum = 0
for val in arr {
    arr_sum = arr_sum + val
}
check(arr_sum == 150, "for-in array sum 150")

-- === 25. For loop with index via enumerate ===
let letters = ["a", "b", "c"]
var idx_sum = 0
let enumed = letters.enumerate()
for pair in enumed {
    idx_sum = idx_sum + pair[0]
}
check(idx_sum == 3, "enumerate index sum 0+1+2 = 3")

-- === 26. Nested if/else ===
fn nested_if(a, b) {
    if a > 0 {
        if b > 0 { "both positive" }
        else { "a positive, b not" }
    } else {
        if b > 0 { "b positive, a not" }
        else { "both not positive" }
    }
}
check(nested_if(1, 1) == "both positive", "nested if ++")
check(nested_if(1, -1) == "a positive, b not", "nested if +-")
check(nested_if(-1, 1) == "b positive, a not", "nested if -+")
check(nested_if(-1, -1) == "both not positive", "nested if --")

-- === 27. Complex loop with accumulator ===
var primes = []
for n in range(2, 30) {
    var is_prime = true
    var d = 2
    while d * d <= n {
        if n % d == 0 {
            is_prime = false
            break
        }
        d = d + 1
    }
    if is_prime { primes.push(n) }
}
check(primes.len() == 10, "primes under 30: 10 primes")
check(primes[0] == 2, "first prime is 2")
check(primes[9] == 29, "tenth prime is 29")

-- === 28. FizzBuzz logic ===
var fizzbuzz = []
for i in range(1, 16) {
    if i % 15 == 0 { fizzbuzz.push("FizzBuzz") }
    elif i % 3 == 0 { fizzbuzz.push("Fizz") }
    elif i % 5 == 0 { fizzbuzz.push("Buzz") }
    else { fizzbuzz.push(str(i)) }
}
check(fizzbuzz[0] == "1", "fizzbuzz[1]=1")
check(fizzbuzz[2] == "Fizz", "fizzbuzz[3]=Fizz")
check(fizzbuzz[4] == "Buzz", "fizzbuzz[5]=Buzz")
check(fizzbuzz[14] == "FizzBuzz", "fizzbuzz[15]=FizzBuzz")

-- === 29. Short-circuit logical AND ===
var side_effect_and = false
let and_result = false && { side_effect_and = true; true }
check(side_effect_and == false, "short-circuit AND skips right side")

-- === 30. Short-circuit logical OR ===
var side_effect_or = false
let or_result = true || { side_effect_or = true; false }
check(side_effect_or == false, "short-circuit OR skips right side")

-- === 31. While loop with complex condition ===
var wc_i = 0
var wc_sum = 0
while wc_i < 10 && wc_sum < 20 {
    wc_sum = wc_sum + wc_i
    wc_i = wc_i + 1
}
check(wc_sum >= 20 || wc_i >= 10, "while with && condition terminates")

-- === 32. Multiple returns in match arms ===
fn categorize(x) {
    match type(x) {
        "int" => "integer",
        "float" => "floating",
        "str" => "string",
        "bool" => "boolean",
        "null" => "nothing",
        _ => "unknown",
    }
}
check(categorize(42) == "integer", "categorize int")
check(categorize(3.14) == "floating", "categorize float")
check(categorize("hi") == "string", "categorize str")
check(categorize(true) == "boolean", "categorize bool")
check(categorize(null) == "nothing", "categorize null")

-- Summary
println("")
println("Control Flow Test Results:")
println("  Passed: " ++ str(passed))
println("  Failed: " ++ str(failed))
if failed > 0 {
    println("SOME TESTS FAILED!")
} else {
    println("ALL CONTROL FLOW TESTS PASSED!")
}
