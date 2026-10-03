# Channels

A `Channel<T>` is a queue one thread puts values into and another takes them out of. `receive` waits until there is something to take, or until the channel is closed and empty — which is how a consumer knows to stop. The queue grows as needed, so `send` never waits.

**One queue, three consumers**

```rune
import std::io
import std::thread

/// Takes values until the channel is closed and empty.
fn consume(line: &thread::Channel<i64>) -> i64 {
    var total = 0
    while true {
        match line.receive() {
            Some(v) => total += v,
            None => break,
        }
    }
    total
}

fn main() -> i64 {
    // Three consumers on one queue. Each takes what it can, and the three
    // totals add up to the whole.
    let sum = thread::scope(thread::Channel<i64>(),
        ||(s: &thread::Scope<thread::Channel<i64>>) -> i64 {
            var a = s.spawn(consume, s.env())
            var b = s.spawn(consume, s.env())
            var c = s.spawn(consume, s.env())

            let jobs = s.env()
            var i = 1
            while i <= 1000 { jobs.send(i); i += 1 }
            jobs.close()       // no more coming; wake everyone waiting

            a.join() + b.join() + c.join()
        })
    println!("{}", sum)
    0
}
```

| Method | Does |
| --- | --- |
| `send(v)` | puts `v` at the back and wakes a receiver |
| `receive()` | the next value, waiting; `nil` once closed and empty |
| `tryReceive()` | a value if one is already there, never waits |
| `close()` | says no more will be sent, and wakes every waiter |
| `pending()` | how many are waiting to be taken |

> [!NOTE]
> **After close**
>
> Sending to a closed channel is a panic rather than a value that quietly vanishes. Closing twice is harmless: the second says nothing the first did not.
