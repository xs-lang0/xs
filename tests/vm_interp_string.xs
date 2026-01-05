-- Test string interpolation in VM mode

fn test_basic_interp() {
    let name = "world"
    let s = "hello {name}"
    assert(s == "hello world", "basic interpolation")
}

fn test_expr_interp() {
    let x = 5
    let s = "result is {x + 3}"
    assert(s == "result is 8", "expression interpolation")
}

fn test_multi_interp() {
    let a = "foo"
    let b = "bar"
    let s = "{a} and {b}"
    assert(s == "foo and bar", "multiple interpolations")
}

fn test_int_interp() {
    let n = 42
    let s = "number: {n}"
    assert(s == "number: 42", "integer interpolation")
}

fn test_bool_interp() {
    let b = true
    let s = "value: {b}"
    assert(s == "value: true", "boolean interpolation")
}

fn test_nested_call_interp() {
    fn double(x) { x * 2 }
    let s = "doubled: {double(21)}"
    assert(s == "doubled: 42", "function call in interpolation")
}

fn test_interp_only() {
    let x = "hello"
    let s = "{x}"
    assert(s == "hello", "interpolation-only string")
}

fn main() {
    test_basic_interp()
    test_expr_interp()
    test_multi_interp()
    test_int_interp()
    test_bool_interp()
    test_nested_call_interp()
    test_interp_only()
    println("vm_interp_string: all tests passed")
}
