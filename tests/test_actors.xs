-- Test actor declaration, spawn, method calls, and send operator

actor Counter {
    var count = 0

    fn increment() {
        count = count + 1
    }

    fn get() {
        count
    }

    fn handle(msg) {
        if msg == "increment" {
            count = count + 1
        } else if msg == "get" {
            count
        } else {
            null
        }
    }
}

-- spawn Counter is syntactic sugar for Counter()
let c = spawn Counter

-- Call methods via dot notation
c.increment()
c.increment()
let result = c.get()
println("Actor count: {result}")
assert(result == 2, "expected count 2 after two increments")

-- Test send operator (dispatches to handle function)
c ! "increment"
let result2 = c.get()
println("Actor count after send: {result2}")
assert(result2 == 3, "expected count 3 after send increment")

-- Test send with get message
let result3 = c ! "get"
println("Actor count via send: {result3}")
assert(result3 == 3, "expected count 3 via send get")

println("Actor test passed!")
