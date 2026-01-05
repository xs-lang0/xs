-- tests/test_stdlib_verify.xs — verify stdlib functions (string, fmt) work at runtime
import string
import fmt

-- String functions
let lev = string.levenshtein("kitten", "sitting")
assert(lev > 0, "levenshtein")

assert(string.camel_to_snake("helloWorld") == "hello_world", "camel_to_snake")
assert(string.snake_to_camel("hello_world") == "helloWorld", "snake_to_camel")
assert(string.is_numeric("123") == true, "is_numeric true")
assert(string.is_numeric("abc") == false, "is_numeric false")

let w = string.words("hello world foo")
assert(len(w) == 3, "words count")

-- Fmt functions
let n = fmt.number(1234567)
assert(type(n) == "str", "fmt.number returns string")

let ord = fmt.ordinal(3)
assert(ord == "3rd", "fmt.ordinal")

println("All stdlib verification tests passed!")
