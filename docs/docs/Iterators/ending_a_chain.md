# Ending a chain

The adaptors compute nothing. Something has to ask, and these are what ask: `collect` runs the whole chain into a `Vector`, and the rest answer a question about it. `find`, `any` and `all` stop as soon as the answer is settled, so a chain over something endless still ends.

| Written | Hands back |
| --- | --- |
| `.collect()` | everything left, in a `vector::Vector` |
| `.count()` | how many values are left; walks to the end |
| `.find(keep)` | the first value `keep` says yes to, or `nil` |
| `.any(keep)` | true at the first yes |
| `.all(keep)` | false at the first no |

*Running a chain out*

**Running a chain out**

```rune
import std::io
import std::iter
import std::collections::vector

fn main() -> i64 {
    var numbers = vector::Vector<i64>()
    var n = 1
    while n <= 6 { numbers.push(n); n += 1 }

    let evens = numbers.filter(||(v: i64) -> bool { v % 2 == 0 }).collect()
    io::println(evens)

    io::println(numbers.as_iter().skip(2).take(3).collect())
    io::println(numbers.as_iter().count())
    io::println(numbers.as_iter().any(||(v: i64) -> bool { v > 5 }))
    io::println(numbers.as_iter().all(||(v: i64) -> bool { v > 5 }))
    match numbers.as_iter().find(||(v: i64) -> bool { v % 4 == 0 }) {
        Some(v) => io::println("first multiple of four: " + v.$str()),
        None    => io::println("none"),
    }

    for (i, v) in numbers.enumerate() { io::print(i.$str() + ":" + v.$str() + " ") }
    io::newline()
    0
}
```

> [!NOTE]
> **Note**
>
> `vector::collect(it)` is the same thing written the other way round, and still there. The method is what a chain reads better with; the free function is what `std::iter` is written against, because a mark cannot depend on a container that binds it.
