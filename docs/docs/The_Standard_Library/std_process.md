# std::process

| Function | Signature | Does |
| --- | --- | --- |
| `argCount` | `() -> i64` | argument count, program name included |
| `arg` | `(index: i64) -> String` | one argument; empty when out of range |
| `argument` | `(index: i64) -> String?` | the same, but absent and empty are told apart |
| `programName` | `() -> String` | argument 0 |
| `args` / `arguments` | `() -> Vector<String>` | all of them, with and without the program name |
| `exit` | `(code: i32) -> Never` | stops now; no deinits run |
| `succeed` / `fail` | `() -> Never` | `exit(0)` and `exit(1)` |
| `panic` | `<M: io::Display>(message: M) -> Never` | aborts with your message — a literal, a built `String`, or anything else printable |
| `assert` | `<M: io::Display>(condition: bool, message: M)` | panics if false |
| `liveObjectCount` | `() -> i64` | objects the runtime is still counting |

> [!NOTE]
> **They do not return**
>
> `exit`, `succeed`, `fail` and `panic` are declared `-> Never`. A `Never` converts to any type, so a call to one can stand wherever a value was expected. See [`Never`: a function that does not return](#functions).

> [!NOTE]
> **Why a bound rather than two functions**
>
> `panic` and `assert` are generic over `io::Display` rather than overloaded. A free function cannot be overloaded in Rune — only a `bind` can — and a bound says what it requires instead of listing what it accepts, which is the better answer here anyway.

**The process itself**

```rune
import std::io
import std::process

fn main() -> i64 {
    io::println("invoked as " + process::arg(0))
    io::println("argument count " + process::argCount().$str())
    process::assert(process::argCount() >= 1, "there is always argv[0]")
    io::println("still live: " + process::liveObjectCount().$str())
    0
}
```
