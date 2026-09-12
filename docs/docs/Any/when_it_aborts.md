# When it aborts

`expect` is for a type that is an invariant of the program rather than something to be checked. It names both types on the way out.

**`expect` on the wrong type**

```rune
import std::io

fn main() -> i64 {
    let boxed: Any = 3.5
    io::println("before")
    io::println(boxed.expect::<i64>())
    0
}
```
