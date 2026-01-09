-- algebraic effects: effect, perform, handle, resume

effect Ask {
    fn prompt(msg) -> str
}

-- basic effect handling
fn greet() {
    let name = perform Ask.prompt("name?")
    return "Hello, {name}!"
}

let result = handle greet() {
    Ask.prompt(msg) => resume("World")
}
assert_eq(result, "Hello, World!")

-- effect with accumulator
effect Log {
    fn log(msg)
}

var logs = []
handle {
    perform Log.log("first")
    perform Log.log("second")
    perform Log.log("third")
} {
    Log.log(msg) => {
        logs.push(msg)
        resume(null)
    }
}
assert_eq(logs, ["first", "second", "third"])

println("test_effects: all passed")
