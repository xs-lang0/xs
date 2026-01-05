-- Test all newly ported features

-- === 1. Map Comprehensions ===
let squares = {x: x * x for x in range(5)}
println(squares)

-- === 2. Class Inheritance ===
class Animal {
    let name = "unknown"
    fn speak(self) { return "..." }
    fn get_name(self) { return self.name }
}

class Dog : Animal {
    let name = "Rex"
    fn speak(self) { return "Woof!" }
}

let d = Dog()
println(d.speak())
println(d.get_name())
println(d.is_a("Animal"))

-- === 3. Actors ===
actor Counter {
    let count = 0

    fn handle(msg) {
        if msg == "inc" {
            count = count + 1
        }
        if msg == "get" {
            println(count)
        }
    }
}

let c = Counter()
c ! "inc"
c ! "inc"
c ! "inc"
c ! "get"

-- === 4. Reactive Signals ===
let s = signal(10)
println(s.get())
s.set(42)
println(s.get())

let doubled = derived(fn() { return s.get() * 2 })
println(doubled.get())

println("All features working!")
