-- Sorting benchmark

fn generate_random_array(n, seed) {
    var arr = []
    var s = seed
    for i in range(n) {
        s = (s * 1103515245 + 12345) % 2147483648
        arr.push(s % 10000)
    }
    arr
}

fn bubble_sort(arr) {
    let n = len(arr)
    var sorted = arr.clone()
    for i in range(n) {
        for j in range(n - i - 1) {
            if sorted[j] > sorted[j + 1] {
                let tmp = sorted[j]
                sorted[j] = sorted[j + 1]
                sorted[j + 1] = tmp
            }
        }
    }
    sorted
}

fn is_sorted(arr) {
    for i in range(len(arr) - 1) {
        if arr[i] > arr[i + 1] { return false }
    }
    true
}

-- Run benchmarks
let data = generate_random_array(500, 42)

-- Test builtin sort
let sorted_builtin = data.sort()
assert is_sorted(sorted_builtin)

-- Test bubble sort
let sorted_bubble = bubble_sort(data)
assert is_sorted(sorted_bubble)

assert len(sorted_builtin) == 500
