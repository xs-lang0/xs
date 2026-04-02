-- new language features: do expressions, with, named arguments

-- do expressions
let x = do {
    let a = 10
    let b = 20
    a + b
}
assert_eq(x, 30)

let grade = do {
    let score = 85
    if score >= 90 { "A" }
    elif score >= 80 { "B" }
    else { "C" }
}
assert_eq(grade, "B")

-- nested do
let nested = do {
    let inner = do { 5 * 5 }
    inner + 1
}
assert_eq(nested, 26)

-- with resource management
struct Resource { name }
impl Resource {
    fn close(self) { }
    fn read(self) { return "data from {self.name}" }
}
var data = ""
with Resource { name: "test.txt" } as f {
    data = f.read()
}
assert_eq(data, "data from test.txt")

-- with without as (just runs block with close)
with Resource { name: "temp" } as r {
    assert_eq(r.name, "temp")
}

-- named arguments
fn connect(host, port) {
    return "{host}:{port}"
}
assert_eq(connect(host: "localhost", port: 8080), "localhost:8080")
assert_eq(connect(port: 3000, host: "0.0.0.0"), "0.0.0.0:3000")

-- named args mixed with positional
fn greet(name, greeting) {
    return "{greeting}, {name}!"
}
assert_eq(greet("World", greeting: "Hello"), "Hello, World!")

-- named args with defaults
fn setup(host, port, debug) {
    if debug { return "{host}:{port} (debug)" }
    return "{host}:{port}"
}
assert_eq(setup(host: "localhost", port: 80, debug: false), "localhost:80")

println("test_new_features: all passed")
