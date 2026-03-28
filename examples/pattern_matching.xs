-- pattern_matching.xs: all pattern types in one place

println("--- Literal Patterns ---")

fn describe_num(n) {
    return match n {
        0 => "zero"
        1 => "one"
        42 => "the answer"
        _ => "something else"
    }
}
println(describe_num(0))
println(describe_num(42))
println(describe_num(99))

println("\n--- Guard Patterns ---")

fn classify_age(age) {
    return match age {
        n if n < 0 => "invalid"
        n if n < 18 => "minor"
        n if n < 65 => "adult"
        _ => "senior"
    }
}
println(classify_age(10))
println(classify_age(30))
println(classify_age(70))

println("\n--- Tuple Patterns ---")

fn describe_point(point) {
    return match point {
        (0, 0) => "origin"
        (x, 0) => "on x-axis at {x}"
        (0, y) => "on y-axis at {y}"
        (x, y) => "({x}, {y})"
    }
}
println(describe_point((0, 0)))
println(describe_point((5, 0)))
println(describe_point((3, 4)))

println("\n--- Enum Patterns ---")

enum Shape {
    Circle(r),
    Rect(w, h)
}

fn area(shape) {
    return match shape {
        Shape::Circle(r) => 3.14 * r * r
        Shape::Rect(w, h) => w * h
        _ => 0
    }
}
println("circle: {area(Shape::Circle(5))}")
println("rect: {area(Shape::Rect(3, 4))}")

println("\n--- Or Patterns ---")

fn is_weekend(day) {
    return match day {
        "sat" | "sun" => true
        _ => false
    }
}
assert(is_weekend("sat"))
assert(!is_weekend("mon"))

println("\n--- String Prefix Patterns ---")

fn protocol(url) {
    return match url {
        "https://" ++ _ => "secure"
        "http://" ++ _ => "plain"
        _ => "unknown"
    }
}
println(protocol("https://example.com"))
println(protocol("ftp://files.org"))

println("\n--- Regex Patterns ---")

fn token_type(tok) {
    return match tok {
        /[0-9]+/ => "number"
        /[a-zA-Z_]+/ => "word"
        _ => "symbol"
    }
}
println(token_type("42"))
println(token_type("hello"))
println(token_type("+="))

println("\n--- Capture Patterns ---")

fn range_check(val) {
    return match val {
        n @ 1..=10 => "small ({n})"
        n @ 11..=100 => "medium ({n})"
        n => "big ({n})"
    }
}
println(range_check(5))
println(range_check(50))
println(range_check(500))

println("\n--- Slice Patterns ---")

fn first_and_rest(arr) {
    return match arr {
        [] => "empty"
        [only] => "just {only}"
        [first, ..rest] => "first={first}, rest has {len(rest)} items"
    }
}
println(first_and_rest([]))
println(first_and_rest([42]))
println(first_and_rest([1, 2, 3, 4]))

println("\nAll pattern matching demos passed!")
