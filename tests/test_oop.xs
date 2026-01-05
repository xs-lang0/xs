-- Comprehensive OOP test suite for XS language
-- Tests: classes, inheritance, super, static methods, operator overloading,
--        dunder methods, is_a, constructors, property access, impl blocks

-- ============================================================
-- 1. Basic class with fields, constructor, and methods
-- ============================================================
class Point {
    x = 0
    y = 0

    fn init(self, x, y) {
        self.x = x
        self.y = y
    }

    fn distance_from_origin(self) {
        return (self.x * self.x + self.y * self.y) ** 0.5
    }

    fn translate(self, dx, dy) {
        return Point(self.x + dx, self.y + dy)
    }
}

let p1 = Point(3, 4)
assert(p1.x == 3, "Point.x should be 3")
assert(p1.y == 4, "Point.y should be 4")
assert(p1.distance_from_origin() == 5.0, "distance from origin should be 5.0")

let p2 = p1.translate(1, -1)
assert(p2.x == 4, "translated x should be 4")
assert(p2.y == 3, "translated y should be 3")
println("PASS: Basic class with fields, constructor, methods")

-- ============================================================
-- 2. Class with field defaults
-- ============================================================
class Config {
    host = "localhost"
    port = 8080
    debug = false

    fn init(self) {}

    fn to_string(self) {
        return self.host + ":" + str(self.port)
    }
}

let cfg = Config()
assert(cfg.host == "localhost", "default host")
assert(cfg.port == 8080, "default port")
assert(cfg.debug == false, "default debug")
println("PASS: Class with field defaults")

-- ============================================================
-- 3. Class inheritance and method override
-- ============================================================
class Shape {
    color = "white"

    fn init(self, color) {
        self.color = color
    }

    fn area(self) {
        return 0
    }

    fn describe(self) {
        return "Shape(" + self.color + ")"
    }
}

class Circle : Shape {
    radius = 1.0

    fn init(self, color, radius) {
        self.color = color
        self.radius = radius
    }

    fn area(self) {
        return 3.14159 * self.radius * self.radius
    }

    fn describe(self) {
        return "Circle(r=" + str(self.radius) + ", " + self.color + ")"
    }
}

class Square : Shape {
    side = 1.0

    fn init(self, color, side) {
        self.color = color
        self.side = side
    }

    fn area(self) {
        return self.side * self.side
    }
}

let s = Shape("red")
assert(s.area() == 0, "Shape base area is 0")
assert(s.describe() == "Shape(red)", "Shape describe")

let c = Circle("blue", 5.0)
assert(c.color == "blue", "Circle inherits color field")
let area = c.area()
assert(area > 78.0 and area < 79.0, "Circle area ~ 78.5")
-- Note: str(5.0) produces "5" in XS (integer-like floats drop the .0)
let cdesc = c.describe()
assert(cdesc == "Circle(r=5.0, blue)" or cdesc == "Circle(r=5, blue)", "Circle overrides describe")

let sq = Square("green", 4.0)
assert(sq.area() == 16.0, "Square area")
assert(sq.color == "green", "Square inherits color")
-- Square doesn't override describe, should inherit from Shape
assert(sq.describe() == "Shape(green)", "Square inherits describe from Shape")
println("PASS: Inheritance and method override")

-- ============================================================
-- 4. Multi-level inheritance
-- ============================================================
class Animal {
    name = "unknown"
    fn speak(self) { return "..." }
    fn get_name(self) { return self.name }
}

class Dog : Animal {
    name = "Rex"
    fn speak(self) { return "Woof!" }
}

class Puppy : Dog {
    name = "Tiny"
    fn play(self) { return "playing!" }
}

let puppy = Puppy()
assert(puppy.speak() == "Woof!", "Puppy inherits Dog.speak")
assert(puppy.get_name() == "Tiny", "Puppy inherits Animal.get_name with own name")
assert(puppy.play() == "playing!", "Puppy has own method")
println("PASS: Multi-level inheritance")

-- ============================================================
-- 5. Multiple inheritance (mixins)
-- ============================================================
class Flyable {
    fn fly(self) { return "flying!" }
}

class Swimmable {
    fn swim(self) { return "swimming!" }
}

class Duck : Animal, Flyable, Swimmable {
    name = "Donald"
    fn speak(self) { return "Quack!" }
}

let duck = Duck()
assert(duck.speak() == "Quack!", "Duck overrides speak")
assert(duck.fly() == "flying!", "Duck inherits fly from Flyable")
assert(duck.swim() == "swimming!", "Duck inherits swim from Swimmable")
assert(duck.get_name() == "Donald", "Duck inherits get_name from Animal")
println("PASS: Multiple inheritance")

-- ============================================================
-- 6. is_a runtime type checking (walks inheritance chain)
-- ============================================================
assert(puppy.is_a("Puppy") == true, "puppy is_a Puppy")
assert(puppy.is_a("Dog") == true, "puppy is_a Dog")
assert(puppy.is_a("Animal") == true, "puppy is_a Animal")
assert(puppy.is_a("Cat") == false, "puppy is NOT a Cat")

