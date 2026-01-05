-- Line comment test
let x = 42

{- Block comment test -}
let y = 10

let z = x + {- inline comment -} y
assert(z == 52, "inline block comment")

{-
  Multi-line
  block comment
-}
assert(x == 42, "multiline block comment")

{- {- nested -} still in comment -}
let w = 1
assert(w == 1, "nested block comments")

println("Comment tests passed!")
