-- tests/test_structs_traits.xs
-- Comprehensive tests for struct and trait support

-- ============================================================
-- 1. Basic struct declaration and instantiation
-- ============================================================

struct Point {
    x: f64
    y: f64
}

let p = Point { x: 3.0, y: 4.0 }
assert(p.x == 3.0, "struct field x")
assert(p.y == 4.0, "struct field y")
println("PASS: basic struct creation")

-- ============================================================
-- 2. Struct methods via impl
-- ============================================================

impl Point {
    fn distance_sq(self) {
        self.x * self.x + self.y * self.y
    }

    fn translate(self, dx: f64, dy: f64) {
        Point { x: self.x + dx, y: self.y + dy }
    }

    fn origin() {
        Point { x: 0.0, y: 0.0 }
    }
}

let d = p.distance_sq()
assert(d == 25.0, "struct method distance_sq")
println("PASS: struct methods via impl")

-- Test method with args
let p2 = p.translate(1.0, 2.0)
assert(p2.x == 4.0, "translate x")
assert(p2.y == 6.0, "translate y")
println("PASS: struct method with args")

-- ============================================================
-- 3. Impl after instance creation (methods found via class fallback)
-- ============================================================

struct Color {
    r: i64
    g: i64
    b: i64
}

let red = Color { r: 255, g: 0, b: 0 }

impl Color {
    fn brightness(self) {
        (self.r + self.g + self.b) // 3
    }
}

-- Method works even though impl was defined after the instance was created
let bright = red.brightness()
assert(bright == 85, "impl after instance creation")
println("PASS: impl after instance creation")

-- ============================================================
-- 4. Trait definitions and implementations
-- ============================================================

trait Printable {
    fn to_string(self) -> str
}

trait HasArea {
    fn area(self) -> f64
}

struct Circle {
    radius: f64
}

impl Printable for Circle {
    fn to_string(self) {
        "Circle(r=" ++ str(self.radius) ++ ")"
    }
}

impl HasArea for Circle {
    fn area(self) {
        3.14159 * self.radius * self.radius
    }
}

let c = Circle { radius: 5.0 }
assert(c.to_string() == "Circle(r=5)", "trait impl to_string")
assert(c.area() > 78.0, "trait impl area")
assert(c.area() < 79.0, "trait impl area upper bound")
println("PASS: trait definitions and implementations")

-- ============================================================
-- 5. Trait implementations add trait name to is_a
-- ============================================================

assert(c.is_a("Circle"), "is_a own type")
assert(c.is_a("Printable"), "is_a trait Printable")
assert(c.is_a("HasArea"), "is_a trait HasArea")
assert(!c.is_a("Point"), "not is_a Point")
println("PASS: is_a checks trait conformance")

-- ============================================================
-- 6. Default method implementations in traits
-- ============================================================

trait Greetable {
    fn name(self) -> str
    fn greet(self) {
        -- default implementation
        "Hello, " ++ self.name() ++ "!"
    }
}

struct Person {
    first: str
    last: str
}

impl Greetable for Person {
    fn name(self) {
        self.first ++ " " ++ self.last
    }
    -- greet() uses the default implementation from the trait
}

let person = Person { first: "Alice", last: "Smith" }
assert(person.name() == "Alice Smith", "trait method name")
assert(person.greet() == "Hello, Alice Smith!", "default trait method greet")
println("PASS: default method implementations in traits")

-- ============================================================
-- 7. Overriding default methods
-- ============================================================

struct Robot {
    id: str
}

impl Greetable for Robot {
    fn name(self) {
        "Robot-" ++ self.id
    }
    fn greet(self) {
        -- override default
        "Beep boop, I am " ++ self.name()
    }
}

let bot = Robot { id: "42" }
assert(bot.greet() == "Beep boop, I am Robot-42", "overridden default method")
println("PASS: overriding default trait methods")

-- ============================================================
-- 8. Derived traits: #[derive(Debug, Clone)]
-- ============================================================

#[derive(Debug, Clone)]
struct Vec2 {
    x: f64
    y: f64
}

