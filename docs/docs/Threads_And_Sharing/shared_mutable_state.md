# Shared mutable state

`Mutex<T>` is the one type that is `Sync` while holding something that is not — because it is the one type that synchronises access to what it holds. It says so with `@sync("reason")`, which is the single place the compiler takes such a claim on trust.

**Four threads, one counter**

```rune
import std::io
import std::thread

fn addOne(n: i64) -> i64 { n + 1 }

/// Four of these run at once against the same counter.
fn bumpAThousand(shared: &thread::Mutex<i64>) -> i64 {
    var i = 0
    while i < 1000 { shared.withLock(addOne); i += 1 }
    0
}

fn main() -> i64 {
    // The scope lends the mutex to every thread it starts, and joins them
    // before it lets go of it.
    let total = thread::scope(thread::Mutex<i64>(0),
        ||(s: &thread::Scope<thread::Mutex<i64>>) -> i64 {
            var a = s.spawn(bumpAThousand, s.env())
            var b = s.spawn(bumpAThousand, s.env())
            var c = s.spawn(bumpAThousand, s.env())
            var d = s.spawn(bumpAThousand, s.env())
            a.join(); b.join(); c.join(); d.join()
            s.env().get()
        })
    println!("{}", total)
    0
}
```

Every method of a `Mutex` takes `&self` — the lock is what makes changing the value through a shared borrow safe — so the threads of a `thread::scope` all use the one they are lent. `withLock` takes a `fn` from the value to what it should become, and holds the lock for exactly as long as that runs. There is no way to keep a reference to what is inside past the moment the lock is released, because none is ever handed out.

| Method | Does |
| --- | --- |
| `withLock(f)` | replaces the value with `f(value)`, locked |
| `get()` | a copy of the value, read locked |
| `set(v)` | replaces the value, locked |
