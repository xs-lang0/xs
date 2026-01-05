-- 1. async/await
async fn fetch_data(n) {
    n * 2
}

async fn process() {
    let v = await fetch_data(21)
    v
}

println(await process())

-- 2. nursery + spawn
var results = []
nursery {
    spawn { results = results ++ [1] }
    spawn { results = results ++ [2] }
    spawn { results = results ++ [3] }
}
println(results)

-- 3. Channels
let ch = channel(5)
ch.send(10)
ch.send(20)
ch.send(30)
println(ch.recv())
println(ch.recv())
println(ch.len())

-- 4. Simple actor-style via struct
struct Counter { }
impl Counter {
    fn new() { Counter { } }
}
var actor_count = 0
fn actor_send(msg) {
    if msg == "inc" { actor_count = actor_count + 1 }
    if msg == "get" { actor_count }
}
actor_send("inc")
actor_send("inc")
println(actor_send("get"))

println("2h concurrency passed")
