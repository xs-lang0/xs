-- regex_demo.xs: regex as a first-class type

println("--- Regex Literals ---")

let digits = /[0-9]+/
let email = /[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]+/

println("type: {type(digits)}")
println("pattern: {digits}")

println("\n--- Regex Methods ---")

println(digits.test("hello 42 world"))   -- true
println(digits.test("no numbers here"))  -- false
println(digits.match("price: $42.99"))   -- 42
println(digits.replace("call 555-1234", "XXX"))

println("\n--- Regex in Match ---")

fn classify(input) {
    return match input {
        /[0-9]+/ => "number"
        /[a-zA-Z_][a-zA-Z0-9_]*/ => "identifier"
        _ => "symbol"
    }
}

println(classify("42"))       -- number
println(classify("hello"))    -- identifier
println(classify("???"))      -- symbol

println("\n--- Typed Regex ---")

let pat: re = /[a-z]+/
assert_eq(type(pat), "re")
assert(pat.test("hello"))
assert(!pat.test("123"))

println("All regex demos passed!")
