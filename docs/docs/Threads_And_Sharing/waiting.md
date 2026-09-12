# Waiting

`std::time` holds a length of time as a count of nanoseconds. A duration is built by naming its unit, because a bare number is how the wrong unit gets passed.

**Durations and a clock**

```rune
import std::io
import std::thread
import std::time

fn main() -> i64 {
    let d = time::milliseconds(1500)
    println!("{} is {} ms, or {} whole seconds", d, d.asMilliseconds(),
             d.asSeconds())
    println!("{} {} {}", time::seconds(2), time::minutes(3),
             time::microseconds(5))
    println!("{}", time::milliseconds(250) + time::milliseconds(750))

    // A clock that only goes forwards, for measuring how long something took.
    let start = time::now()
    thread::sleep(time::milliseconds(50))
    println!("waited at least 50ms: {}", start.elapsed().asMilliseconds() >= 50)
    0
}
```

`sleep` stops the thread for *at least* that long — the operating system decides when to wake it, and it will not be early. An `Instant` from `time::now()` means nothing on its own; what it is for is `since` and `elapsed`.
