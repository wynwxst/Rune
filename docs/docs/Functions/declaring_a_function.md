# Declaring a function

**Result, early return, and no result**

```rune
import std::io

fn add(a: i64, b: i64) -> i64 {
    a + b                        // the last expression is the result
}

fn shout(text: String) -> String {
    return text + "!"            // `return` works too, and exits early
}

fn log(message: String) {        // no `->` means the result is ()
    io::println("log: " + message)
}

fn main() -> i64 {
    io::println(add(20, 22))
    io::println(shout("hey"))
    log("done")
    0
}
```
