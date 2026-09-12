# Shared mutable state

`Mutex<T>` is the one type that is `Sync` while holding something that is not — because it is the one type that synchronises access to what it holds. It says so with `@sync("reason")`, which is the single place the compiler takes such a claim on trust.

**Four threads, one counter**

```rune
import std::io
import std::thread

fn addOne(n: i64) -> i64 { n + 1 }

/// Four of these run at once against the same counter.
fn bumpAThousand(shared: thread::Mutex<i64>) -> i64 {
    var i = 0
    while i < 1000 { shared.withLock(addOne); i += 1 }
    0
}

fn main() -> i64 {
    let total = thread::Mutex<i64>(0)

    var a = thread::spawn(bumpAThousand, total)
    var b = thread::spawn(bumpAThousand, total)
    var c = thread::spawn(bumpAThousand, total)
    var d = thread::spawn(bumpAThousand, total)
    a.join(); b.join(); c.join(); d.join()

    println!("{}", total.get())
    0
}
```

`withLock` takes a `fn` from the value to what it should become, and holds the lock for exactly as long as that runs. There is no way to keep a reference to what is inside past the moment the lock is released, because none is ever handed out.

| Method | Does |
| --- | --- |
| `withLock(f)` | replaces the value with `f(value)`, locked |
| `get()` | a copy of the value, read locked |
| `set(v)` | replaces the value, locked |
