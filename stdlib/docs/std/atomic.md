# std::atomic

A `Counter` and a `Flag` that several threads may change at once without a
lock. Both are classes that synchronise themselves, so both are `Sync`.

## A counter

```rune
import std::io
import std::atomic
import std::thread

fn bump(c: atomic::Counter) -> i64 {
    for _ in 0..1000 { c.increment() }
    0
}

fn main() -> i64 {
    let c = atomic::Counter(0)
    var a = thread::spawn(bump, c)
    var b = thread::spawn(bump, c)
    a.join(); b.join()
    io::println(c.load())
    io::println(c.add(5))
    io::println(c.exchange(0))
    io::println(c.compareExchange(0, 42))
    io::println(c.load())
    io::println(c.raiseTo(10))
    0
}
```

## A flag: doing something exactly once

`raise` says whether *this* call was the one that raised it, which is what
makes it a one-shot latch across threads.

```rune
import std::io
import std::atomic
import std::thread

fn tryClaim(f: atomic::Flag) -> i64 {
    if f.raise() { 1 } else { 0 }
}

fn main() -> i64 {
    let once = atomic::Flag(false)
    var a = thread::spawn(tryClaim, once)
    var b = thread::spawn(tryClaim, once)
    var c = thread::spawn(tryClaim, once)
    io::println(a.join() + b.join() + c.join())      // exactly one claimed it
    io::println(once.isRaised())
    io::println(once.lower())
    0
}
```
