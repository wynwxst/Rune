# Blocks and closures

`task::spawn` starts a closure as a task. An `async { }` block is the same thing written in place — its value is the future — and an `async ||(...)` closure starts a task each time it is called.

**Three ways to start one**

```rune
import std::io
import std::task

fn main() -> i64 {
    let work = task::spawn(||() -> i64 { 3 + 4 })
    io::println(work.wait())

    let base = 5
    let block = async { base + 3 }
    io::println(block.wait())

    let scale = async ||(x: i64) -> i64 { x * 5 }
    io::println(scale(2).wait())
    0
}
```

A block or closure captures what it uses by value, as every closure does: the task gets its own copy, made when it starts.
