// XS Language -- Message Passing Simulation
// Demonstrates: functions, maps, try/catch, pipelines
// (Actors are not yet fully implemented; this uses functions + maps instead)

// ---------------------------------------------------------------
// Counter -- map with count field, handle function
// ---------------------------------------------------------------

fn counter_new() {
    return #{"count": 0}
}

fn counter_handle(c, msg) {
    let t = msg.type
    match t {
        "increment" => {
            let amount = msg.amount
            if amount == null {
                c.count = c.count + 1
            } else {
                c.count = c.count + amount
            }
        },
        "decrement" => {
            let amount = msg.amount
            if amount == null {
                c.count = c.count - 1
            } else {
                c.count = c.count - amount
            }
        },
        "reset" => {
            c.count = 0
        },
        "get" => {
            println("  Counter: {c.count}")
        },
        _ => {
            println("  Unknown message: {t}")
        }
    }
}

// ---------------------------------------------------------------
// Bank Account -- map with balance, deposit/withdraw functions
// ---------------------------------------------------------------

fn bank_new(owner, initial) {
    println("  Account opened for {owner} with ${initial}")
    return #{"owner": owner, "balance": initial, "history": []}
}

fn bank_deposit(acct, amount) {
    acct.balance = acct.balance + amount
    acct.history.push(#{"op": "deposit", "amount": amount})
    println("  Deposited ${amount} -> Balance: ${acct.balance}")
}

fn bank_withdraw(acct, amount) {
    if amount > acct.balance {
        println("  Insufficient funds! Have ${acct.balance}, need ${amount}")
    } else {
        acct.balance = acct.balance - amount
        acct.history.push(#{"op": "withdraw", "amount": amount})
        println("  Withdrew ${amount} -> Balance: ${acct.balance}")
    }
}

fn bank_transfer(accounts, amount) {
    // accounts is [from, to] -- workaround for 3-arg field mutation
    let from = accounts[0]
    let to = accounts[1]
    if amount <= from.balance {
        bank_withdraw(from, amount)
        bank_deposit(to, amount)
        println("  Transferred ${amount} from {from.owner} to {to.owner}")
    } else {
        println("  Transfer failed: insufficient funds")
    }
}

fn bank_statement(acct) {
    println("  === Account Statement: {acct.owner} ===")
    println("  Current Balance: ${acct.balance}")
    println("  Transactions: {len(acct.history)}")
    for tx in acct.history {
        println("    {tx.op} ${tx.amount}")
    }
}

// ---------------------------------------------------------------
// Supervision -- wrap calls in try/catch
// ---------------------------------------------------------------

fn supervised_call(name, max_restarts, callback) {
    var count = 0
    var success = false
    while count < max_restarts {
        try {
            callback()
            success = true
            break
        } catch e {
            count = count + 1
            println("  Supervisor: '{name}' failed (attempt {count}/{max_restarts}): {e}")
        }
    }
    if !success {
        println("  Supervisor: '{name}' exceeded max restarts, giving up")
    }
}

// ---------------------------------------------------------------
// Pipeline -- chain of functions processing data
// ---------------------------------------------------------------

fn pipeline_run(stages, data) {
    var result = data
    for stage in stages {
        let name = stage.name
        let transform = stage.transform
        let prev = result
        result = transform(result)
        println("  [{name}] {prev} -> {result}")
    }
    return result
}

// ---------------------------------------------------------------
// Main
// ---------------------------------------------------------------

fn main() {
    println("=== XS Message Passing Demo ===")
    println("")

    // -- Counter --
    println("--- Counter ---")
    let counter = counter_new()

    counter_handle(counter, #{"type": "increment"})
    counter_handle(counter, #{"type": "increment"})
    counter_handle(counter, #{"type": "increment", "amount": 5})
    counter_handle(counter, #{"type": "get"})
    counter_handle(counter, #{"type": "decrement", "amount": 2})
    counter_handle(counter, #{"type": "get"})
    counter_handle(counter, #{"type": "reset"})
    counter_handle(counter, #{"type": "get"})
    println("")

    // -- Bank Account --
    println("--- Bank Accounts ---")
    let alice = bank_new("Alice", 1000.0)
    let bob = bank_new("Bob", 500.0)

    bank_deposit(alice, 250.0)
    bank_deposit(bob, 100.0)
    bank_withdraw(alice, 300.0)
    bank_transfer([alice, bob], 200.0)
    println("")
    bank_statement(alice)
    println("")
    bank_statement(bob)
    println("")

    // -- Supervision with try/catch --
    println("--- Supervision ---")

    // Simulate a task that fails twice then succeeds
    var fail_count = 0
    supervised_call("flaky-task", 3, fn() {
        fail_count = fail_count + 1
        if fail_count <= 2 {
            throw "random failure #" ++ str(fail_count)
        }
        println("  Task succeeded on attempt {fail_count}")
    })

    // Simulate a task that always fails (exceeds max)
    supervised_call("bad-task", 3, fn() {
        throw "always fails"
    })
    println("")

    // -- Pipeline --
    println("--- Processing Pipeline ---")
    let stages = [
        #{"name": "parse",     "transform": fn(x) { return x * 2 }},
        #{"name": "validate",  "transform": fn(x) { return x + 10 }},
        #{"name": "transform", "transform": fn(x) { return x * x }},
        #{"name": "format",    "transform": fn(x) { return "result=" ++ str(x) }}
    ]

    pipeline_run(stages, 5)
    println("")
    pipeline_run(stages, 3)
    println("")

    // -- Event dispatch with map of callbacks --
    println("--- Event Dispatch ---")
    var handlers = #{"_": null}
    var log = []

    handlers.user_login = fn(data) {
        let entry = "[login] " ++ data
        log.push(entry)
        println("  {entry}")
    }
    handlers.order_placed = fn(data) {
        let entry = "[order] " ++ data
        log.push(entry)
        println("  {entry}")
    }

    fn dispatch(h, event) {
        let handler = h[event]
        return handler
    }

    let h1 = dispatch(handlers, "user_login")
    if h1 != null { h1("alice@example.com") }

    let h2 = dispatch(handlers, "order_placed")
    if h2 != null { h2("order-12345") }

    let h3 = dispatch(handlers, "user_logout")
    if h3 != null {
        h3("alice@example.com")
    } else {
        println("  No handler for: user_logout")
    }
    println("")

    println("Event log ({len(log)} entries):")
    for entry in log {
        println("  {entry}")
    }
}