assert(duck.is_a("Duck") == true, "duck is_a Duck")
assert(duck.is_a("Animal") == true, "duck is_a Animal")
assert(duck.is_a("Flyable") == true, "duck is_a Flyable")
assert(duck.is_a("Swimmable") == true, "duck is_a Swimmable")
assert(duck.is_a("Dog") == false, "duck is NOT a Dog")
println("PASS: is_a runtime type checking")

-- ============================================================
-- 7. Super calls
-- ============================================================
class Base {
    fn greet(self) { return "Hello from Base" }
    fn info(self) { return "base" }
}

class Child : Base {
    fn greet(self) {
        let parent_msg = super.greet()
        return "Child says hi (parent: " + parent_msg + ")"
    }
}

let child = Child()
assert(child.greet() == "Child says hi (parent: Hello from Base)",
       "super.greet() should call Base.greet()")
assert(child.info() == "base", "Child inherits info from Base")
println("PASS: Super calls")

-- ============================================================
-- 8. Static methods
-- ============================================================
class MathUtils {
    static fn add(a, b) { return a + b }
    static fn multiply(a, b) { return a * b }
    static fn square(x) { return x * x }

    fn instance_method(self) { return "instance" }
}

assert(MathUtils.add(3, 4) == 7, "static add")
assert(MathUtils.multiply(5, 6) == 30, "static multiply")
assert(MathUtils.square(9) == 81, "static square")

let mu = MathUtils()
assert(mu.instance_method() == "instance", "instance methods still work")
println("PASS: Static methods")

-- ============================================================
-- 9. Static method access via field
-- ============================================================
let add_fn = MathUtils.add
assert(add_fn(10, 20) == 30, "static method as first-class value")
println("PASS: Static methods as values")

-- ============================================================
-- 10. Operator overloading with dunder methods
-- ============================================================
class Vec2 {
    x = 0
    y = 0
    fn init(self, x, y) { self.x = x; self.y = y }

    fn __add__(self, other) {
        return Vec2(self.x + other.x, self.y + other.y)
    }
    fn __sub__(self, other) {
        return Vec2(self.x - other.x, self.y - other.y)
    }
    fn __eq__(self, other) {
        return self.x == other.x and self.y == other.y
    }
    fn __lt__(self, other) {
        return (self.x * self.x + self.y * self.y) < (other.x * other.x + other.y * other.y)
    }
    fn __str__(self) {
        return "Vec2(" + str(self.x) + ", " + str(self.y) + ")"
    }
    fn __len__(self) { return 2 }
    fn __index__(self, i) {
        if i == 0 { return self.x }
        if i == 1 { return self.y }
        return null
    }
}

let v1 = Vec2(1, 2)
let v2 = Vec2(3, 4)

-- __add__
let v3 = v1 + v2
assert(v3.x == 4, "v1 + v2 x == 4")
assert(v3.y == 6, "v1 + v2 y == 6")

-- __sub__
let v4 = v2 - v1
assert(v4.x == 2, "v2 - v1 x == 2")
assert(v4.y == 2, "v2 - v1 y == 2")

-- __eq__
assert(v1 == Vec2(1, 2), "Vec2(1,2) == Vec2(1,2)")
assert((v1 == v2) == false, "Vec2(1,2) != Vec2(3,4)")

-- != derived from __eq__
assert(v1 != v2, "v1 != v2")
assert((v1 != Vec2(1, 2)) == false, "v1 == Vec2(1,2) so not !=")

-- __lt__
assert(v1 < v2, "v1 < v2 by magnitude")

-- __str__ via println
println(v1)  -- should print "Vec2(1, 2)"

-- __str__ via str()
assert(str(v1) == "Vec2(1, 2)", "str(v1) uses __str__")

-- __len__
assert(len(v1) == 2, "len(v1) == 2 via __len__")

-- __index__
assert(v1[0] == 1, "v1[0] == 1 via __index__")
assert(v1[1] == 2, "v1[1] == 2 via __index__")

println("PASS: Operator overloading with dunder methods")

-- ============================================================
-- 11. Operator overloading via impl block
-- ============================================================
class Vec3 {
    x = 0
    y = 0
    z = 0
    fn init(self, x, y, z) { self.x = x; self.y = y; self.z = z }
}

impl Vec3 {
    fn +(self, other) {
        return Vec3(self.x + other.x, self.y + other.y, self.z + other.z)
    }
    fn -(self, other) {
        return Vec3(self.x - other.x, self.y - other.y, self.z - other.z)
    }
}

let va = Vec3(1, 2, 3)
let vb = Vec3(4, 5, 6)
let vc = va + vb
assert(vc.x == 5, "Vec3 + x")
assert(vc.y == 7, "Vec3 + y")
assert(vc.z == 9, "Vec3 + z")
let vd = vb - va
assert(vd.x == 3, "Vec3 - x")
assert(vd.y == 3, "Vec3 - y")
assert(vd.z == 3, "Vec3 - z")
println("PASS: Operator overloading via impl block")

