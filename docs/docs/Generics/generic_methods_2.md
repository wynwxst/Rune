# Generic methods

A method may introduce its own type parameters, independent of the type's.

**A method with its own parameter**

```rune
import std::io

struct Holder<T> {
    pub value: T

    pub fn mapped<U>(&self, f: @function(T) -> U) -> Holder<U> {
        Holder<U> { value: f(self.value) }
    }
}

fn main() -> i64 {
    let n = Holder<i64> { value: 21 }
    let doubled = n.mapped::<i64>(||(v: i64) -> i64 { v * 2 })
    let described = n.mapped::<String>(||(v: i64) -> String { "n=" + v.$str() })

    io::println(doubled.value)
    io::println(described.value)
    0
}
```
