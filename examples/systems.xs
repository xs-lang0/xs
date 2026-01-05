// XS Language -- Systems Programming Demo
// Demonstrates: bit manipulation, low-level data structures,
//               checksums, buffer operations
// (No unsafe blocks, raw pointers, or assembly yet)

// ---------------------------------------------------------------
// Bit manipulation utilities
// ---------------------------------------------------------------

fn popcount(x) {
    var count = 0
    var v = x
    while v != 0 {
        count = count + 1
        v = v & (v - 1)
    }
    return count
}

fn set_bit(x, pos) {
    return x | (1 << pos)
}

fn clear_bit(x, pos) {
    // XOR trick: set then XOR if already set
    let with_set = x | (1 << pos)
    if x == with_set {
        // bit was set, XOR to clear
        return x ^ (1 << pos)
    }
    return x
}

fn toggle_bit(x, pos) {
    return x ^ (1 << pos)
}

fn test_bit(x, pos) {
    return (x >> pos) & 1 == 1
}

fn extract_bits(x, start, num_bits) {
    return (x >> start) & ((1 << num_bits) - 1)
}

fn bit_reverse_8(x) {
    var r = 0
    var v = x
    var i = 0
    while i < 8 {
        r = (r << 1) | (v & 1)
        v = v >> 1
        i = i + 1
    }
    return r
}

// ---------------------------------------------------------------
// Fixed-size stack (simulated with array)
// ---------------------------------------------------------------

fn stack_new() {
    return #{"data": [], "capacity": 256}
}

fn stack_push(s, val) {
    if len(s.data) >= s.capacity {
        return false
    }
    s.data.push(val)
    return true
}

fn stack_pop(s) {
    if len(s.data) == 0 {
        return null
    }
    return s.data.pop()
}

fn stack_peek(s) {
    if len(s.data) == 0 {
        return null
    }
    return s.data[len(s.data) - 1]
}

fn stack_len(s) {
    return len(s.data)
}

fn stack_is_empty(s) {
    return len(s.data) == 0
}

// ---------------------------------------------------------------
// Ring buffer
// ---------------------------------------------------------------

fn ringbuf_new(cap) {
    return #{"data": [], "read_pos": 0, "write_pos": 0, "capacity": cap, "count": 0}
}

fn ringbuf_push(rb, val) {
    if rb.count >= rb.capacity {
        return false
    }
    rb.data.push(val)
    rb.count = rb.count + 1
    return true
}

fn ringbuf_pop(rb) {
    if rb.count == 0 {
        return null
    }
    // Simple approach: remove from front
    let val = rb.data[0]
    // Shift remaining
    var new_data = []
    for i in 1..len(rb.data) {
        new_data.push(rb.data[i])
    }
    rb.data = new_data
    rb.count = rb.count - 1
    return val
}

// ---------------------------------------------------------------
// Checksums
// ---------------------------------------------------------------

fn checksum_8(data) {
    var sum = 0
    for b in data {
        sum = (sum + b) & 0xFF
    }
    return (256 - sum) & 0xFF
}

fn djb2_hash(s) {
    var hash = 5381
    var i = 0
    while i < len(s) {
        let c = s[i]
        hash = hash * 33 + i
        i = i + 1
    }
    return hash
}

// ---------------------------------------------------------------
// Main
// ---------------------------------------------------------------

fn main() {
    println("=== XS Systems Programming Demo ===")
    println("")

    // Bit manipulation
    println("--- Bit Operations ---")
    let x = 181  // binary: 10110101
    println("x = {x} (binary: 10110101)")
    println("popcount({x}): {popcount(x)}")
    println("set_bit({x}, 1): {set_bit(x, 1)}")
    println("clear_bit({x}, 2): {clear_bit(x, 2)}")
    println("toggle_bit({x}, 0): {toggle_bit(x, 0)}")
    println("test_bit({x}, 4): {test_bit(x, 4)}")
    println("extract_bits({x}, 2, 4): {extract_bits(x, 2, 4)}")
    println("bit_reverse_8(197): {bit_reverse_8(197)}")
    println("")

    // Bitwise operator demo
    println("--- Bitwise Operators ---")
    let a = 0xFF
    let b = 0x0F
    println("0xFF & 0x0F = {a & b}")
    println("0xFF | 0x0F = {a | b}")
    println("0xFF ^ 0x0F = {a ^ b}")
    println("0xFF << 4   = {a << 4}")
    println("0xFF >> 4   = {a >> 4}")
    println("")

    // Stack
    println("--- Fixed Stack ---")
    let s = stack_new()
    stack_push(s, 10)
    stack_push(s, 20)
    stack_push(s, 30)
    stack_push(s, 40)
    println("Stack len: {stack_len(s)}")
    println("Peek: {stack_peek(s)}")
    while !stack_is_empty(s) {
        let val = stack_pop(s)
        println("  Pop: {val}")
    }
    println("")

    // Ring buffer
    println("--- Ring Buffer ---")
    let rb = ringbuf_new(8)
    ringbuf_push(rb, 1)
    ringbuf_push(rb, 2)
    ringbuf_push(rb, 3)
    println("Pushed 1, 2, 3. Count: {rb.count}")
    let v1 = ringbuf_pop(rb)
    println("Pop: {v1}")
    ringbuf_push(rb, 4)
    ringbuf_push(rb, 5)
    println("After more ops. Count: {rb.count}")
    println("")

    // Checksums
    println("--- Checksums ---")
    let data = [1, 2, 3, 4]
    println("checksum_8([1,2,3,4]): {checksum_8(data)}")
    println("djb2_hash('hello'): {djb2_hash("hello")}")
    println("djb2_hash('world'): {djb2_hash("world")}")
    println("")

    // Process info
    println("--- System Info ---")
    import os
    import process
    println("cwd: {os.cwd()}")
    println("pid: {process.pid()}")
}
