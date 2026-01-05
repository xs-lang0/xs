let x = 5
let label = match x {
    0 => "zero",
    1 => "one",
    _ => "other",
}
assert(label == "other", "wildcard covers rest")

enum Color { Red, Green, Blue }
let c = Color::Green
let name = match c {
    Color::Red   => "red",
    Color::Green => "green",
    Color::Blue  => "blue",
}
assert(name == "green", "enum exhaustive match")

let flag = true
let s = match flag {
    true  => "yes",
    false => "no",
}
assert(s == "yes", "bool exhaustive")
println("exhaustiveness test passed")
