-- Test list and map comprehensions in VM mode

fn test_basic_list_comp() {
    let result = [x * 2 for x in [1, 2, 3, 4, 5]]
    assert(len(result) == 5, "list comp length")
    assert(result[0] == 2, "list comp [0]")
    assert(result[4] == 10, "list comp [4]")
}

fn test_filtered_list_comp() {
    let result = [x for x in 1..10 if x % 2 == 0]
    assert(len(result) == 4, "filtered comp length: evens in 1..10")
    assert(result[0] == 2, "first even")
    assert(result[3] == 8, "last even")
}

fn test_range_list_comp() {
    let squares = [i * i for i in [1, 2, 3, 4, 5]]
    assert(len(squares) == 5, "squares length")
    assert(squares[0] == 1, "1 squared")
    assert(squares[4] == 25, "5 squared")
}

fn test_string_comp() {
    let letters = chars("hello")
    let result = [c for c in letters]
    assert(len(result) == 5, "chars length")
    assert(result[0] == "h", "first char")
}

fn test_transform() {
    -- Simple transform equivalent to map comp
    var result = {}
    for n in [1, 2, 3] {
        result[str(n)] = n * n
    }
    assert(result["1"] == 1, "transform 1")
    assert(result["2"] == 4, "transform 2")
    assert(result["3"] == 9, "transform 3")
}

fn main() {
    test_basic_list_comp()
    test_filtered_list_comp()
    test_range_list_comp()
    test_string_comp()
    test_transform()
    println("vm_list_comp: all tests passed")
}
