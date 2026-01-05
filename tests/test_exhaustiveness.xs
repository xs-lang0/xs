enum Color { Red, Green, Blue }

-- This should be exhaustive (no warning)
fn name(c) {
    match c {
        Color::Red => "red",
        Color::Green => "green",
        Color::Blue => "blue"
    }
}
assert(name(Color::Red) == "red", "exhaustive match")

-- This uses wildcard (explicit opt-out)
fn is_red(c) {
    match c {
        Color::Red => true,
        _ => false
    }
}
assert(is_red(Color::Red) == true, "wildcard match")

println("Exhaustiveness test passed!")
