-- Test all array methods from the XS language reference (Section 21)

var passed = 0
var failed = 0

fn assert_eq(actual, expected, label) {
    if actual == expected {
        passed = passed + 1
    } else {
        print("FAIL: " ++ label ++ " expected=" ++ str(expected) ++ " got=" ++ str(actual))
        failed = failed + 1
    }
}

fn assert_true(val, label) {
    if val {
        passed = passed + 1
    } else {
        print("FAIL: " ++ label ++ " expected true, got false")
        failed = failed + 1
    }
}

fn assert_false(val, label) {
    if !val {
        passed = passed + 1
    } else {
        print("FAIL: " ++ label ++ " expected false, got true")
        failed = failed + 1
    }
}

-- Test push / append
var a1 = [1, 2, 3]
a1.push(4)
assert_eq(a1.len(), 4, "push length")
assert_eq(a1[3], 4, "push value")

var a1b = [10, 20]
a1b.append(30)
assert_eq(a1b.len(), 3, "append length")
assert_eq(a1b[2], 30, "append value")

-- Test pop
var a2 = [1, 2, 3]
var popped = a2.pop()
assert_eq(popped, 3, "pop value")
assert_eq(a2.len(), 2, "pop length")

-- Test insert
var a3 = [1, 3, 4]
a3.insert(1, 2)
assert_eq(a3[1], 2, "insert value")
assert_eq(a3.len(), 4, "insert length")

-- Test remove
var a4 = [1, 2, 3, 4]
var removed = a4.remove(1)
assert_eq(removed, 2, "remove value")
assert_eq(a4.len(), 3, "remove length")

-- Test slice
let a5 = [1, 2, 3, 4, 5]
let sliced = a5.slice(1, 3)
assert_eq(sliced.len(), 2, "slice length")
assert_eq(sliced[0], 2, "slice[0]")
assert_eq(sliced[1], 3, "slice[1]")

-- Test map
let a6 = [1, 2, 3]
let mapped = a6.map(|x| x * 2)
assert_eq(mapped[0], 2, "map[0]")
assert_eq(mapped[1], 4, "map[1]")
assert_eq(mapped[2], 6, "map[2]")

-- Test filter
let a7 = [1, 2, 3, 4, 5]
let filtered = a7.filter(|x| x > 3)
assert_eq(filtered.len(), 2, "filter length")
assert_eq(filtered[0], 4, "filter[0]")
assert_eq(filtered[1], 5, "filter[1]")

-- Test fold
let a8 = [1, 2, 3, 4, 5]
let total = a8.fold(0, |acc, x| acc + x)
assert_eq(total, 15, "fold sum")

-- Test reduce
let a8b = [1, 2, 3, 4, 5]
let total_r = a8b.reduce(|acc, x| acc + x, 0)
assert_eq(total_r, 15, "reduce sum")

-- Test for_each / each
let a9 = [1, 2, 3]
var sum9 = 0
a9.for_each(|x| { sum9 = sum9 + x })
assert_eq(sum9, 6, "for_each sum")

var sum9b = 0
a9.each(|x| { sum9b = sum9b + x })
assert_eq(sum9b, 6, "each sum")

-- Test flat_map
let a10 = [1, 2, 3]
let fm = a10.flat_map(|x| [x, x * 10])
assert_eq(fm.len(), 6, "flat_map length")
assert_eq(fm[0], 1, "flat_map[0]")
assert_eq(fm[1], 10, "flat_map[1]")
assert_eq(fm[2], 2, "flat_map[2]")

-- Test flatten
let a11 = [[1, 2], [3, 4], [5]]
let flat = a11.flatten()
assert_eq(flat.len(), 5, "flatten length")
assert_eq(flat[0], 1, "flatten[0]")
assert_eq(flat[4], 5, "flatten[4]")

-- Test zip
let a12a = [1, 2, 3]
let a12b = [4, 5, 6]
let zipped = a12a.zip(a12b)
assert_eq(zipped.len(), 3, "zip length")