let v1 = Vec2 { x: 1.0, y: 2.0 }
let s = v1.to_string()
assert(s == "Vec2 \{ x: 1, y: 2 \}", "Debug derive to_string")

let v2 = v1.clone()
v2.x = 99.0
assert(v1.x == 1.0, "clone does not mutate original")
assert(v2.x == 99.0, "clone has independent values")
assert(v2.y == 2.0, "clone copies other fields")
println("PASS: #[derive(Debug, Clone)]")

-- ============================================================
-- 9. Derived traits: derives keyword
-- ============================================================

struct Vec3 {
    x: f64
    y: f64
    z: f64
} derives Debug, Clone, PartialEq

let a = Vec3 { x: 1.0, y: 2.0, z: 3.0 }
let b = Vec3 { x: 1.0, y: 2.0, z: 3.0 }
let d2 = Vec3 { x: 4.0, y: 5.0, z: 6.0 }

assert(a == b, "PartialEq derive: equal structs")
assert(!(a == d2), "PartialEq derive: unequal structs")
assert(a.eq(b), "eq method on derived PartialEq")
assert(!a.eq(d2), "eq method: unequal")
println("PASS: derives keyword with PartialEq")

-- ============================================================
-- 10. Struct update syntax: Point { x: 10, ..p1 }
-- ============================================================

let p3 = Point { x: 10.0, y: 20.0 }
let p4 = Point { x: 99.0, ..p3 }

assert(p4.x == 99.0, "struct update overrides x")
assert(p4.y == 20.0, "struct update keeps y from base")
println("PASS: struct update syntax")

-- ============================================================
-- 11. Struct printing (Debug repr)
-- ============================================================

let printed = str(p3)
-- p3 should print as "Point { x: 10, y: 20 }"
assert(printed == "Point \{ x: 10, y: 20 \}", "struct instance repr")
println("PASS: struct printing")

-- ============================================================
-- 12. Associated types in traits (parsing)
-- ============================================================

trait Container {
    type Item
    fn first(self) -> any
}

-- Associated types are parsed; we just verify it does not crash
println("PASS: associated types in traits (parsed)")

-- ============================================================
-- 13. Trait with super trait
-- ============================================================

trait Shape {
    fn describe(self) -> str
}

-- trait FancyShape: Shape is parsed (super trait)
trait FancyShape: Shape {
    fn color(self) -> str
}

println("PASS: super trait parsing")

-- ============================================================
-- 14. Multiple impls on the same struct
-- ============================================================

struct Rect {
    w: f64
    h: f64
}

impl Rect {
    fn area(self) {
        self.w * self.h
    }
}

impl Rect {
    fn perimeter(self) {
        2.0 * (self.w + self.h)
    }
}

let r = Rect { w: 3.0, h: 4.0 }
assert(r.area() == 12.0, "first impl method")
assert(r.perimeter() == 14.0, "second impl method")
println("PASS: multiple impls on same struct")

-- ============================================================
-- 15. Struct field mutation
-- ============================================================

let mut_p = Point { x: 1.0, y: 2.0 }
mut_p.x = 10.0
mut_p.y = 20.0
assert(mut_p.x == 10.0, "field mutation x")
assert(mut_p.y == 20.0, "field mutation y")
println("PASS: struct field mutation")

-- ============================================================
-- 16. Nested structs
-- ============================================================

struct Line {
    start: any
    end_pt: any
}

let line = Line {
    start: Point { x: 0.0, y: 0.0 },
    end_pt: Point { x: 3.0, y: 4.0 }
}
assert(line.start.x == 0.0, "nested struct start.x")
assert(line.end_pt.y == 4.0, "nested struct end_pt.y")
println("PASS: nested structs")

-- ============================================================
-- 17. Struct with default field values
-- ============================================================

struct Config {
    width: i64 = 800
    height: i64 = 600
    title: str = "Default"
}

let cfg = Config { title: "Custom" }
assert(cfg.width == 800, "default field width")
assert(cfg.height == 600, "default field height")
assert(cfg.title == "Custom", "overridden field title")
println("PASS: struct with default field values")

-- ============================================================
-- All tests passed
-- ============================================================

println!("All struct and trait tests passed!")
