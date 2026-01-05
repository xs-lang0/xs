-- Test class inheritance

class Animal {
    name = "unknown"
    fn speak(self) {
        return "..."
    }
    fn get_name(self) {
        return self.name
    }
}

class Dog : Animal {
    name = "Rex"
    fn speak(self) {
        return "Woof!"
    }
}

class Puppy : Dog {
    name = "Tiny"
    fn play(self) {
        return "playing!"
    }
}

-- Test basic inheritance
let a = Animal()
print(a.speak())       -- ...
print(a.get_name())    -- unknown

-- Test single inheritance with method override
let d = Dog()
print(d.speak())       -- Woof!
print(d.get_name())    -- Rex

-- Test multi-level inheritance
let p = Puppy()
print(p.speak())       -- Woof! (inherited from Dog)
print(p.get_name())    -- Tiny
print(p.play())        -- playing!

-- Test is_a with inheritance
print(p.is_a("Puppy"))   -- true
print(p.is_a("Dog"))     -- true
print(p.is_a("Animal"))  -- true
print(p.is_a("Cat"))     -- false

-- Test multiple bases
class Flyable {
    fn fly(self) {
        return "flying!"
    }
}

class Swimmable {
    fn swim(self) {
        return "swimming!"
    }
}

class Duck : Animal, Flyable, Swimmable {
    name = "Donald"
    fn speak(self) {
        return "Quack!"
    }
}

let duck = Duck()
print(duck.speak())      -- Quack!
print(duck.get_name())   -- Donald
print(duck.fly())        -- flying!
print(duck.swim())       -- swimming!
print(duck.is_a("Duck"))     -- true
print(duck.is_a("Animal"))   -- true
print(duck.is_a("Flyable"))  -- true
print(duck.is_a("Swimmable"))// true
