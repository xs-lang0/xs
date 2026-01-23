-- structs_and_traits.xs: structs, impl, traits, enums, pattern matching

println("--- Structs ---")

struct Vec2 { x, y }

impl Vec2 {
    fn length(self) {
        return sqrt(self.x * self.x + self.y * self.y)
    }

    fn add(self, other) {
        return Vec2 { x: self.x + other.x, y: self.y + other.y }
    }

    fn scale(self, factor) {
        return Vec2 { x: self.x * factor, y: self.y * factor }
    }

    fn to_str(self) {
        return "({self.x}, {self.y})"
    }
}

let a = Vec2 { x: 3, y: 4 }
let b = Vec2 { x: 1, y: 2 }
let c = a.add(b)

println("a = {a.to_str()}")
println("b = {b.to_str()}")
println("a + b = {c.to_str()}")
println("|a| = {a.length()}")
assert_eq(a.length(), 5.0)
assert_eq(c.x, 4)

-- struct spread (update syntax)
let moved = Vec2 { ...a, y: 0 }
println("moved = {moved.to_str()}")
assert_eq(moved.x, 3)
assert_eq(moved.y, 0)

println("\n--- Traits ---")

trait Describe {
    fn describe(self) -> str
}

struct Dog { name, breed }
struct Car { make, year }

impl Describe for Dog {
    fn describe(self) -> str {
        return "{self.name} the {self.breed}"
    }
}

impl Describe for Car {
    fn describe(self) -> str {
        return "{self.year} {self.make}"
    }
}

let rex = Dog { name: "Rex", breed: "Shepherd" }
let car = Car { make: "Volvo", year: 2024 }

println(rex.describe())
println(car.describe())
assert_eq(rex.describe(), "Rex the Shepherd")

println("\n--- Enums ---")

-- simple enum
enum Direction { North, South, East, West }

let dir = Direction::North
println("direction: {dir}")

-- enum with associated data
enum Shape {
    Circle(radius),
    Rect(w, h),
    Triangle(a, b, c)
}

fn area(shape) {
    return match shape {
        Shape::Circle(r) => 3.14159 * r * r
        Shape::Rect(w, h) => w * h
        Shape::Triangle(a, b, c) => {
            -- heron's formula
            let s = (a + b + c) / 2.0
            sqrt(s * (s - a) * (s - b) * (s - c))
        }
        _ => 0
    }
}

let shapes = [
    Shape::Circle(5),
    Shape::Rect(3, 4),
    Shape::Triangle(3, 4, 5)
]

for s in shapes {
    let a = area(s)
    println("{s} -> area = {a}")
}

assert_eq(area(Shape::Rect(3, 4)), 12)

println("\n--- Pattern Matching ---")

-- match with guards and destructuring
fn classify_point(p) {
    return match (p.x, p.y) {
        (0, 0) => "origin"
        (x, 0) => "x-axis at {x}"
        (0, y) => "y-axis at {y}"
        (x, y) if x == y => "diagonal at {x}"
        _ => "({p.x}, {p.y})"
    }
}

println(classify_point(Vec2 { x: 0, y: 0 }))
println(classify_point(Vec2 { x: 5, y: 0 }))
println(classify_point(Vec2 { x: 3, y: 3 }))
println(classify_point(Vec2 { x: 1, y: 2 }))

assert_eq(classify_point(Vec2 { x: 0, y: 0 }), "origin")
assert_eq(classify_point(Vec2 { x: 3, y: 3 }), "diagonal at 3")

-- matching on Result types
fn safe_divide(a, b) {
    if b == 0 { return Err("division by zero") }
    return Ok(a / b)
}

for pair in [(10, 2), (7, 0), (100, 4)] {
    let result = safe_divide(pair.0, pair.1)
    let msg = match result {
        Ok(v) => "  {pair.0}/{pair.1} = {v}"
        Err(e) => "  {pair.0}/{pair.1} failed: {e}"
        _ => "  ???"
    }
    println(msg)
}

println("\nAll good!")
