// XS Language -- Match Expression Tests
// Tests: literal patterns, variable capture, OR patterns, config lookup

let conf = #{
    "controls": #{
        "up": "w",
        "down": "s",
        "left": "a",
        "right": "d"
    }
}

// Simulate a key press
let dir = conf.controls.up

// Match against values
match dir {
    "w" => println("Moving up"),
    "s" => println("Moving down"),
    "a" => println("Moving left"),
    "d" => println("Moving right"),
    _ => println("Invalid direction")
}

// OR patterns: multiple options in one arm
match dir {
    "w" | "s" => println("vertical axis"),
    "a" | "d" => println("horizontal axis"),
    _ => println("unknown")
}

// Literal OR patterns
let n = 2
match n {
    1 | 2 | 3 => println("small"),
    4 | 5     => println("medium"),
    _         => println("large")
}

// Match with different types
println("")
println("--- Type matching ---")
let values = [42, "hello", true, 3.14, null]
for v in values {
    let t = type(v)
    match t {
        "int"   => println("{v} is an integer"),
        "str"   => println("{v} is a string"),
        "bool"  => println("{v} is a boolean"),
        "float" => println("{v} is a float"),
        "null"  => println("null value"),
        _       => println("{v} is something else")
    }
}

// Match with computed values
println("")
println("--- Match with ranges ---")
for score in [95, 85, 75, 65, 45] {
    if score >= 90 {
        println("Score {score}: A")
    } else if score >= 80 {
        println("Score {score}: B")
    } else if score >= 70 {
        println("Score {score}: C")
    } else if score >= 60 {
        println("Score {score}: D")
    } else {
        println("Score {score}: F")
    }
}

// Nested match
println("")
println("--- Nested match ---")
let cmd = "greet"
let target = "world"

match cmd {
    "greet" => {
        match target {
            "world" => println("Hello, World!"),
            "xs"    => println("Hello, XS!"),
            _       => println("Hello, {target}!")
        }
    },
    "bye" => println("Goodbye!"),
    _ => println("Unknown command: {cmd}")
}
