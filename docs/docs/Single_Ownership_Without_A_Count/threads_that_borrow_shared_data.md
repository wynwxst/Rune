# Threads that borrow shared data

`thread::scope` runs threads that may borrow the data around them and joins every one before it returns, so a borrow a thread takes never outlives what it points at. A thread is only ever handed something built from the scope's environment — `argument: A from self`, checked at the call — never a local of the body it could outlive. A shared `&T` may cross into a thread exactly when `T` is `Sync`.

**Scoped threads over shared state**

```rune
import std::io
import std::thread
import std::atomic

fn bump(c: &atomic::Counter) -> i64 {
    var i = 0
    while i < 100 { c.increment(); i += 1 }
    0
}

fn main() -> i64 {
    let counter = atomic::Counter(0)
    let total = thread::scope(counter,
        ||(s: &thread::Scope<atomic::Counter>) -> i64 {
            let a = s.spawn(bump, s.env())
            let b = s.spawn(bump, s.env())
            a.join()
            b.join()
            s.env().load()
        })
    io::println(total.$str())     // 200
    0
}
```
