// XS Language -- Data Structures Example
// Pure XS implementations of fundamental data structures

// ---------------------------------------------------------
// Linked List
// ---------------------------------------------------------

struct Node { value, next }

fn list_prepend(head, val) {
    return Node { value: val, next: head }
}

fn list_to_array(head) {
    var result = []
    var cur = head
    while cur != null {
        result.push(cur.value)
        cur = cur.next
    }
    return result
}

fn list_len(head) {
    var count = 0
    var cur = head
    while cur != null {
        count = count + 1
        cur = cur.next
    }
    return count
}

fn list_map(head, f) {
    if head == null { return null }
    return Node { value: f(head.value), next: list_map(head.next, f) }
}

fn list_filter(head, pred) {
    if head == null { return null }
    if pred(head.value) {
        return Node { value: head.value, next: list_filter(head.next, pred) }
    } else {
        return list_filter(head.next, pred)
    }
}

fn list_fold(head, init, f) {
    var acc = init
    var cur = head
    while cur != null {
        acc = f(acc, cur.value)
        cur = cur.next
    }
    return acc
}

// ---------------------------------------------------------
// Stack (using array)
// ---------------------------------------------------------

fn stack_new() { return [] }

fn stack_push(s, val) {
    s.push(val)
    return s
}

fn stack_pop(s) {
    if len(s) == 0 { return null }
    let val = s[len(s) - 1]
    // Remove last element by rebuilding
    var result = []
    var i = 0
    while i < len(s) - 1 {
        result.push(s[i])
        i = i + 1
    }
    return result
}

fn stack_peek(s) {
    if len(s) == 0 { return null }
    return s[len(s) - 1]
}

// ---------------------------------------------------------
// Queue (using array)
// ---------------------------------------------------------

fn queue_new() { return [] }

fn queue_enqueue(q, val) {
    q.push(val)
    return q
}

fn queue_dequeue(q) {
    if len(q) == 0 { return null }
    // Return remaining after first element
    var result = []
    var i = 1
    while i < len(q) {
        result.push(q[i])
        i = i + 1
    }
    return result
}

fn queue_front(q) {
    if len(q) == 0 { return null }
    return q[0]
}

// ---------------------------------------------------------
// Binary Search Tree
// ---------------------------------------------------------

struct BSTNode { key, value, left, right }

fn bst_insert(node, key, val) {
    if node == null {
        return BSTNode { key: key, value: val, left: null, right: null }
    }
    if key < node.key {
        return BSTNode { key: node.key, value: node.value,
                  left: bst_insert(node.left, key, val),
                  right: node.right }
    } elif key > node.key {
        return BSTNode { key: node.key, value: node.value,
                  left: node.left,
                  right: bst_insert(node.right, key, val) }
    } else {
        return BSTNode { key: key, value: val,
                  left: node.left, right: node.right }
    }
}

fn bst_find(node, key) {
    if node == null { return null }
    if key == node.key { return node.value }
    if key < node.key { return bst_find(node.left, key) }
    return bst_find(node.right, key)
}

fn bst_inorder(node) {
    if node == null { return [] }
    return bst_inorder(node.left) ++ [node.key] ++ bst_inorder(node.right)
}

fn bst_height(node) {
    if node == null { return 0 }
    let lh = bst_height(node.left)
    let rh = bst_height(node.right)
    if lh > rh {
        return 1 + lh
    } else {
        return 1 + rh
    }
}

// ---------------------------------------------------------
// Main
// ---------------------------------------------------------

fn main() {
    println("=== XS Data Structures Demo ===")
    println("")

    // Linked List
    println("--- Linked List ---")
    var list = null
    let items = [5, 3, 8, 1, 9, 2, 7]
    for i in items {
        list = list_prepend(list, i)
    }
    println("List: {list_to_array(list)}")
    println("Length: {list_len(list)}")

    let doubled = list_map(list, fn(x) { return x * 2 })
    println("Doubled: {list_to_array(doubled)}")

    let evens = list_filter(list, fn(x) { return x % 2 == 0 })
    println("Evens: {list_to_array(evens)}")

    let sum = list_fold(list, 0, fn(acc, x) { return acc + x })
    println("Sum: {sum}")
    println("")

    // Stack
    println("--- Stack ---")
    var s = stack_new()
    s = stack_push(s, 10)
    s = stack_push(s, 20)
    s = stack_push(s, 30)
    println("Stack: {s}")
    println("Peek: {stack_peek(s)}")
    s = stack_pop(s)
    println("After pop: {s}")
    println("Peek: {stack_peek(s)}")
    println("")

    // Queue
    println("--- Queue ---")
    var q = queue_new()
    q = queue_enqueue(q, "first")
    q = queue_enqueue(q, "second")
    q = queue_enqueue(q, "third")
    println("Queue: {q}")
    println("Front: {queue_front(q)}")
    q = queue_dequeue(q)
    println("After dequeue: {q}")
    println("Front: {queue_front(q)}")
    println("")

    // BST
    println("--- Binary Search Tree ---")
    var bst = null
    let keys = [5, 3, 8, 1, 4, 7, 9, 2, 6]
    for k in keys {
        bst = bst_insert(bst, k, "val_" ++ str(k))
    }
    println("In-order traversal: {bst_inorder(bst)}")
    println("Height: {bst_height(bst)}")
    println("Find 4: {bst_find(bst, 4)}")
    println("Find 99: {bst_find(bst, 99)}")
}
