-- string operations benchmark

var result = ""
for i in 0..500 {
    result = result + str(i) + " "
}
assert(result.len() > 0, "string built")

-- string methods
let words = result.trim().split(" ")
assert(words.len() == 500, "split correct count")

var upper_count = 0
for w in words {
    let u = w.upper()
    if u == w { upper_count = upper_count + 1 }
}

println("processed {words.len()} words")
