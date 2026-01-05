-- Test map comprehensions
let squares = {x: x * x for x in range(5)}
println(squares)

let names = ["alice", "bob", "charlie"]
let name_lens = {n: len(n) for n in names}
println(name_lens)

-- With filter
let evens = {x: x * x for x in range(10) if x % 2 == 0}
println(evens)

println("Map comprehension tests passed!")
