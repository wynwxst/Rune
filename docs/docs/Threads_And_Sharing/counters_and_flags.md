# Counters and flags

A `Mutex` is the general answer, and a heavy one. For a counter or a flag there is a cheaper one: `std::atomic` wraps a single number in operations the machine performs in one indivisible step, so no two threads can lose an update between them and none of them ever waits.

**Four threads, one counter, no lock**

```rune
import std::io
import std::atomic
import std::thread

fn bump(counter: atomic::Counter) -> i64 {
    var i = 0
    while i < 100000 { counter.increment(); i += 1 }
    0
}

/// `raise` sets the flag and says whether *this* call was the one that did,
/// so exactly one caller out of any number gets `true`.
fn tryClaim(flag: atomic::Flag) -> i64 {
    if flag.raise() { return 1 }
    0
}

fn main() -> i64 {
    let served = atomic::Counter(0)
    var a = thread::spawn(bump, served)
    var b = thread::spawn(bump, served)
    var c = thread::spawn(bump, served)
    var d = thread::spawn(bump, served)
    a.join(); b.join(); c.join(); d.join()
    println!("{}", served.load())

    let once = atomic::Flag(false)
    var w = thread::spawn(tryClaim, once)
    var x = thread::spawn(tryClaim, once)
    var y = thread::spawn(tryClaim, once)
    println!("winners: {}", w.join() + x.join() + y.join())
    0
}
```

| Method | Does |
| --- | --- |
| `load()` / `store(v)` | read, write |
| `add(d)` / `sub(d)` | and hand back the value *before* |
| `increment()` / `decrement()` / `next()` | by one |
| `exchange(v)` | replace, handing back what was there |
| `compareExchange(was, want)` | store `want` only while the value is still `was` |
| `update(f)` | apply `f`, retrying until it sticks |
| `raiseTo(n)` | keep the larger of the two |
| `Flag::raise()` | set it, and say whether this call did |
| `Flag::lower()` / `isRaised()` | clear it, read it |

Returning the value from *before* is what makes `add` a hand-out rather than a count: every caller gets a number nobody else got. `compareExchange` is how anything more involved is built — read, work out the new value, and swap it in only if nobody moved it meanwhile; `update` is that loop, written once.

> [!WARNING]
> **One number at a time**
>
> Two atomic operations are still two moments. A counter is safe because it *is* one number; code that has to change two things together needs a `Mutex`, whatever the pieces are made of.

The cost is the point. Four threads doing 400,000 increments each, the same work both ways:

```sh
atomic: 1600000 = 27ms
mutex:  1600000 = 120ms
```
