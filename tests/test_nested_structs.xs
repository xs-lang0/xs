struct Inner { x: int, y: int }
struct Outer { name: str, inner: any }

let o = Outer { name: "test", inner: Inner { x: 1, y: 2 } }
assert(o.name == "test", "outer field")
assert(o.inner.x == 1, "nested struct field x")
assert(o.inner.y == 2, "nested struct field y")

println("All nested struct tests passed!")
