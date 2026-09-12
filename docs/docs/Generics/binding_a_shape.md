# Binding a shape

A `bind` target may be a shape rather than a declaration — `[T]`, `[N:T]`, `(A, B)`, `&T` — and then every type of that shape gets the binding. An array is usable wherever a slice is, so one written for `[T]` covers both.

**One binding, every slice and array**

```rune
import std::io

/// A mark of one's own, over every slice and array of a summable element.
mark Total { fn total(&self) -> i64 }
bind Total to i64 { fn total(&self) -> i64 { self } }

bind<T> Total to [T] where T: Total {
    fn total(&self) -> i64 {
        var sum = 0
        for i in 0..self.$length() { sum += self[i].total() }
        sum
    }
}

bind<A, B> Total to (A, B) where A: Total, B: Total {
    fn total(&self) -> i64 { self.0.total() + self.1.total() }
}

fn sum<T: Total>(value: T) -> i64 { value.total() }

fn main() -> i64 {
    let ints: [3:i64] = [1, 2, 3]
    io::println(sum(ints))          // an array
    io::println(sum(ints[1..3]))    // a slice of it
    io::println(sum((4, 5)))        // a pair
    0
}
```

`std::io` uses exactly this, which is why an array prints without anything having to be written for its element type.

**What the library binds for you**

```rune
import std::io

fn main() -> i64 {
    io::println([1, 2, 3])
    io::println((7, "seven"))
    io::println([[1, 2], [3, 4]])
    println!("{} and {}", [1.5, 2.5], (1, true))
    0
}
```

> [!NOTE]
> **Only where it holds**
>
> The `where` clause is what decides which shapes are covered. A slice of something that is not `Display` is not `Display` either, and asking is how you find out.