-- Test zip_with
let a13a = [1, 2, 3]
let a13b = [10, 20, 30]
let zw = a13a.zip_with(a13b, |a, b| a + b)
assert_eq(zw[0], 11, "zip_with[0]")
assert_eq(zw[1], 22, "zip_with[1]")
assert_eq(zw[2], 33, "zip_with[2]")

-- Test enumerate
let a14 = ["a", "b", "c"]
let enumed = a14.enumerate()
assert_eq(enumed.len(), 3, "enumerate length")

-- Test find
let a15 = [1, 2, 3, 4, 5]
let found = a15.find(|x| x > 3)
assert_eq(found, 4, "find value")

let not_found = a15.find(|x| x > 10)
assert_eq(not_found, null, "find null")

-- Test any
let a16 = [1, 2, 3, 4, 5]
assert_true(a16.any(|x| x > 4), "any true")
assert_false(a16.any(|x| x > 10), "any false")

-- Test all
let a17 = [2, 4, 6, 8]
assert_true(a17.all(|x| x % 2 == 0), "all true")
assert_false(a17.all(|x| x > 5), "all false")

-- Test contains / includes
let a18 = [1, 2, 3, 4, 5]
assert_true(a18.contains(3), "contains true")
assert_false(a18.contains(99), "contains false")
assert_true(a18.includes(3), "includes true")
assert_false(a18.includes(99), "includes false")

-- Test index_of / find_index
let a19 = [10, 20, 30, 40]
assert_eq(a19.index_of(30), 2, "index_of found")
assert_eq(a19.index_of(99), -1, "index_of not found")
assert_eq(a19.find_index(20), 1, "find_index found")

-- Test sort (in place)
var a20 = [3, 1, 4, 1, 5]
a20.sort()
assert_eq(a20[0], 1, "sort[0]")
assert_eq(a20[1], 1, "sort[1]")
assert_eq(a20[2], 3, "sort[2]")
assert_eq(a20[3], 4, "sort[3]")
assert_eq(a20[4], 5, "sort[4]")

-- Test sorted (returns copy)
let a21 = [5, 3, 1, 4, 2]
let s21 = a21.sorted()
assert_eq(s21[0], 1, "sorted[0]")
assert_eq(s21[4], 5, "sorted[4]")
assert_eq(a21[0], 5, "sorted original unchanged")

-- Test sort_by
let a22 = ["banana", "apple", "fig"]
let s22 = a22.sort_by(|w| w.len())
assert_eq(s22[0], "fig", "sort_by[0]")
assert_eq(s22[2], "banana", "sort_by[2]")

-- Test reverse (in place)
var a23 = [1, 2, 3]
a23.reverse()
assert_eq(a23[0], 3, "reverse[0]")
assert_eq(a23[2], 1, "reverse[2]")

-- Test reversed (returns copy)
let a24 = [1, 2, 3]
let r24 = a24.reversed()
assert_eq(r24[0], 3, "reversed[0]")
assert_eq(r24[2], 1, "reversed[2]")
assert_eq(a24[0], 1, "reversed original unchanged")

-- Test join
let a25 = ["hello", "world"]
assert_eq(a25.join(" "), "hello world", "join space")
assert_eq(a25.join(","), "hello,world", "join comma")

-- Test first / last
let a26 = [10, 20, 30]
assert_eq(a26.first(), 10, "first")
assert_eq(a26.last(), 30, "last")
let a26b = []
assert_eq(a26b.first(), null, "first empty")
assert_eq(a26b.last(), null, "last empty")

-- Test is_empty
assert_true([].is_empty(), "is_empty true")
assert_false([1].is_empty(), "is_empty false")

-- Test count
let a27 = [1, 2, 3, 4, 5]
assert_eq(a27.count(), 5, "count all")
assert_eq(a27.count(|x| x % 2 == 0), 2, "count evens")

-- Test sum
let a28 = [1, 2, 3, 4, 5]
assert_eq(a28.sum(), 15, "sum")

-- Test sum_by
let a28b = [1, 2, 3]
let sq_sum = a28b.sum_by(|x| x * x)
assert_eq(sq_sum, 14, "sum_by squares")

-- Test product
let a29 = [1, 2, 3, 4]
assert_eq(a29.product(), 24, "product")

