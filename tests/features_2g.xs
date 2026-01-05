-- Basic effect: logging
effect Log {
    fn log(msg)
}

fn greet(name) {
    perform Log.log("greeting {name}")
    "Hello, {name}!"
}

let result = handle greet("Alice") {
    Log.log(msg) => {
        println("[LOG] {msg}")
        resume null
    }
}
println(result)

-- Effect: state
effect State {
    fn get()
    fn put(val)
}

fn counter() {
    let v = perform State.get()
    perform State.put(v + 1)
    perform State.get()
}

var state_val = 0
let final_state = handle counter() {
    State.get() => {
        resume state_val
    }
    State.put(val) => {
        state_val = val
        resume null
    }
}
println(final_state)
println(state_val)

println("2g effects passed")
