trait Container {
    fn len(self) -> i32
    fn is_empty(self) -> bool
}

struct Bag {
    items: [i32],
}

impl Container for Bag {
    fn len(self) -> i32 { self.items.len() }
    fn is_empty(self) -> bool { self.items.len() == 0 }
}

let b = Bag { items: [1, 2, 3] }
assert(b.len() == 3, "bag len")
assert(!b.is_empty(), "bag not empty")
println("traits assoc test passed")
