-- Test for-in loops over maps in VM mode

fn test_for_map_keys() {
    let m = {"a": 1, "b": 2, "c": 3}
    var count = 0
    for k in m {
        count += 1
    }
    assert(count == 3, "for over map should iterate keys")
}

fn test_for_map_values() {
    let m = {"x": 10, "y": 20}
    var total = 0
    for k in m {
        total += m[k]
    }
    assert(total == 30, "map iteration should access values")
}

fn test_for_map_entries() {
    let m = {"a": 1, "b": 2}
    var found_a = false
    var found_b = false
    for (k, v) in entries(m) {
        if k == "a" { found_a = true }
        if k == "b" { found_b = true }
    }
    assert(found_a, "found key a")
    assert(found_b, "found key b")
}

fn test_for_array_tuple_destructure() {
    let pairs = [(1, "one"), (2, "two"), (3, "three")]
    var total = 0
    for (n, _) in pairs {
        total += n
    }
    assert(total == 6, "tuple destructure in for loop")
}

fn test_for_enumerate() {
    let arr = ["a", "b", "c"]
    var idx_sum = 0
    for (i, _) in enumerate(arr) {
        idx_sum += i
    }
    assert(idx_sum == 3, "enumerate destructure in for loop")
}

fn main() {
    test_for_map_keys()
    test_for_map_values()
    test_for_map_entries()
    test_for_array_tuple_destructure()
    test_for_enumerate()
    println("vm_for_map: all tests passed")
}
