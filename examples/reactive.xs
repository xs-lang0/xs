// XS Language -- Reactive Programming Patterns
// Demonstrates: closures for state, observer pattern, derived values
// (The reactive module is minimal; we build patterns with closures)

// ---------------------------------------------------------------
// Signal: a mutable value with subscribers
// ---------------------------------------------------------------

fn make_signal(initial) {
    let state = #{"value": initial, "subs": []}

    let get = fn() { return state.value }

    let set = fn(new_val) {
        state.value = new_val
        for sub in state.subs {
            sub(new_val)
        }
    }

    let subscribe = fn(callback) {
        state.subs.push(callback)
    }

    return #{"get": get, "set": set, "subscribe": subscribe}
}

// ---------------------------------------------------------------
// Derived: a computed value from other signals
// ---------------------------------------------------------------

fn make_derived(compute_fn) {
    return #{"get": compute_fn}
}

// ---------------------------------------------------------------
// Main
// ---------------------------------------------------------------

fn main() {
    println("=== XS Reactive Programming Demo ===")
    println("")

    // Create signals
    let count = make_signal(0)
    let name = make_signal("World")
    let temp_f = make_signal(72.0)

    // Derived values
    let doubled = make_derived(fn() { return count.get() * 2 })
    let greeting = make_derived(fn() { return "Hello, " ++ name.get() ++ "!" })
    let temp_c = make_derived(fn() { return (temp_f.get() - 32.0) * 5.0 / 9.0 })
    let is_hot = make_derived(fn() { return temp_f.get() > 90.0 })

    // Display initial values
    println("Initial state:")
    println("  count = {count.get()}")
    println("  doubled = {doubled.get()}")
    println("  greeting = {greeting.get()}")
    println("  temp_f = {temp_f.get()}")
    println("  temp_c = {temp_c.get()}")
    println("  is_hot = {is_hot.get()}")
    println("")

    // Update signals
    count.set(5)
    name.set("XS")
    temp_f.set(100.0)

    println("After updates:")
    println("  count = {count.get()}")
    println("  doubled = {doubled.get()}")
    println("  greeting = {greeting.get()}")
    println("  temp_f = {temp_f.get()}")
    println("  temp_c = {temp_c.get()}")
    println("  is_hot = {is_hot.get()}")
    println("")

    // Subscriptions
    println("--- Subscription Effects ---")
    count.subscribe(fn(new_val) {
        println("  [effect] count changed to {new_val}")
    })

    temp_f.subscribe(fn(new_val) {
        let c = (new_val - 32.0) * 5.0 / 9.0
        if new_val > 90.0 {
            println("  [effect] WARNING: {new_val}F ({c}C) is too hot!")
        } else {
            println("  [effect] Temperature: {new_val}F")
        }
    })

    println("Incrementing count 3 times:")
    count.set(count.get() + 1)
    count.set(count.get() + 1)
    count.set(count.get() + 1)

    println("")
    println("Changing temperature:")
    temp_f.set(75.0)
    temp_f.set(95.0)
    temp_f.set(68.0)

    println("")
    println("Final count: {count.get()}")
    println("Final temp: {temp_f.get()}F")
    println("")

    // ---- Simple store (Redux-like) ----
    println("--- Reactive Store ---")

    fn make_store(initial) {
        let state = #{"data": initial, "subs": []}

        let get_state = fn() { return state.data }

        let dispatch = fn(action, payload) {
            match action {
                "ADD_ITEM" => {
                    state.data.items.push(payload)
                    state.data.count = len(state.data.items)
                },
                "SET_FILTER" => {
                    state.data.filter = payload
                },
                _ => {}
            }
            for sub in state.subs {
                sub(state.data)
            }
        }

        let subscribe = fn(callback) {
            state.subs.push(callback)
        }

        return #{"dispatch": dispatch, "subscribe": subscribe, "get_state": get_state}
    }

    let store = make_store(#{"items": [], "filter": "all", "count": 0})

    store.subscribe(fn(st) {
        println("  [store] State changed, count: {st.count}")
    })

    store.dispatch("ADD_ITEM", "Buy groceries")
    store.dispatch("ADD_ITEM", "Clean house")
    store.dispatch("ADD_ITEM", "Exercise")
    store.dispatch("ADD_ITEM", "Read book")

    println("")
    let final_state = store.get_state()
    println("Final state:")
    println("  items: {final_state.items}")
    println("  count: {final_state.count}")
}
