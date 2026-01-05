-- test_collections.xs — Comprehensive tests for the collections module
-- Tests: Deque, Set, OrderedMap, Stack, PriorityQueue, Counter

import collections

-- ============================================================
-- 1. Stack
-- ============================================================
let stack = collections.Stack()
assert(stack.is_empty() == true, "Stack: new stack is empty")
assert(stack.len() == 0, "Stack: new stack len is 0")

stack.push(10)
stack.push(20)
stack.push(30)

assert(stack.len() == 3, "Stack: len after 3 pushes")
assert(stack.is_empty() == false, "Stack: not empty after pushes")
assert(stack.peek() == 30, "Stack: peek returns top (30)")

let popped = stack.pop()
assert(popped == 30, "Stack: pop returns 30")
assert(stack.len() == 2, "Stack: len after pop is 2")
assert(stack.peek() == 20, "Stack: peek returns 20 after pop")

-- to_array
let sa = stack.to_array()
assert(sa.len() == 2, "Stack: to_array len is 2")
assert(sa[0] == 10, "Stack: to_array[0] is 10")
assert(sa[1] == 20, "Stack: to_array[1] is 20")

-- clear
stack.clear()
assert(stack.is_empty() == true, "Stack: empty after clear")
assert(stack.len() == 0, "Stack: len 0 after clear")

-- pop on empty returns null
let empty_pop = stack.pop()
assert(empty_pop == null, "Stack: pop on empty returns null")

-- peek on empty returns null
let empty_peek = stack.peek()
assert(empty_peek == null, "Stack: peek on empty returns null")

println("Stack: ALL PASS")

-- ============================================================
-- 2. Deque
-- ============================================================
let dq = collections.Deque()
assert(dq.is_empty() == true, "Deque: new deque is empty")
assert(dq.len() == 0, "Deque: new deque len is 0")

dq.push_back(2)
dq.push_back(3)
dq.push_front(1)
dq.push_front(0)

assert(dq.len() == 4, "Deque: len after 4 inserts")
assert(dq.is_empty() == false, "Deque: not empty")

-- pop_front removes from front
let df = dq.pop_front()
assert(df == 0, "Deque: pop_front returns 0")
assert(dq.len() == 3, "Deque: len after pop_front")

-- pop_back removes from back
let db = dq.pop_back()
assert(db == 3, "Deque: pop_back returns 3")
assert(dq.len() == 2, "Deque: len after pop_back")

-- to_array preserves order
let da = dq.to_array()
assert(da.len() == 2, "Deque: to_array len")
assert(da[0] == 1, "Deque: to_array[0] is 1")
assert(da[1] == 2, "Deque: to_array[1] is 2")

-- pop on empty
let dq2 = collections.Deque()
assert(dq2.pop_front() == null, "Deque: pop_front on empty returns null")
assert(dq2.pop_back() == null, "Deque: pop_back on empty returns null")

println("Deque: ALL PASS")

-- ============================================================
-- 3. Set
-- ============================================================
let st = collections.Set()
assert(st.is_empty() == true, "Set: new set is empty")
assert(st.len() == 0, "Set: new set len is 0")

st.add("apple")
st.add("banana")
st.add("cherry")
st.add("banana")

assert(st.len() == 3, "Set: deduplicates (len 3 not 4)")
assert(st.contains("apple") == true, "Set: contains apple")
assert(st.contains("banana") == true, "Set: contains banana")
assert(st.contains("grape") == false, "Set: does not contain grape")
assert(st.is_empty() == false, "Set: not empty")

st.remove("banana")
assert(st.len() == 2, "Set: len after remove")
assert(st.contains("banana") == false, "Set: banana removed")

-- to_array
let star = st.to_array()
assert(star.len() == 2, "Set: to_array len 2")

-- Set from array constructor
let s_from_arr = collections.Set([10, 20, 30, 20])
assert(s_from_arr.len() == 3, "Set: constructor deduplicates")
assert(s_from_arr.contains(10) == true, "Set: constructor contains 10")
assert(s_from_arr.contains(20) == true, "Set: constructor contains 20")
assert(s_from_arr.contains(30) == true, "Set: constructor contains 30")

-- union
let sa1 = collections.Set(["a", "b", "c"])
let sa2 = collections.Set(["b", "c", "d"])
let su = sa1.union(sa2)
assert(su.len() == 4, "Set: union len 4")
assert(su.contains("a") == true, "Set: union has a")
assert(su.contains("b") == true, "Set: union has b")
assert(su.contains("c") == true, "Set: union has c")
assert(su.contains("d") == true, "Set: union has d")

-- intersection
let si = sa1.intersection(sa2)
assert(si.len() == 2, "Set: intersection len 2")
assert(si.contains("b") == true, "Set: intersection has b")
assert(si.contains("c") == true, "Set: intersection has c")
assert(si.contains("a") == false, "Set: intersection not a")
assert(si.contains("d") == false, "Set: intersection not d")

-- difference
let sd = sa1.difference(sa2)
assert(sd.len() == 1, "Set: difference len 1")
assert(sd.contains("a") == true, "Set: difference has a")
assert(sd.contains("b") == false, "Set: difference not b")

println("Set: ALL PASS")

