-- Test compound assignment operators in VM mode

fn test_plus_assign() {
    var x = 10
    x += 5
    assert(x == 15, "+= operator")
}

fn test_minus_assign() {
    var x = 10
    x -= 3
    assert(x == 7, "-= operator")
}

fn test_mul_assign() {
    var x = 4
    x *= 3
    assert(x == 12, "*= operator")
}

fn test_div_assign() {
    var x = 20
    x /= 4
    assert(x == 5, "/= operator")
}

fn test_mod_assign() {
    var x = 17
    x %= 5
    assert(x == 2, "%= operator")
}

fn test_compound_in_loop() {
    var sum = 0
    for i in 1..=10 {
        sum += i
    }
    assert(sum == 55, "compound += in loop")
}

fn test_string_concat_assign() {
    var s = "hello"
    s ++= " world"
    assert(s == "hello world", "++= string concat")
}

fn test_array_index_compound() {
    var arr = [1, 2, 3]
    arr[0] += 10
    assert(arr[0] == 11, "array index compound +=")
}

fn test_field_compound() {
    var m = {"x": 10, "y": 20}
    m.x += 5
    assert(m.x == 15, "field compound +=")
}

fn test_chained_compound() {
    var a = 0
    a += 1
    a += 2
    a += 3
    assert(a == 6, "chained compound +=")
}

fn main() {
    test_plus_assign()
    test_minus_assign()
    test_mul_assign()
    test_div_assign()
    test_mod_assign()
    test_compound_in_loop()
    test_string_concat_assign()
    test_array_index_compound()
    test_field_compound()
    test_chained_compound()
    println("vm_compound_assign: all tests passed")
}
