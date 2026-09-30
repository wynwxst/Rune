# Work on other threads

Anything that would block — a slow read, a long computation — would stop every task on the thread if it ran there. `blocking` runs a `fn` on a worker thread and hands back a future that is done when it returns; `offload` does the same for a closure, and `offloadAsync` for an `async` closure. The workers are a pool of at most `task::workers()` threads — as many as the machine runs at once — started as they are needed and then kept. Each is an ordinary thread with an executor of its own, so a job may start tasks there and wait for them: tasks do run on several threads at once, one executor per thread, and only results cross back.

**A pool of workers**

```rune
import std::io
import std::task
import std::time

fn slowSquare(n: i64) -> i64 {
    var i = 0
    var noise = 0
    while i < 100000 { noise += (n * n) % 7; i += 1 }
    n * n
}

async fn compute(n: i64) -> i64 {
    task::sleep(time::milliseconds(1)).await
    n * 2
}

async fn main() -> i64 {
    let a = task::blocking(slowSquare, 12)
    let b = task::blocking(slowSquare, 13)
    io::println(a.await + b.await)

    let base = 100
    io::println(task::offload(||() -> i64 { base + 1 }).await)

    // The async closure's tasks run on the worker's executor.
    let total = task::offloadAsync(async ||() -> i64 {
        let x = compute(1)
        let y = compute(2)
        x.await + y.await
    })
    io::println(total.await)
    0
}
```

The rule is `thread::spawn`'s, because these are threads: what crosses has to be `Send`. For `blocking` that is the argument and the result. For a closure it is everything it captured — and a closure's *type* says nothing about its captures, so the check is made on the closure as written, at the call, which is why one has to be written there:

**A capture that may not cross**

```rune
import std::task

class Counter { var n: i64
    fn init(self) { self.n = 0 } }

fn main() -> i64 {
    let c = Counter()
    task::offload(||() -> i64 { c.n + 1 }).wait()
}
```
