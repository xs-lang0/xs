-- Test labeled break/continue for nested loops
-- Label syntax: `label: for/while/loop` and `break label` / `continue label`

-- === Test 1: Labeled break exits outer loop ===
var found_i = -1
var found_j = -1
outer: for i in range(5) {
    for j in range(5) {
        if i * j == 6 {
            found_i = i
            found_j = j
            break outer
        }
    }
}
assert(found_i == 2, "labeled break outer i should be 2, got " + str(found_i))
assert(found_j == 3, "labeled break outer j should be 3, got " + str(found_j))

-- === Test 2: Unlabeled break only exits inner loop ===
var last_i = -1
var last_j = -1
for i in range(3) {
    for j in range(5) {
        if j == 2 { break }
        last_j = j
    }
    last_i = i
}
assert(last_i == 2, "unlabeled break last_i should be 2")
assert(last_j == 1, "unlabeled break last_j should be 1")

-- === Test 3: Labeled continue skips to next outer iteration ===
var visited = []
skip: for i in range(4) {
    for j in range(4) {
        if j == 1 { continue skip }
        visited.push([i, j])
    }
}
-- Each outer iteration: j==0 is visited, then j==1 triggers continue skip
assert(len(visited) == 4, "labeled continue should visit 4 pairs, got " + str(len(visited)))
assert(visited[0][0] == 0, "first visit i==0")
assert(visited[0][1] == 0, "first visit j==0")
assert(visited[1][0] == 1, "second visit i==1")
assert(visited[1][1] == 0, "second visit j==0")
assert(visited[2][0] == 2, "third visit i==2")
assert(visited[3][0] == 3, "fourth visit i==3")

-- === Test 4: Unlabeled continue only skips inner iteration ===
var count = 0
for i in range(3) {
    for j in range(3) {
        if j == 1 { continue }
        count = count + 1
    }
}
assert(count == 6, "unlabeled continue in inner loop: count should be 6, got " + str(count))

-- === Test 5: Labeled break with while loop ===
var wi = 0
var wj = 0
outer_w: while wi < 5 {
    wj = 0
    while wj < 5 {
        if wi + wj == 7 {
            break outer_w
        }
        wj = wj + 1
    }
    wi = wi + 1
}
assert(wi + wj == 7, "labeled break while: wi+wj should be 7, got " + str(wi + wj))

-- === Test 6: Labeled continue with while loop ===
var wc_count = 0
var wc_i = 0
row: while wc_i < 3 {
    var wc_j = 0
    while wc_j < 3 {
        wc_j = wc_j + 1
        if wc_j == 2 {
            wc_i = wc_i + 1
            continue row
        }
        wc_count = wc_count + 1
    }
    wc_i = wc_i + 1
}
-- Each outer iteration: j goes 1 (count++), then j==2 triggers continue row
assert(wc_count == 3, "labeled continue while: count should be 3, got " + str(wc_count))

-- === Test 7: Flag-based nested break (no labels) ===
var fi = -1
var fj = -1
var done = false
for i in range(5) {
    for j in range(5) {
        if i * j == 6 {
            fi = i
            fj = j
            done = true
            break
        }
    }
    if done { break }
}
assert(fi == 2, "flag-based nested break i")
assert(fj == 3, "flag-based nested break j")

-- === Test 8: Triple-nested with labeled break ===
var ti = -1
var tj = -1
var tk = -1
top: for i in range(3) {
    for j in range(3) {
        for k in range(3) {
            if i + j + k == 4 {
                ti = i
                tj = j
                tk = k
                break top
            }
        }
    }
}
-- First solution: i=0, j=2, k=2 (0+2+2=4)
assert(ti == 0, "triple nested labeled break i should be 0, got " + str(ti))
assert(tj == 2, "triple nested labeled break j should be 2, got " + str(tj))
assert(tk == 2, "triple nested labeled break k should be 2, got " + str(tk))

println("All labeled loop tests passed!")
