-- structs, traits, impl, enums

-- basic struct
struct Point { x, y }
let p = Point { x: 10, y: 20 }
assert_eq(p.x, 10)
assert_eq(p.y, 20)

-- struct with methods via impl
struct Rect { w, h }
impl Rect {
    fn area(self) { return self.w * self.h }
    fn perimeter(self) { return 2 * (self.w + self.h) }
}
let r = Rect { w: 5, h: 3 }
assert_eq(r.area(), 15)
assert_eq(r.perimeter(), 16)

-- nested structs
struct Line { start, end }
let l = Line { start: Point { x: 0, y: 0 }, end: Point { x: 3, y: 4 } }
assert_eq(l.start.x, 0)
assert_eq(l.end.y, 4)

-- struct update (spread)
let p2 = Point { ...p, y: 30 }
assert_eq(p2.x, 10)
assert_eq(p2.y, 30)

-- traits
trait Printable {
    fn display(self) -> str
}

struct Dog { name, breed }
impl Printable for Dog {
    fn display(self) -> str {
        return "{self.name} ({self.breed})"
    }
}
let d = Dog { name: "Rex", breed: "Shepherd" }
assert_eq(d.display(), "Rex (Shepherd)")

-- multiple traits
trait Area {
    fn area(self) -> f64
}
struct Circle { radius }
impl Area for Circle {
    fn area(self) -> f64 { return 3.14159 * self.radius * self.radius }
}
let c = Circle { radius: 5 }
assert(c.area() > 78.0, "circle area")

-- enums
enum Color { Red, Green, Blue }
let color = Color::Red
assert_eq(str(color), "Color::Red")

-- enum with data
enum Shape {
    Circle(radius),
    Rect(w, h)
}
let s1 = Shape::Circle(5)
let s2 = Shape::Rect(3, 4)
assert(str(s1).contains("Circle"), "enum Circle")
assert(str(s2).contains("Rect"), "enum Rect")

-- enum matching
fn describe(shape) {
    return match shape {
        Shape::Circle(r) => "circle r={r}"
        Shape::Rect(w, h) => "rect {w}x{h}"
        _ => "unknown"
    }
}
assert_eq(describe(s1), "circle r=5")
assert_eq(describe(s2), "rect 3x4")

-- struct destructuring in let
let Point { x: px, y: py } = Point { x: 100, y: 200 }
assert_eq(px, 100)
assert_eq(py, 200)

println("test_structs: all passed")
