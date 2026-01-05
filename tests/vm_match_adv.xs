-- Test advanced pattern matching in VM mode

fn test_match_or_pattern() {
    let x = 2
    let r = match x {
        1 | 2 => "one or two"
        3 | 4 => "three or four"
        _ => "other"
    }
    assert(r == "one or two", "or pattern")
}

fn test_match_guard() {
    let x = 15
    let r = match x {
        n if n > 10 => "big"
        n if n > 5 => "medium"
        _ => "small"
    }
    assert(r == "big", "guard pattern")
}

fn test_match_tuple_pattern() {
    let point = (3, 4)
    let r = match point {
        (0, 0) => "origin"
        (x, 0) => "x-axis"
        (0, y) => "y-axis"
        (x, y) => "point at ({x}, {y})"
    }
    assert(r == "point at (3, 4)", "tuple pattern")
}

fn test_match_range_pattern() {
    let score = 85
    let grade = match score {
        90..=100 => "A"
        80..=89 => "B"
        70..=79 => "C"
        _ => "F"
    }
    assert(grade == "B", "range pattern: 85 -> B")
}

fn test_match_string_literal() {
    let cmd = "quit"
    let r = match cmd {
        "start" => 1
        "stop" => 2
        "quit" => 3
        _ => 0
    }
    assert(r == 3, "string literal pattern")
}

fn test_match_bool() {
    let b = false
    let r = match b {
        true => "yes"
        false => "no"
    }
    assert(r == "no", "bool pattern")
}

fn test_match_nested() {
    fn classify(x) {
        match x {
            0 => "zero"
            n if n > 0 => match n {
                1 => "one"
                2 => "two"
                _ => "many"
            }
            _ => "negative"
        }
    }
    assert(classify(0) == "zero", "nested match zero")
    assert(classify(1) == "one", "nested match one")
    assert(classify(5) == "many", "nested match many")
    assert(classify(-1) == "negative", "nested match negative")
}

fn test_match_struct_pattern() {
    let p = {"x": 10, "y": 20}
    let r = match p {
        { x: x, y: y } => x + y
        _ => 0
    }
    assert(r == 30, "struct pattern: x + y")
}

fn test_match_slice_pattern() {
    let arr = [1, 2, 3]
    let r = match arr {
        [a, b, c] => a + b + c
        _ => 0
    }
    assert(r == 6, "slice pattern: a + b + c")
}

fn main() {
    test_match_or_pattern()
    test_match_guard()
    test_match_tuple_pattern()
    test_match_range_pattern()
    test_match_string_literal()
    test_match_bool()
    test_match_nested()
    test_match_struct_pattern()
    test_match_slice_pattern()
    println("vm_match_adv: all tests passed")
}
