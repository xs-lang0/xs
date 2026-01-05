-- Test: Algebraic Effects — effect declarations, perform, handle, resume

effect Ask {
    fn prompt(msg) -> str
}

fn greet() {
    let name = perform Ask.prompt("What is your name?")
    "Hello, {name}!"
}

let result = handle greet() {
    Ask.prompt(msg) => resume("World")
}
assert(result == "Hello, World!", "algebraic effects basic")

-- Test 2: multiple perform calls
effect Log {
    fn log(msg)
}

let logs = []
handle {
    perform Log.log("first")
    perform Log.log("second")
} {
    Log.log(msg) => {
        logs.push(msg)
        resume(null)
    }
}
assert(logs.len() == 2, "effect: multiple performs")
assert(logs[0] == "first", "effect: first log")
assert(logs[1] == "second", "effect: second log")

-- Test 3: effect with resume value
effect Read {
    fn read(key) -> str
}

let val = handle {
    perform Read.read("x")
} {
    Read.read(key) => resume(42)
}
assert(val == 42, "effect: resume value")

-- Test 4: unhandled effect (should not crash, returns nil)
effect Missing {
    fn get()
}

println("Effects test passed!")
