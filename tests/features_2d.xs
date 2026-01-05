-- Operator overloading
struct Vec2 { x, y }
impl Vec2 {
    fn +(self, other) { Vec2 { x: self.x + other.x, y: self.y + other.y } }
    fn to_str(self) { "({self.x}, {self.y})" }
}
let a = Vec2 { x: 1, y: 2 }
let b = Vec2 { x: 3, y: 4 }
let c = a + b
println(c.to_str())

-- Enum methods
enum Direction { North, South, East, West }
impl Direction {
    fn opposite(self) {
        match self {
            Direction::North => Direction::South,
            Direction::South => Direction::North,
            Direction::East => Direction::West,
            Direction::West => Direction::East,
        }
    }
    fn to_str(self) {
        match self {
            Direction::North => "North",
            Direction::South => "South",
            Direction::East => "East",
            Direction::West => "West",
        }
    }
}
let d = Direction::North
println(d.opposite().to_str())

println("2d features passed")
