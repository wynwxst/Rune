# Generic types

**Generic structs, and a method that changes the parameters**

```rune
import std::io

struct Pair<A, B> {
    pub first: A
    pub second: B

    pub fn swapped(&self) -> Pair<B, A> {
        Pair<B, A> { first: self.second, second: self.first }
    }
}

struct Stack<T> {
    items: [8:T]
    count: i64

    pub fn depth(&self) -> i64 { self.count }
    pub fn top(&self, empty: T) -> T {
        if self.count == 0 { return empty }
        self.items[self.count - 1]
    }
}

fn main() -> i64 {
    let p = Pair<i64, String> { first: 1, second: "one" }
    io::println(p.first.$str() + " " + p.second)

    let flipped = p.swapped()
    io::println(flipped.first + " " + flipped.second.$str())

    let s = Stack<i64> { items: [10, 20, 30, 0, 0, 0, 0, 0], count: 3 }
    io::println(s.depth().$str() + " " + s.top(-1).$str())
    0
}
```
