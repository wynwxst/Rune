# std::atomic

A `Counter` and a `Flag` that several threads may change at once without a
lock. Both are classes that synchronise themselves, so both are `Sync`, and
every method takes `&self`: the threads of a `thread::scope` share one through
the `&` the scope lends them.

## A counter

```rune
import std::io
import std::atomic
import std::thread

fn bump(c: &atomic::Counter) -> i64 {
    for _ in 0..1000 { c.increment() }
    0
}

fn main() -> i64 {
    thread::scope(atomic::Counter(0), ||(s: &thread::Scope<atomic::Counter>) -> i64 {
        var a = s.spawn(bump, s.env())
        var b = s.spawn(bump, s.env())
        a.join(); b.join()
        let c = s.env()
        io::println(c.load())
        io::println(c.add(5))
        io::println(c.exchange(0))
        io::println(c.compareExchange(0, 42))
        io::println(c.load())
        io::println(c.raiseTo(10))
        0
    })
}
```

## A flag: doing something exactly once

`raise` says whether *this* call was the one that raised it, which is what
makes it a one-shot latch across threads.

```rune
import std::io
import std::atomic
import std::thread

fn tryClaim(f: &atomic::Flag) -> i64 {
    if f.raise() { 1 } else { 0 }
}

fn main() -> i64 {
    thread::scope(atomic::Flag(false), ||(s: &thread::Scope<atomic::Flag>) -> i64 {
        var a = s.spawn(tryClaim, s.env())
        var b = s.spawn(tryClaim, s.env())
        var c = s.spawn(tryClaim, s.env())
        io::println(a.join() + b.join() + c.join())      // exactly one claimed it
        io::println(s.env().isRaised())
        io::println(s.env().lower())
        0
    })
}
```
