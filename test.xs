fn* count(n) {
    var i = 0
    while i < n {
        yield i
        i += 1
    }
}
let gen = count(3)
println(gen.next())
println(gen.next())
println(gen.next())
println(gen.next())
