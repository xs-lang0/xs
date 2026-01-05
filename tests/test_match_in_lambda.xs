let classify = |x| match x {
    1 => "one",
    2 => "two",
    _ => "other"
}
assert(classify(1) == "one", "match in pipe lambda")
assert(classify(2) == "two", "match in pipe lambda 2")
assert(classify(99) == "other", "match in pipe lambda wildcard")

let items = [1, 2, 3]
let result = items.map(|x| match x {
    1 => "a",
    2 => "b",
    _ => "c"
})
assert(result == ["a", "b", "c"], "match in lambda in method call")

println("All match-in-lambda tests passed!")
