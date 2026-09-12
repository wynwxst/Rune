# From the library

| Written | Walks |
| --- | --- |
| `for v in vec` | a `Vector<T>`, through `VectorIter<T>` |
| `for (k, v) in map` | a `Map<K, V>`, as pairs |
| `for c in text::chars(s)` | the characters of a `String`, in one linear pass |
| `iter::counting(n)` | integers from `n`, endlessly |
| `iter::countingBy(n, step)` | the same, in steps |
| `it.collect()` | runs an iterator into a `Vector` |
| `vector::collect(it)` | the same, written the other way round |

**The containers that come with it**

```rune
import std::io
import std::text
import std::collections::vector

fn main() -> i64 {
    var names = vector::Vector<String>()
    names.push("ada")
    names.push("grace")
    for name in names { io::println(name) }

    for c in text::chars("héllo") { io::print(c.$str() + "·") }
    io::newline()
    0
}
```
