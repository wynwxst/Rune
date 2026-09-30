# Cancellation and timeouts

A task can be asked to stop. `cancel` marks it; at its next *suspension point* — an `.await`, a `sleep`, a `yieldNow`, a `checkpoint` — the task leaves its body the way `?` leaves a function: from that line, running its `defer`s and releasing what it holds on the way out, and it ends without a result. A task parked at one of those points is woken to leave at once; one that is running leaves when it next reaches one. Nothing is interrupted mid-statement, which is what makes a cancelled task safe to reason about: every invariant it keeps between suspension points still holds.

**A task cancelled at its sleep**

```rune
import std::io
import std::task
import std::time

async fn slow(name: String, ms: i64) -> String {
    defer io::println(name + " leaves")
    task::sleep(time::milliseconds(ms)).await
    io::println(name + " finished")
    name.$clone()
}

async fn main() -> i64 {
    let t = slow("tortoise", 40)
    task::sleep(time::milliseconds(5)).await
    t.cancel()
    match t.outcome() {
        task::Outcome::Done(v) => io::println("done " + v),
        task::Outcome::Cancelled => io::println("cancelled, done=" + t.isDone().$str()),
    }
    0
}
```

The tortoise left at its `sleep`: its `defer` ran, and "finished" never printed. A cancelled task has no result, so `.await` on it is a panic; `outcome()` waits like `.await` and says which of the two happened. `isCancelled()` says whether a cancel has been asked for, whether or not the task has reached the point where it leaves; a task that never suspends again finishes with its value regardless.

| Call | Does |
| --- | --- |
| `future.cancel()` | asks the task to stop at its next suspension point; a timer, a socket wait or a `pending` future simply ends |
| `future.outcome()` | waits, then `Done(value)` or `Cancelled` |
| `future.isCancelled()` | whether a cancel has been asked for |
| `task::checkpoint()` | leaves the task here if it has been cancelled — for a loop with no `.await` in it |
| `task::race(futures).await` | the first value; the rest are cancelled the moment it arrives, and have left before it is handed back |
| `task::timeout(limit, future).await` | `Some(value)` within `limit`, or `nil` — the task behind it cancelled, and gone |

**A deadline**

```rune
import std::io
import std::task
import std::time

async fn fetch(ms: i64) -> String {
    task::sleep(time::milliseconds(ms)).await
    "page"
}

async fn main() -> i64 {
    match task::timeout(time::milliseconds(10), fetch(60)).await {
        Some(page) => io::println(page),
        None => io::println("too slow"),
    }
    match task::timeout(time::milliseconds(60), fetch(5)).await {
        Some(page) => io::println(page),
        None => io::println("too slow"),
    }
    0
}
```

> [!WARNING]
> **What cancel cannot do**
>
> Cancellation is cooperative and not recursive. A task that computes without ever suspending is never interrupted — put a `checkpoint()` in its loop — and cancelling a task does not cancel the tasks it started; cancel those too if they should stop. A cancel reaches `wait()` only at the next `.await` after it: `wait()` is for code that is not `async`, and is not a suspension point.
