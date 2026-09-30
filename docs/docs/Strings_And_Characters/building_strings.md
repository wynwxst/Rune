# Building strings

`+` concatenates. Every primitive has `.$str()`, which is what makes building a message practical.

**Concatenation and `.$str()`**

```rune
import std::io

fn main() -> i64 {
    let name = "Rune"
    let version = 1
    let ratio = 0.5
    let ready = true
    let initial = 'R'

    let message = name + " v" + version.$str() + " ratio=" + ratio.$str() +
                  " ready=" + ready.$str() + " initial=" + initial.$str()
    io::println(message)

    var built = ""
    for i in 1..=5 { built += i.$str() + "," }
    io::println(built)

    io::println("-".$repeat(20))
    io::println("ab".$repeat(3))
    0
}
```
