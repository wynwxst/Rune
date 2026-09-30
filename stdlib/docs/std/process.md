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

## Running another program

`Command` runs a program to completion and collects both of its output
streams. Arguments are handed over one at a time and never pass through a
shell, so a space or a quote inside one stays part of it. `run` gives `nil`
only when the program could not be started; one that starts and then fails
comes back with a non-zero `status`.

```rune
import std::io
import std::process

fn main() -> i64 {
    var c = process::Command("sh")
    c.arg("-c")
    c.arg("echo it said this; exit 3")
    match c.run() {
        Some(out) => {
            io::print(out.stdout)                 // it said this
            io::println(out.status)               // 3
            io::println(out.succeeded())          // false
        },
        None => io::println("sh is not on PATH"),
    }
    io::println(process::Command("no-such-program-anywhere").run().isNil())   // true
    0
}
```

## On WebAssembly

A WASI module has its arguments, its environment and `exit`, but no way to
start another program: `Command::run` fails as it does for a program that is
not there, and hands back `nil`.
