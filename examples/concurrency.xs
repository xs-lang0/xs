-- concurrency.xs: spawn, channels, actors, async/await, nursery

println("--- Spawn ---")

-- spawn runs a block as a task
var results = []
let task = spawn {
    results.push("spawned!")
    42
}

println("task result: {task["_result"]}")
println("task status: {task["_status"]}")
assert_eq(task["_result"], 42)
assert_eq(results[0], "spawned!")

println("\n--- Channels ---")

-- channels are typed message queues
let ch = channel()

ch.send("ping")
ch.send("pong")
ch.send("done")

assert_eq(ch.len(), 3)

-- recv pulls in FIFO order
assert_eq(ch.recv(), "ping")
assert_eq(ch.recv(), "pong")
assert_eq(ch.recv(), "done")
assert(ch.is_empty(), "channel should be empty")
println("channel FIFO: works")

-- bounded channels have a max capacity
let bounded = channel(2)
bounded.send("a")
bounded.send("b")
assert(bounded.is_full(), "should be full at capacity 2")
println("bounded channel: full at {bounded.len()}")

bounded.recv()
assert(!bounded.is_full(), "not full after recv")
println("bounded channel: drained one, full={bounded.is_full()}")

println("\n--- Actors ---")

-- actors encapsulate state and respond to messages
actor BankAccount {
    var balance = 0

    fn deposit(amount) {
        balance = balance + amount
    }

    fn withdraw(amount) {
        if amount > balance {
            return Err("insufficient funds")
        }
        balance = balance - amount
        return Ok(balance)
    }

    fn get_balance() {
        return balance
    }

    -- handle() processes raw messages sent with !
    fn handle(msg) {
        if msg == "reset" {
            balance = 0
            return "reset done"
        }
        return "unknown: {msg}"
    }
}

let acct = spawn BankAccount

acct.deposit(100)
acct.deposit(50)
println("balance after deposits: {acct.get_balance()}")
assert_eq(acct.get_balance(), 150)

let r = acct.withdraw(30)
println("withdraw 30: {r}")
assert_eq(acct.get_balance(), 120)

let fail = acct.withdraw(500)
println("withdraw 500: {fail}")

-- send a raw message with !
acct ! "reset"
println("after reset: {acct.get_balance()}")
assert_eq(acct.get_balance(), 0)

println("\n--- Async/Await ---")

-- async functions return a future, await gets the value
async fn fetch_user(id) {
    return #{"id": id, "name": "User {id}"}
}

async fn fetch_score(id) {
    return id * 100
}

let user = await fetch_user(42)
let score = await fetch_score(42)
println("user: {user}")
println("score: {score}")
assert_eq(user["id"], 42)
assert_eq(score, 4200)

println("\n--- Nursery (Structured Concurrency) ---")

-- nursery waits for ALL spawned tasks before continuing
-- this guarantees no dangling tasks leak out

var collected = []

nursery {
    spawn { collected.push("task A") }
    spawn { collected.push("task B") }
    spawn { collected.push("task C") }
}

-- after nursery block, everything has completed
println("collected: {collected.sort()}")
assert_eq(collected.sort(), ["task A", "task B", "task C"])

-- nursery with channels for coordination
let pipe = channel()
var output = []

nursery {
    -- producer
    spawn {
        for i in 1..=3 {
            pipe.send(i * 10)
        }
    }
    -- consumer
    spawn {
        for i in 0..3 {
            output.push(pipe.recv())
        }
    }
}

println("pipe output: {output}")
assert_eq(output, [10, 20, 30])

println("\nAll good!")
