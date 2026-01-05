-- ============================================================
-- Comprehensive concurrency tests for XS
-- Tests: actors, channels, spawn, nursery
-- ============================================================

-- ============================================================
-- 1. ACTOR TESTS
-- ============================================================

-- 1a. Basic actor declaration and spawn
actor Counter {
    var count = 0

    fn increment() {
        count = count + 1
    }

    fn decrement() {
        count = count - 1
    }

    fn get() {
        count
    }

    fn reset() {
        count = 0
    }

    fn handle(msg) {
        if msg == "increment" {
            count = count + 1
        } else if msg == "decrement" {
            count = count - 1
        } else if msg == "get" {
            count
        } else if msg == "reset" {
            count = 0
        } else {
            null
        }
    }
}

let c = spawn Counter
assert(c.get() == 0, "1a: initial count should be 0")
c.increment()
c.increment()
c.increment()
assert(c.get() == 3, "1a: count should be 3 after 3 increments")
c.decrement()
assert(c.get() == 2, "1a: count should be 2 after decrement")
c.reset()
assert(c.get() == 0, "1a: count should be 0 after reset")
println("1a. Basic actor: PASSED")

-- 1b. Actor send operator (!)
let c2 = spawn Counter
c2 ! "increment"
c2 ! "increment"
c2 ! "increment"
let val = c2 ! "get"
assert(val == 3, "1b: send get should return 3")
c2 ! "decrement"
let val2 = c2 ! "get"
assert(val2 == 2, "1b: send get should return 2 after decrement")
println("1b. Actor send operator: PASSED")

-- 1c. Actor with complex state
actor BankAccount {
    var balance = 0

    fn deposit(amount) {
        balance = balance + amount
        balance
    }

    fn withdraw(amount) {
        if amount > balance {
            -1
        } else {
            balance = balance - amount
            balance
        }
    }

    fn get_balance() {
        balance
    }

    fn handle(msg) {
        balance
    }
}

let acct = spawn BankAccount
acct.deposit(100)
acct.deposit(50)
assert(acct.get_balance() == 150, "1c: balance should be 150")
let w1 = acct.withdraw(30)
assert(w1 == 120, "1c: withdraw should return 120")
let w2 = acct.withdraw(200)
assert(w2 == -1, "1c: overdraft should return -1")
assert(acct.get_balance() == 120, "1c: balance unchanged after failed withdraw")
println("1c. Actor with complex state: PASSED")

-- 1d. Multiple actor instances are independent
let a1 = spawn Counter
let a2 = spawn Counter
a1.increment()
a1.increment()
a2.increment()
assert(a1.get() == 2, "1d: a1 count should be 2")
assert(a2.get() == 1, "1d: a2 count should be 1")
println("1d. Multiple actor instances: PASSED")

-- 1e. Actor method with arguments
actor Accumulator {
    var total = 0

    fn add(x) {
        total = total + x
    }

    fn get() {
        total
    }

    fn handle(msg) {
        total = total + msg
        total
    }
}

let acc = spawn Accumulator
acc.add(10)
acc.add(20)
acc.add(30)
assert(acc.get() == 60, "1e: accumulator total should be 60")

-- Send numeric values
let r = acc ! 5
assert(r == 65, "1e: after send 5, total should be 65")
println("1e. Actor with arguments: PASSED")

-- ============================================================
-- 2. CHANNEL TESTS
-- ============================================================

-- 2a. Basic channel send/recv
let ch = channel()
ch.send(42)
ch.send("hello")
ch.send(true)
let v1 = ch.recv()
let v2 = ch.recv()
let v3 = ch.recv()
assert(v1 == 42, "2a: first recv should be 42")
assert(v2 == "hello", "2a: second recv should be 'hello'")
assert(v3 == true, "2a: third recv should be true")
println("2a. Basic channel: PASSED")

-- 2b. Channel len and is_empty
let ch2 = channel()
assert(ch2.is_empty() == true, "2b: new channel is empty")
assert(ch2.len() == 0, "2b: new channel len is 0")
ch2.send(1)
ch2.send(2)
assert(ch2.is_empty() == false, "2b: channel not empty after send")
assert(ch2.len() == 2, "2b: channel len is 2")
ch2.recv()
assert(ch2.len() == 1, "2b: channel len is 1 after recv")
ch2.recv()
assert(ch2.is_empty() == true, "2b: channel empty after all recv")
println("2b. Channel len/is_empty: PASSED")

