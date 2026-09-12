# std::option

`Option<T>` is a value that may be absent. It is an ordinary enum the compiler
knows by name, so `T?`, `nil`, `??` and `?` are sugar over it rather than
separate machinery, and `Some` and `None` are in scope everywhere.

## The sugar, and what it stands for

```rune
import std::io

fn firstEven(values: [i64]) -> i64? {
    for v in values { if v % 2 == 0 { return v } }
    nil                                 // Option::None
}

fn main() -> i64 {
    let found = firstEven([1, 4, 5])    // i64? is Option<i64>
    io::println(found ?? -1)            // the value, or a default
    io::println(firstEven([1, 3]) ?? -1)
    match found {
        Some(v) => io::println("found " + v.$str()),
        None => io::println("nothing"),
    }
    if found is Some(v) { io::println(v * 2) }
    0
}
```

## Working with one without unwrapping it

A chain of these is one calculation where the unwrapping version is four
nil checks.

```rune
import std::io

struct Config { pub port: i64?, pub name: String }

fn main() -> i64 {
    let config: Config? = Config { port: 8080, name: "api" }
    let port = config.andThen(||(c: Config) -> i64? { c.port })
    io::println(port ?? 80)
    let label = config.map(||(c: Config) -> String { c.name }).or("unnamed")
    io::println(label)
    let big = port.filter(||(p: i64) -> bool { p > 1024 })
    io::println(big.hasValue())
    let fromFlag: i64? = nil
    io::println(fromFlag.otherwise(port) ?? 0)
    io::println(port.zip(config.map(||(c: Config) -> String { c.name })).hasValue())
    0
}
```

## `take`: moving a value out

`take` hands the value over and leaves the option empty — how a field that
owns something is moved out of without the owner keeping a copy.

```rune
import std::io

struct Job { pub id: i64 }
struct Queue { pub pending: Job? }

fn main() -> i64 {
    var q = Queue { pending: Job { id: 7 } }
    match q.pending.take() {
        Some(job) => io::println("running " + job.id.$str()),
        None => io::println("idle"),
    }
    io::println(q.pending.isNil())
    0
}
```
