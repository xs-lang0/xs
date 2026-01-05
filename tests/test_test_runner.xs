#[test]
fn test_addition() {
    assert(1 + 1 == 2, "addition")
}

#[test]
fn test_strings() {
    assert("hello".upper() == "HELLO", "upper")
}

fn helper() { 42 }

#[test]
fn test_helper() {
    assert(helper() == 42, "helper")
}
