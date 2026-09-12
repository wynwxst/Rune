# std::process

The program as the operating system sees it: what it was invoked with, how
it stops, and how many objects it still holds.

## Arguments

```rune
import std::io
import std::process

fn main() -> i64 {
    io::println(process::argCount() >= 1)
    io::println(process::programName().$isEmpty())
    io::println(process::argument(99).isNil())
    io::println(process::arg(99).$isEmpty())      // out of range is empty, not an abort
    for a in process::arguments() { io::println(a) }
    0
}
```

## Stopping

`exit`, `succeed`, `fail` and `panic` are declared `-> Never`. A `Never`
converts to any type, so a call to one can stand wherever a value was
expected — which is what lets a function that promised a value end in one.

```rune
import std::io
import std::process

fn pick(n: i64) -> i64 {
    if n > 0 { return n }
    process::panic("pick: " + n.$str() + " is not positive")
}

fn main() -> i64 {
    io::println(pick(3))
    process::assert(process::liveObjectCount() >= 0, "the count is never negative")
    io::println("done")
    process::succeed()
}
```
