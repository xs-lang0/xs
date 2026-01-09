-- classes, inheritance, constructors, methods

-- basic class
class Animal {
    name = ""
    sound = "..."

    fn init(self, name) {
        self.name = name
    }

    fn speak(self) {
        return "{self.name} says {self.sound}"
    }
}

let cat = Animal("Cat")
cat.sound = "meow"
assert_eq(cat.name, "Cat")
assert_eq(cat.speak(), "Cat says meow")

-- class with multiple methods
class Counter {
    count = 0

    fn init(self, start) {
        self.count = start
    }

    fn inc(self) { self.count = self.count + 1 }
    fn dec(self) { self.count = self.count - 1 }
    fn value(self) { return self.count }
}

let c = Counter(0)
c.inc()
c.inc()
c.inc()
c.dec()
assert_eq(c.value(), 2)

-- inheritance
class Dog : Animal {
    fn init(self, name) {
        super.init(name)
        self.sound = "woof"
    }

    fn fetch(self) {
        return "{self.name} fetches the ball"
    }
}

let d = Dog("Rex")
assert_eq(d.name, "Rex")
assert_eq(d.sound, "woof")
assert_eq(d.speak(), "Rex says woof")
assert_eq(d.fetch(), "Rex fetches the ball")

-- static-like class usage
class MathHelper {
    fn init(self) {}

    fn square(self, x) { return x * x }
    fn cube(self, x) { return x * x * x }
}
let mh = MathHelper()
assert_eq(mh.square(5), 25)
assert_eq(mh.cube(3), 27)

println("test_classes: all passed")
