# Sharing between tasks

Tasks on one thread take turns and never overlap, so the two questions `std::thread` asks — `Send`, `Sync` — are not asked here. Two tasks may hold one class, and both may write to it; a write is finished before the other task gets a turn.

**One class, two tasks, no lock**

```rune
import std::io
import std::task

class Counter {
    var hits: i64
    fn init(self) { self.hits = 0 }
    fn bump(&var self) { self.hits += 1 }
}

async fn touch(c: Counter, times: i64) {
    var i = 0
    while i < times {
        c.bump()
        task::yieldNow()          // let the other task have a turn
        i += 1
    }
}

async fn main() -> i64 {
    let c = Counter()
    let first = touch(c, 3)
    let second = touch(c, 3)
    first.await
    second.await
    io::println(c.hits)
    0
}
```

Under `--memory zombie` a value has one owner, so the tasks share by borrowing — `touch(c: &Counter, ...)` — and change what they share through `mem::Checked<T>`, exactly as two borrows anywhere else would. A future's result is cloned out to each awaiter, so the future keeps its own and frees it exactly once. The future itself is a handle: `$clone()` — and so a read out of a `Vector<Future<T>>`, or a `vec![...]` of them — shares the task rather than copying what is behind it.

> [!NOTE]
> **One thread, one executor**
>
> A task belongs to the thread that made it, and futures do not cross threads: awaiting one from another thread is a panic. Each thread that uses tasks has an executor of its own.
