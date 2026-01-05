match 5 {
    1 => println("one"),
    2 => {},
    _ => println("other")
}

let y = match 3 {
    1 => "one",
    _ => null
}
assert(y == null, "match arm returning null")

println("All empty match arm tests passed!")
