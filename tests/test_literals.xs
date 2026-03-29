use literals duration, color, date, size, angle

-- durations
assert_eq(type(5s), "float")
assert_eq(5s, 5000)
assert_eq(200ms, 200)
assert_eq(2m, 120000)
assert_eq(1h, 3600000)
assert_eq(3d, 259200000)

-- colors
let c = #ff6600
assert_eq(c.r, 255)
assert_eq(c.g, 102)
assert_eq(c.b, 0)

let white = #ffffff
assert_eq(white.r, 255)
assert_eq(white.g, 255)
assert_eq(white.b, 255)

-- dates
let d = 2024-03-15
assert_eq(type(d), "str")
assert_eq(d, "2024-03-15")

-- sizes
assert_eq(10kb, 10240)
assert_eq(1mb, 1048576)
assert_eq(1gb, 1073741824)

-- angles
let a = 180deg
assert(a > 3.14 and a < 3.15)
assert_eq(1rad, 1)

-- temporal (all execute immediately in interpreter)
var ran = false
after 100ms {
    ran = true
}
assert_eq(ran, true)

var count = 0
every 1s {
    count = count + 1
}
assert_eq(count, 1)

timeout 5s {
    println("ok")
}

println("all literal tests passed")
