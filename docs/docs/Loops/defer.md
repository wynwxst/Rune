# `defer`

`defer` schedules an expression to run when the enclosing block ends, whichever way it ends. Deferred expressions run in reverse order.

**Cleanup that always runs**

```rune
import std::io

fn work(fail: bool) -> i64 {
    defer io::println("  cleanup A")
    defer io::println("  cleanup B")
    if fail {
        io::println("  bailing out")
        return -1
    }
    io::println("  finished")
    0
}

fn main() -> i64 {
    io::println("normal:")
    work(false)
    io::println("early return:")
    work(true)
    0
}
```