-- 2c. Channel with capacity
let ch3 = channel(3)
ch3.send("a")
ch3.send("b")
ch3.send("c")
-- Fourth send should fail silently (capacity 3)
ch3.send("d")
-- Should still only have 3 items
assert(ch3.len() == 3, "2c: capped channel should have 3 items")
assert(ch3.recv() == "a", "2c: FIFO order preserved")
assert(ch3.recv() == "b", "2c: FIFO order preserved")
assert(ch3.recv() == "c", "2c: FIFO order preserved")
println("2c. Channel with capacity: PASSED")

-- 2d. Channel preserves complex values
let ch4 = channel()
ch4.send([1, 2, 3])
ch4.send(#{x: 10, y: 20})
let arr = ch4.recv()
let map_val = ch4.recv()
assert(len(arr) == 3, "2d: array preserved in channel")
assert(arr[0] == 1, "2d: array[0] preserved")
println("2d. Channel complex values: PASSED")

-- ============================================================
-- 3. SPAWN TESTS
-- ============================================================

-- 3a. Spawn block outside nursery (immediate execution)
var s_result = 0
spawn { s_result = 42 }
assert(s_result == 42, "3a: spawn block should execute immediately")
println("3a. Spawn block immediate: PASSED")

-- 3b. Spawn fn outside nursery
var s_result2 = 0
spawn fn() { s_result2 = 99 }
assert(s_result2 == 99, "3b: spawn fn should execute immediately")
println("3b. Spawn fn immediate: PASSED")

-- 3c. Spawn named function outside nursery
var s_result3 = 0
fn set_value() { s_result3 = 77 }
spawn set_value
assert(s_result3 == 77, "3c: spawn named fn should execute immediately")
println("3c. Spawn named fn: PASSED")

-- 3d. Spawn actor (creates instance)
let spawned_counter = spawn Counter
spawned_counter.increment()
assert(spawned_counter.get() == 1, "3d: spawned actor should work")
println("3d. Spawn actor: PASSED")

-- ============================================================
-- 4. NURSERY TESTS
-- ============================================================

-- 4a. Basic nursery with spawn blocks
var n_total = 0
nursery {
    spawn { n_total = n_total + 10 }
    spawn { n_total = n_total + 20 }
    spawn { n_total = n_total + 30 }
}
assert(n_total == 60, "4a: nursery total should be 60")
println("4a. Basic nursery: PASSED")

-- 4b. Nursery with spawn fn
var n_total2 = 0
nursery {
    spawn fn() { n_total2 = n_total2 + 5 }
    spawn fn() { n_total2 = n_total2 + 15 }
}
assert(n_total2 == 20, "4b: nursery fn total should be 20")
println("4b. Nursery with fn: PASSED")

-- 4c. Nursery with named functions
var n_total3 = 0
fn task_a() { n_total3 = n_total3 + 100 }
fn task_b() { n_total3 = n_total3 + 200 }
nursery {
    spawn task_a
    spawn task_b
}
assert(n_total3 == 300, "4c: nursery named fn total should be 300")
println("4c. Nursery named fn: PASSED")

-- 4d. Nursery collects results via channel
let result_ch = channel()
nursery {
    spawn { result_ch.send(1 * 1) }
    spawn { result_ch.send(2 * 2) }
    spawn { result_ch.send(3 * 3) }
}
let r1 = result_ch.recv()
let r2 = result_ch.recv()
let r3 = result_ch.recv()
assert(r1 == 1, "4d: first result should be 1")
assert(r2 == 4, "4d: second result should be 4")
assert(r3 == 9, "4d: third result should be 9")
println("4d. Nursery with channels: PASSED")

-- 4e. Nested nursery
var nested_log = []
nursery {
    spawn {
        nested_log = nested_log ++ ["outer1"]
        nursery {
            spawn { nested_log = nested_log ++ ["inner1"] }
            spawn { nested_log = nested_log ++ ["inner2"] }
        }
    }
    spawn { nested_log = nested_log ++ ["outer2"] }
}
assert(len(nested_log) == 4, "4e: nested nursery should produce 4 entries")
assert(nested_log[0] == "outer1", "4e: first entry is outer1")
assert(nested_log[1] == "inner1", "4e: second entry is inner1")
assert(nested_log[2] == "inner2", "4e: third entry is inner2")
assert(nested_log[3] == "outer2", "4e: fourth entry is outer2")
println("4e. Nested nursery: PASSED")

-- 4f. Nursery with FIFO ordering
var order = []
nursery {
    spawn { order = order ++ [1] }
    spawn { order = order ++ [2] }
    spawn { order = order ++ [3] }
    spawn { order = order ++ [4] }
    spawn { order = order ++ [5] }
}
assert(order[0] == 1, "4f: FIFO order 1")
assert(order[1] == 2, "4f: FIFO order 2")
assert(order[2] == 3, "4f: FIFO order 3")
assert(order[3] == 4, "4f: FIFO order 4")
assert(order[4] == 5, "4f: FIFO order 5")
println("4f. Nursery FIFO ordering: PASSED")

-- ============================================================
-- 5. COMBINED CONCURRENCY PATTERNS
-- ============================================================

-- 5a. Actor + channel communication
let msg_ch = channel()
let counter_actor = spawn Counter
nursery {
    -- Producer: sends commands to channel
    spawn {
        msg_ch.send("increment")
        msg_ch.send("increment")
        msg_ch.send("increment")
        msg_ch.send("done")
    }
    -- Consumer: reads from channel and sends to actor
    spawn {
        var done = false
        while !done {
            let msg = msg_ch.recv()
            if msg == "done" {
                done = true
            } else {
                counter_actor ! msg
            }
        }
    }
}
assert(counter_actor.get() == 3, "5a: actor should have count 3 from channel msgs")
println("5a. Actor + channel: PASSED")

-- 5b. Fan-out pattern: multiple workers processing from same channel
let work_ch = channel()
let done_ch = channel()
nursery {
    -- Producer
    spawn {
        var i = 0
        while i < 6 {
            work_ch.send(i)
            i = i + 1
        }
    }
    -- Workers consume and produce results
    spawn {
        var j = 0
        while j < 3 {
            let item = work_ch.recv()
            done_ch.send(item * 10)
            j = j + 1
        }
    }
    spawn {
        var j = 0
        while j < 3 {
            let item = work_ch.recv()
            done_ch.send(item * 10)
            j = j + 1
        }
    }
}
-- Collect all 6 results
var sum_results = 0
var k = 0
while k < 6 {
    sum_results = sum_results + done_ch.recv()
    k = k + 1
}
-- 0*10 + 1*10 + 2*10 + 3*10 + 4*10 + 5*10 = 150
assert(sum_results == 150, "5b: fan-out sum should be 150")
println("5b. Fan-out pattern: PASSED")

-- 5c. Pipeline pattern
let stage1_ch = channel()
let stage2_ch = channel()
let output_ch = channel()

nursery {
    -- Stage 1: double the input
    spawn {
        stage1_ch.send(5)
        stage1_ch.send(10)
        stage1_ch.send(15)
    }
    -- Stage 2: add 1
    spawn {
        var i2 = 0
        while i2 < 3 {
            let x = stage1_ch.recv()
            stage2_ch.send(x * 2)
            i2 = i2 + 1
        }
    }
    -- Stage 3: collect
    spawn {
        var i3 = 0
        while i3 < 3 {
            let x = stage2_ch.recv()
            output_ch.send(x + 1)
            i3 = i3 + 1
        }
    }
}

let p1 = output_ch.recv()
let p2 = output_ch.recv()
let p3 = output_ch.recv()
assert(p1 == 11, "5c: pipeline 5*2+1 = 11")
assert(p2 == 21, "5c: pipeline 10*2+1 = 21")
assert(p3 == 31, "5c: pipeline 15*2+1 = 31")
println("5c. Pipeline pattern: PASSED")

-- ============================================================
-- 6. EDGE CASES
-- ============================================================

-- 6a. Empty nursery
nursery {}
println("6a. Empty nursery: PASSED")

-- 6b. Nursery with no spawns (just regular code)
var ec_val = 0
nursery {
    ec_val = 42
}
assert(ec_val == 42, "6b: nursery body runs without spawn")
println("6b. Nursery without spawn: PASSED")

-- 6c. Spawn returns null when inside nursery
nursery {
    let spawn_result = spawn { 42 }
    assert(spawn_result == null, "6c: spawn inside nursery returns null")
}
println("6c. Spawn returns null in nursery: PASSED")

-- 6d. Channel used as FIFO queue
let fifo = channel()
var idx = 0
while idx < 10 {
    fifo.send(idx)
    idx = idx + 1
}
var fifo_ok = true
idx = 0
while idx < 10 {
    let got = fifo.recv()
    if got != idx {
        fifo_ok = false
    }
    idx = idx + 1
}
assert(fifo_ok, "6d: channel FIFO order preserved for 10 items")
println("6d. Channel FIFO 10 items: PASSED")

-- ============================================================
println("")
println("All concurrency tests PASSED!")
