# std::io

| Function | Signature | Does |
| --- | --- | --- |
| `print` | `<T: Display>(value: T)` | writes to stdout |
| `println` | `<T: Display>(value: T)` | and a newline |
| `eprint` | `<T: Display>(value: T)` | writes to stderr |
| `eprintln` | `<T: Display>(value: T)` | and a newline |
| `debug` | `<T: Display>(value: T)` | stderr, prefixed |
| `newline` | `()` | a bare newline |
| `readLine` | `() -> String` | a line from stdin, newline removed |
| `readLineOrEnd` | `() -> String?` | the same, and `nil` at the end of the input — which is what a loop needs |

*`Display` is the public mark in this module; anything that binds it can be printed.*

Every builtin binds `Display`, and so do the *shapes*: `bind<T> Display to [T] where T: Display` covers every slice and every array of something printable, and there are pair and triple bindings beside it. So an array prints without anything having to be written for its element type — see [Binding a shape](#generics).

**The shapes the library binds**

```rune
import std::io

fn main() -> i64 {
    io::println([1, 2, 3])
    io::println((7, "seven"))
    io::println([["a", "b"], ["c", "d"]])
    0
}
```

> [!NOTE]
> **Reading until the end**
>
> `readLine` cannot tell a blank line from the end of the input: both give an empty string. `readLineOrEnd` can, which is why it is the one to loop over — `while io::readLineOrEnd() is Some(line)` ends where the input does.

`println!` and `print!` live here too. They take a format string, need no import, and are what most code reaches for; the functions above are what they call. See **Formatting**.

**Printing your own types**

```rune
import std::io

struct Duration { seconds: i64 }

bind io::Display to Duration {
    fn display(&self) -> String {
        let m = self.seconds / 60
        let s = self.seconds % 60
        m.$str() + "m" + s.$str() + "s"
    }
}

fn main() -> i64 {
    io::println(Duration { seconds: 205 })
    io::print("no newline: ")
    io::println(42)
    io::eprintln("this went to stderr")
    0
}
```