-- ============================================================
-- 12. Chained operator overloading
-- ============================================================
let vchain = v1 + v2 + Vec2(10, 10)
assert(vchain.x == 14, "chained + x")
assert(vchain.y == 16, "chained + y")
println("PASS: Chained operator overloading")

-- ============================================================
-- 13. __str__ and to_string fallback
-- ============================================================
class Rect {
    w = 0
    h = 0
    fn init(self, w, h) { self.w = w; self.h = h }
    fn to_string(self) { return "Rect(" + str(self.w) + "x" + str(self.h) + ")" }
}

let r = Rect(5, 3)
assert(str(r) == "Rect(5x3)", "to_string fallback for str()")
println("PASS: to_string fallback")

-- ============================================================
-- 14. __repr__ support
-- ============================================================
class Tag {
    name = ""
    fn init(self, name) { self.name = name }
    fn __repr__(self) { return "Tag(name=" + self.name + ")" }
}

let t = Tag("hello")
assert(repr(t) == "Tag(name=hello)", "repr uses __repr__")
println("PASS: __repr__ support")

-- ============================================================
-- 15. Constructor patterns (init and __init__)
-- ============================================================
class Foo {
    val = 0
    fn init(self, v) { self.val = v }
}

class Bar {
    val = 0
    fn __init__(self, v) { self.val = v }
}

let foo = Foo(42)
let bar = Bar(99)
assert(foo.val == 42, "init constructor")
assert(bar.val == 99, "__init__ constructor")
println("PASS: Constructor patterns")

-- ============================================================
-- 16. Property access via self.field in methods
-- ============================================================
class Counter {
    count = 0

    fn increment(self) {
        self.count = self.count + 1
    }

    fn get_count(self) {
        return self.count
    }

    fn reset(self) {
        self.count = 0
    }
}

let ctr = Counter()
assert(ctr.get_count() == 0, "initial count is 0")
ctr.increment()
ctr.increment()
ctr.increment()
assert(ctr.get_count() == 3, "count after 3 increments")
ctr.reset()
assert(ctr.get_count() == 0, "count after reset")
println("PASS: Property access via self.field")

-- ============================================================
-- 17. impl blocks add methods to existing classes
-- ============================================================
class SimpleClass {
    value = 0
    fn init(self, v) { self.value = v }
}

impl SimpleClass {
    fn doubled(self) { return self.value * 2 }
    fn tripled(self) { return self.value * 3 }
}

let sc = SimpleClass(5)
assert(sc.doubled() == 10, "impl block adds doubled()")
assert(sc.tripled() == 15, "impl block adds tripled()")
println("PASS: impl blocks add methods")

-- ============================================================
-- 18. Static method inheritance
-- ============================================================
class BaseStatic {
    static fn create(x) { return x * 2 }
}

class DerivedStatic : BaseStatic {
    static fn transform(x) { return x + 10 }
}

assert(DerivedStatic.create(5) == 10, "inherited static method")
assert(DerivedStatic.transform(5) == 15, "own static method")
println("PASS: Static method inheritance")

-- ============================================================
-- 19. Method returns self for chaining (conceptual test)
-- ============================================================
class Builder {
    parts = ""

    fn add(self, part) {
        self.parts = self.parts + part
        return self
    }

    fn build(self) {
        return self.parts
    }
}

let b = Builder()
-- Note: fluent chaining requires self return
b.add("Hello")
b.add(" ")
b.add("World")
assert(b.build() == "Hello World", "builder pattern")
println("PASS: Builder pattern with self mutation")

-- ============================================================
-- 20. Complex inheritance scenario
-- ============================================================
class Vehicle {
    speed = 0
    fn init(self, speed) { self.speed = speed }
    fn type_name(self) { return "Vehicle" }
}

class Car : Vehicle {
    doors = 4
    fn init(self, speed, doors) {
        self.speed = speed
        self.doors = doors
    }
    fn type_name(self) { return "Car" }
}

class ElectricCar : Car {
    battery = 100
    fn init(self, speed, doors, battery) {
        self.speed = speed
        self.doors = doors
        self.battery = battery
    }
    fn type_name(self) { return "ElectricCar" }
}

let ev = ElectricCar(150, 4, 85)
assert(ev.speed == 150, "ElectricCar inherits speed")
assert(ev.doors == 4, "ElectricCar inherits doors")
assert(ev.battery == 85, "ElectricCar has battery")
assert(ev.type_name() == "ElectricCar", "ElectricCar overrides type_name")
assert(ev.is_a("ElectricCar") == true, "ev is_a ElectricCar")
assert(ev.is_a("Car") == true, "ev is_a Car")
assert(ev.is_a("Vehicle") == true, "ev is_a Vehicle")
println("PASS: Complex inheritance scenario")

-- ============================================================
-- Summary
-- ============================================================
println("")
println("All OOP tests passed!")
