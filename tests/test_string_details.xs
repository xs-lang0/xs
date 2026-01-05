-- Escape sequences
assert("\n".len() == 1, "newline is 1 char")
assert("\t".len() == 1, "tab is 1 char")
assert("\\".len() == 1, "backslash is 1 char")
assert("\"".len() == 1, "quote is 1 char")

-- String indexing
let s = "hello"
assert(s[0] == "h", "index 0")
assert(s[4] == "o", "index 4")

-- .len() - bytes or chars?
let ascii = "hello"
assert(ascii.len() == 5, "ascii len")

-- .chars() return type
let c = "abc".chars()
assert(len(c) == 3, "chars count")
assert(c[0] == "a", "chars returns strings")

-- Unicode (if supported)
-- let emoji = "😀"
-- println("emoji len: {emoji.len()}")

println("String details test passed!")