-- Test min / max
let a30 = [5, 2, 8, 1, 9]
assert_eq(a30.min(), 1, "min")
assert_eq(a30.max(), 9, "max")

-- Test min_by / max_by
let a31 = ["banana", "fig", "apple"]
let shortest = a31.min_by(|w| w.len())
assert_eq(shortest, "fig", "min_by")
let longest = a31.max_by(|w| w.len())
assert_eq(longest, "banana", "max_by")

-- Test unique / dedup
let a32 = [1, 2, 2, 3, 3, 3, 1]
let u32 = a32.unique()
assert_eq(u32.len(), 3, "unique length")
assert_eq(u32[0], 1, "unique[0]")
assert_eq(u32[1], 2, "unique[1]")
assert_eq(u32[2], 3, "unique[2]")

let d32 = a32.dedup()
assert_eq(d32.len(), 3, "dedup length")

-- Test take
let a33 = [1, 2, 3, 4, 5]
let t33 = a33.take(3)
assert_eq(t33.len(), 3, "take length")
assert_eq(t33[2], 3, "take[2]")

-- Test take_while
let a33b = [1, 2, 3, 4, 5]
let tw = a33b.take_while(|x| x < 4)
assert_eq(tw.len(), 3, "take_while length")
assert_eq(tw[2], 3, "take_while[2]")

-- Test skip / drop
let a34 = [1, 2, 3, 4, 5]
let s34 = a34.skip(2)
assert_eq(s34.len(), 3, "skip length")
assert_eq(s34[0], 3, "skip[0]")

let d34 = a34.drop(2)
assert_eq(d34.len(), 3, "drop length")
assert_eq(d34[0], 3, "drop[0]")

-- Test drop_while
let a34b = [1, 2, 3, 4, 5]
let dw = a34b.drop_while(|x| x < 3)
assert_eq(dw.len(), 3, "drop_while length")
assert_eq(dw[0], 3, "drop_while[0]")

-- Test chunk
let a35 = [1, 2, 3, 4, 5]
let c35 = a35.chunk(2)
assert_eq(c35.len(), 3, "chunk count")
assert_eq(c35[0].len(), 2, "chunk[0] length")
assert_eq(c35[2].len(), 1, "chunk[2] length")

-- Test window
let a36 = [1, 2, 3, 4, 5]
let w36 = a36.window(3)
assert_eq(w36.len(), 3, "window count")
assert_eq(w36[0].len(), 3, "window[0] length")
assert_eq(w36[0][0], 1, "window[0][0]")
assert_eq(w36[1][0], 2, "window[1][0]")
assert_eq(w36[2][0], 3, "window[2][0]")

-- Test group_by
let a37 = [1, 2, 3, 4, 5]
let g37 = a37.group_by(|x| x % 2 == 0)
assert_eq(g37["false"].len(), 3, "group_by false count")
assert_eq(g37["true"].len(), 2, "group_by true count")

-- Test partition
let a38 = [1, 2, 3, 4, 5]
let p38 = a38.partition(|x| x > 3)
assert_eq(p38[0].len(), 2, "partition pass")
assert_eq(p38[1].len(), 3, "partition fail")

-- Test intersperse
let a39 = [1, 2, 3]
let i39 = a39.intersperse(0)
assert_eq(i39.len(), 5, "intersperse length")
assert_eq(i39[0], 1, "intersperse[0]")
assert_eq(i39[1], 0, "intersperse[1]")
assert_eq(i39[2], 2, "intersperse[2]")

-- Test rotate
let a40 = [1, 2, 3, 4, 5]
let r40 = a40.rotate(2)
assert_eq(r40[0], 3, "rotate[0]")
assert_eq(r40[1], 4, "rotate[1]")
assert_eq(r40[4], 2, "rotate[4]")

-- Test len
let a41 = [1, 2, 3]
assert_eq(a41.len(), 3, "len")

-- Summary
print("")
print("Array Methods Test Results:")
print("  Passed: " ++ str(passed))
print("  Failed: " ++ str(failed))

if failed > 0 {
    print("SOME TESTS FAILED!")
} else {
    print("ALL TESTS PASSED!")
}
