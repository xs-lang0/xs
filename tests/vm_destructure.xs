-- Test destructuring assignments in VM mode

fn test_tuple_destructure() {
    let (a, b) = (10, 20)
    assert(a == 10, "tuple destructure a")
    assert(b == 20, "tuple destructure b")
}

fn test_triple_destructure() {
    let (x, y, z) = (1, 2, 3)
    assert(x == 1, "triple x")
    assert(y == 2, "triple y")
    assert(z == 3, "triple z")
}

fn test_array_destructure() {
    let [a, b, c] = [10, 20, 30]
    assert(a == 10, "array destructure a")
    assert(b == 20, "array destructure b")
    assert(c == 30, "array destructure c")
}

fn test_destructure_from_fn() {
    fn pair() { (42, "hello") }
    let (n, s) = pair()
    assert(n == 42, "fn pair number")
    assert(s == "hello", "fn pair string")
}

fn test_wildcard() {
    let (a, _, c) = (1, 2, 3)
    assert(a == 1, "wildcard a")
    assert(c == 3, "wildcard c")
}

fn test_nested_tuple() {
    let tup = (100, 200, 300)
    let (x, y, z) = tup
    assert(x == 100, "nested x")
    assert(y == 200, "nested y")
    assert(z == 300, "nested z")
}

fn main() {
    test_tuple_destructure()
    test_triple_destructure()
    test_array_destructure()
    test_destructure_from_fn()
    test_wildcard()
    test_nested_tuple()
    println("vm_destructure: all tests passed")
}
