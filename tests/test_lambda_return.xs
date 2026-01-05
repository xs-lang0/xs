-- Test: return in lambda should return from the lambda, not the enclosing fn
fn test_lambda_return() {
    let items = [1, 2, 3, 4, 5]
    let result = items.map(|x| {
        if x > 3 { return x * 10 }
        x
    })
    -- If return exits lambda: result = [1, 2, 3, 40, 50]
    -- If return exits test_lambda_return: we'd never get here
    assert(result == [1, 2, 3, 40, 50], "return exits lambda only")
    "completed"
}

let r = test_lambda_return()
assert(r == "completed", "enclosing function completed")

-- Test: last expression is lambda return value
let double = |x| x * 2
assert(double(5) == 10, "implicit return")

println("Lambda return test passed!")
