let x = match 2 { 1 => "a", 2 => "b", _ => "c" }
assert(x == "b", "single-line match with commas")

let y = match 3 { 1 => "a"; 2 => "b"; _ => "c" }
assert(y == "c", "single-line match with semicolons")

let z = match 1 { 1 => "first"; _ => "other" }
assert(z == "first", "semicolons work in match")

println("All match separator tests passed!")
