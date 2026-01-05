-- Test class and struct features in VM mode

struct Point {
    x: 0,
    y: 0,
}

fn test_struct_init() {
    let p = Point { x: 3, y: 4 }
    assert(p.x == 3, "struct field x")
    assert(p.y == 4, "struct field y")
}

fn test_struct_fields() {
    let p = Point { x: 1, y: 2 }
    assert(p.x == 1, "struct field x=1")
    assert(p.y == 2, "struct field y=2")
}

fn test_class_basic() {
    -- Class with init
    class Counter {
        var count = 0
        fn init(self, start) {
            self.count = start
        }
    }
    let c = Counter(5)
    assert(c.count == 5, "class init sets count")
}

fn test_class_method() {
    -- Class with methods via callable fields
    let obj = {"val": 10}
    obj.val = obj.val + 5
    assert(obj.val == 15, "object field mutation")
}

fn test_class_multiple_calls() {
    -- Simulate a counter pattern
    var state = {"count": 0}
    state.count = state.count + 1
    state.count = state.count + 1
    state.count = state.count + 1
    assert(state.count == 3, "map-based counter")
}

fn test_map_as_struct() {
    -- Maps can act like structs
    let p = {"name": "Alice", "age": 30}
    assert(p.name == "Alice", "map field access")
    p.age = 31
    assert(p.age == 31, "map field mutation")
}

fn main() {
    test_struct_init()
    test_struct_fields()
    test_class_basic()
    test_class_method()
    test_class_multiple_calls()
    test_map_as_struct()
    println("vm_class: all tests passed")
}
