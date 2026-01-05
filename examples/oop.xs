// XS Language -- Object-Oriented Programming Example
// Demonstrates: struct, impl, methods, polymorphism via duck typing

struct Circle { color, x, y, radius }

impl Circle {
    fn draw(self) {
        return "Circle(r=" ++ str(self.radius) ++ ") at (" ++ str(self.x) ++ ", " ++ str(self.y) ++ ") [" ++ self.color ++ "]"
    }
    fn area(self) {
        return 3.14159 * self.radius * self.radius
    }
    fn describe(self) {
        return self.color ++ " circle"
    }
}

struct Rectangle { color, x, y, width, height }

impl Rectangle {
    fn draw(self) {
        return "Rect(" ++ str(self.width) ++ "x" ++ str(self.height) ++ ") at (" ++ str(self.x) ++ ", " ++ str(self.y) ++ ") [" ++ self.color ++ "]"
    }
    fn area(self) {
        return self.width * self.height
    }
    fn is_square(self) {
        return self.width == self.height
    }
}

struct Triangle { color, base, height }

impl Triangle {
    fn draw(self) {
        return "Triangle(base=" ++ str(self.base) ++ ", h=" ++ str(self.height) ++ ") [" ++ self.color ++ "]"
    }
    fn area(self) {
        return 0.5 * self.base * self.height
    }
}

// Canvas holds multiple shapes
struct Canvas { width, height, shapes }

fn new_canvas(w, h) {
    return Canvas { width: w, height: h, shapes: [] }
}

impl Canvas {
    fn add(self, shape) {
        self.shapes.push(shape)
    }
    fn render(self) {
        println("Canvas ({self.width}x{self.height}):")
        for shape in self.shapes {
            println("  - {shape.draw()}")
        }
    }
    fn total_area(self) {
        var total = 0.0
        for shape in self.shapes {
            total = total + shape.area()
        }
        return total
    }
    fn largest(self) {
        var best = null
        var best_area = 0.0
        for s in self.shapes {
            let a = s.area()
            if best == null {
                best = s
                best_area = a
            } elif a > best_area {
                best = s
                best_area = a
            }
        }
        return best
    }
}

fn main() {
    println("=== XS OOP Demo ===")
    println("")

    // Create shapes
    let c = Circle { color: "red", x: 10.0, y: 20.0, radius: 5.0 }
    let r = Rectangle { color: "blue", x: 0.0, y: 0.0, width: 10.0, height: 6.0 }
    let t = Triangle { color: "green", base: 8.0, height: 4.0 }

    println("Shapes:")
    println("  {c.draw()}")
    println("  {r.draw()}")
    println("  {t.draw()}")
    println("")

    println("Areas:")
    println("  Circle: {c.area()}")
    println("  Rectangle: {r.area()}")
    println("  Triangle: {t.area()}")
    println("")

    // Canvas
    let canvas = new_canvas(1920.0, 1080.0)
    canvas.add(c)
    canvas.add(r)
    canvas.add(t)
    canvas.add(Circle { color: "purple", x: 50.0, y: 50.0, radius: 15.0 })
    canvas.add(Rectangle { color: "orange", x: 100.0, y: 100.0, width: 20.0, height: 30.0 })

    canvas.render()
    println("")
    println("Total area: {canvas.total_area()}")

    let big = canvas.largest()
    if big != null {
        println("Largest shape: {big.draw()}")
    }

    println("")
    println("Is r a square? {r.is_square()}")
    let sq = Rectangle { color: "yellow", x: 0.0, y: 0.0, width: 5.0, height: 5.0 }
    println("Is sq a square? {sq.is_square()}")
}
