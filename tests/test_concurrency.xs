-- concurrency: spawn, channels, actors, async/await, nursery

-- spawn executes immediately
var spawned = false
spawn { spawned = true }
assert(spawned, "spawn executed")

-- spawn returns task handle
let t = spawn { 1 + 2 }
assert_eq(t["_status"], "done")
assert_eq(t["_result"], 3)

-- channels
let ch = channel()
ch.send(1)
ch.send(2)
ch.send(3)
assert_eq(ch.recv(), 1)
assert_eq(ch.recv(), 2)
assert_eq(ch.recv(), 3)
assert_eq(ch.len(), 0)
assert(ch.is_empty(), "empty after recv all")

-- bounded channels
let bch = channel(2)
bch.send("a")
bch.send("b")
assert_eq(bch.len(), 2)
assert(bch.is_full(), "full at capacity")
assert_eq(bch.recv(), "a")
assert(!bch.is_full(), "not full after recv")

-- actors
actor Counter {
    var count = 0
    fn increment() { count = count + 1 }
    fn get() { count }
    fn handle(msg) {
        if msg == "increment" { count = count + 1 }
        elif msg == "get" { count }
        else { null }
    }
}
let c = spawn Counter
c.increment()
c.increment()
assert_eq(c.get(), 2)
c ! "increment"
assert_eq(c.get(), 3)

-- async/await
async fn compute(x) { return x * 2 }
let r = await compute(21)
assert_eq(r, 42)

-- nursery
var results = []
nursery {
    spawn { results.push("a") }
    spawn { results.push("b") }
    spawn { results.push("c") }
}
assert_eq(results.sort(), ["a", "b", "c"])

println("test_concurrency: all passed")