-- ============================================================
-- 4. OrderedMap
-- ============================================================
let om = collections.OrderedMap()
assert(om.len() == 0, "OrderedMap: new map len 0")

om.set("gamma", 3)
om.set("alpha", 1)
om.set("beta", 2)

assert(om.len() == 3, "OrderedMap: len 3")
assert(om.get("alpha") == 1, "OrderedMap: get alpha")
assert(om.get("beta") == 2, "OrderedMap: get beta")
assert(om.get("gamma") == 3, "OrderedMap: get gamma")
assert(om.get("missing") == null, "OrderedMap: get missing returns null")
assert(om.get("missing", 99) == 99, "OrderedMap: get with default")
assert(om.has("alpha") == true, "OrderedMap: has alpha")
assert(om.has("delta") == false, "OrderedMap: has delta false")

-- keys preserve insertion order
let ok = om.keys()
assert(ok[0] == "gamma", "OrderedMap: keys[0] is gamma (first inserted)")
assert(ok[1] == "alpha", "OrderedMap: keys[1] is alpha")
assert(ok[2] == "beta", "OrderedMap: keys[2] is beta")

-- values preserve insertion order
let ov = om.values()
assert(ov[0] == 3, "OrderedMap: values[0] is 3")
assert(ov[1] == 1, "OrderedMap: values[1] is 1")
assert(ov[2] == 2, "OrderedMap: values[2] is 2")

-- entries
let oe = om.entries()
assert(oe.len() == 3, "OrderedMap: entries len 3")

-- update existing key (should not add duplicate to keys)
om.set("alpha", 100)
assert(om.len() == 3, "OrderedMap: len still 3 after update")
assert(om.get("alpha") == 100, "OrderedMap: updated value")

-- delete
om.delete("alpha")
assert(om.len() == 2, "OrderedMap: len after delete")
assert(om.has("alpha") == false, "OrderedMap: alpha deleted")
let ok2 = om.keys()
assert(ok2[0] == "gamma", "OrderedMap: keys after delete[0]")
assert(ok2[1] == "beta", "OrderedMap: keys after delete[1]")

println("OrderedMap: ALL PASS")

-- ============================================================
-- 5. PriorityQueue
-- ============================================================
let pq = collections.PriorityQueue()
assert(pq.is_empty() == true, "PQ: new pq is empty")
assert(pq.len() == 0, "PQ: new pq len 0")

pq.push("low", 1)
pq.push("high", 10)
pq.push("medium", 5)
pq.push("critical", 100)

assert(pq.len() == 4, "PQ: len 4")
assert(pq.is_empty() == false, "PQ: not empty")
assert(pq.peek() == "critical", "PQ: peek returns highest priority")

let p1 = pq.pop()
assert(p1 == "critical", "PQ: pop 1 is critical")
let p2 = pq.pop()
assert(p2 == "high", "PQ: pop 2 is high")
let p3 = pq.pop()
assert(p3 == "medium", "PQ: pop 3 is medium")
let p4 = pq.pop()
assert(p4 == "low", "PQ: pop 4 is low")
assert(pq.is_empty() == true, "PQ: empty after all pops")

-- pop/peek on empty
assert(pq.pop() == null, "PQ: pop on empty returns null")
assert(pq.peek() == null, "PQ: peek on empty returns null")

-- same priority items
let pq2 = collections.PriorityQueue()
pq2.push("a", 5)
pq2.push("b", 5)
pq2.push("c", 5)
assert(pq2.len() == 3, "PQ: same priority len")

println("PriorityQueue: ALL PASS")

-- ============================================================
-- 6. Counter
-- ============================================================
-- Counter from array
let cnt = collections.Counter(["x", "y", "x", "z", "x", "y"])
assert(cnt.get("x") == 3, "Counter: x count is 3")
assert(cnt.get("y") == 2, "Counter: y count is 2")
assert(cnt.get("z") == 1, "Counter: z count is 1")
assert(cnt.get("missing") == 0, "Counter: missing returns 0")

-- most_common
let mc = cnt.most_common(2)
assert(mc.len() == 2, "Counter: most_common(2) len 2")

-- most_common without arg
let mc_all = cnt.most_common()
assert(mc_all.len() == 3, "Counter: most_common() returns all")

-- add
let cnt2 = collections.Counter([])
cnt2.add("a")
cnt2.add("a")
cnt2.add("a")
cnt2.add("b", 5)
cnt2.add("c", 3)
assert(cnt2.get("a") == 3, "Counter: add a 3 times")
assert(cnt2.get("b") == 5, "Counter: add b with count 5")
assert(cnt2.get("c") == 3, "Counter: add c with count 3")

-- total
assert(cnt2.total() == 11, "Counter: total is 11")

-- to_map
let cmap = cnt2.to_map()
assert(cmap.has("a") == true, "Counter: to_map has a")
assert(cmap.has("b") == true, "Counter: to_map has b")

-- keys
let ckeys = cnt2.keys()
assert(ckeys.len() == 3, "Counter: keys len 3")

-- Counter with empty array
let cnt3 = collections.Counter([])
assert(cnt3.get("anything") == 0, "Counter: empty counter get is 0")

println("Counter: ALL PASS")

-- ============================================================
-- Summary
-- ============================================================
println("")
println("=== All collections tests passed! ===")
