var i = 0
while true {
    if i >= 5 { break }
    i = i + 1
}
println(i)

var found = -1
for x in [10, 20, 30, 40, 50] {
    if x == 30 {
        found = x
        break
    }
}
println(found)
