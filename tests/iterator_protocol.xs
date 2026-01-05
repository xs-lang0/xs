struct Counter {
    max: i32,
    cur: i32,
}

impl Counter {
    fn iter(self) -> Counter { self }
    fn next(self) -> i32? {
        if self.cur >= self.max { return None }
        let val = self.cur
        self.cur = self.cur + 1
        Some(val)
    }
}

let c = Counter { max: 5, cur: 0 }
var sum = 0
for n in c {
    sum = sum + n
}
assert(sum == 10, "iterator sum 0+1+2+3+4 == 10")
println("iterator protocol test passed")
