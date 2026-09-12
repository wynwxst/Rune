# `Never`: a function that does not return

`Never` is the type of an expression that does not come back. `process::exit`, `succeed`, `fail` and `panic` are declared `-> Never`, and a `Never` converts to any type — so a call to one can stand at the end of a function that promised a value, or in the branch of an `if` or `match` that has nothing to produce. No `return` is needed after it: there is no after.

**A diverging call where a value was expected**

```rune
import std::io
import std::process

fn pick(n: i64) -> i64 {
    if n > 0 { return n }
    process::panic("negative")     // `Never` fills the `i64` this promised
}

fn describe(n: i64) -> String {
    if n == 1 { "one" } else { process::fail() }
}

fn main() -> i64 {
    io::println(pick(3))
    io::println(describe(1))
    let x: i64 = if true { 5 } else { process::panic("no") }
    io::println(x)
    0
}
```

A function of your own may be declared `-> Never`. Every path has to end in something that does not return; falling off the end with a value is a type error.

**Falling off the end of `-> Never`**

```rune
import std::io

fn bad() -> Never { io::println("x") }

fn main() -> i64 { bad() }
```

> [!NOTE]
> **Nothing is cleaned up**
>
> `exit` and `panic` leave immediately: no `deinit` runs. That is the difference from returning — a `Never` is not an empty value, it is the absence of one.
