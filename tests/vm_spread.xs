-- Test spread operator in VM mode

fn test_spread_in_array() {
    let a = [1, 2, 3]
    let b = [0, ...a, 4]
    assert(len(b) == 5, "spread array length")
    assert(b[0] == 0, "spread b[0]")
    assert(b[1] == 1, "spread b[1]")
    assert(b[4] == 4, "spread b[4]")
}

fn test_spread_in_call() {
    fn add3(a, b, c) { a + b + c }
    let args = [10, 20, 30]
    -- Spread in function call not directly supported in VM yet,
    -- but we can test spread in array context
    let result = [0, ...args]
    assert(len(result) == 4, "spread call array length")
    assert(result[3] == 30, "spread call result[3]")
}

fn test_spread_concat() {
    let a = [1, 2]
    let b = [3, 4]
    let c = [...a, ...b]
    assert(len(c) == 4, "double spread length")
    assert(c[0] == 1, "double spread c[0]")
    assert(c[3] == 4, "double spread c[3]")
}

fn test_spread_empty() {
    let a = []
    let b = [1, ...a, 2]
    assert(len(b) == 2, "spread empty array")
    assert(b[0] == 1, "spread empty b[0]")
    assert(b[1] == 2, "spread empty b[1]")
}

fn main() {
    test_spread_in_array()
    test_spread_in_call()
    test_spread_concat()
    test_spread_empty()
    println("vm_spread: all tests passed")
}
