# Generic functions

Type arguments are usually inferred from the call. When they cannot be, write them with a turbofish.

**Inference, and the turbofish when it is needed**

```rune
import std::io

fn identity<T>(value: T) -> T { value }

fn pair<A, B>(a: A, b: B) -> (A, B) { (a, b) }

fn firstOr<T>(values: [T], fallback: T) -> T {
    if values.$isEmpty() { return fallback }
    values[0]
}

fn main() -> i64 {
    io::println(identity(7))
    io::println(identity("text"))
    io::println(identity::<f64>(2.5))       // written out

    let p = pair(1, "one")
    io::println(p.0.$str() + " " + p.1)

    let numbers: [3:i64] = [4, 5, 6]
    io::println(firstOr(numbers, -1))
    io::println(firstOr([0; 0], -1))
    0
}
```
