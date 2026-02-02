-- demo for --check static analysis
-- run with: xs --check examples/check_demo.xs
-- this file has intentional errors to show the error recovery

let x: int = "hello"
let y: bool = 42
let z = undefined_name
fn add(a: int, b: int) -> int { return "not an int" }
let ok = 10 + 20
