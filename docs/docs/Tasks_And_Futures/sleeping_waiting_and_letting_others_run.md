# Sleeping, waiting, and letting others run

| Call | Does |
| --- | --- |
| `task::sleep(duration).await` | parks this task until the time has passed; the thread runs the others |
| `task::yieldNow()` | lets every task that is ready run before this one continues |
| `task::all(futures).await` | every result, in order, once every future in the `Vector` is done |
| `task::first(futures).await` | the first to finish: its index and the future itself, already done. The lowest index when several are |
| `task::race(futures).await` | the value of the first to finish |
| `task::pending<T>()` | a future with no task behind it, which `complete(value)` finishes — how a callback becomes something awaitable |
| `future.isDone()` | whether the result is there |

**`all` and `pending`**

```rune
import std::io
import std::task
import std::time
import std::collections::vector

async fn fetch(id: i64) -> String {
    task::sleep(time::milliseconds(3 - id)).await
    "item " + id.$str()
}

async fn main() -> i64 {
    let results = task::all(vec![fetch(1), fetch(2), fetch(3)]).await
    for r in results { io::println(r) }

    let answer = task::pending<i64>()
    let doubled = async { answer.await * 2 }
    io::println(doubled.isDone())
    answer.complete(21)
    io::println(doubled.await)
    0
}
```

**`first` and `race`**

```rune
import std::io
import std::task
import std::time
import std::collections::vector

async fn mirror(name: String, delay: i64) -> String {
    task::sleep(time::milliseconds(delay)).await
    "from " + name
}

async fn main() -> i64 {
    let slow = mirror("slow", 30)
    let fast = mirror("fast", 5)
    let (which, winner) = task::first(vec![slow, fast]).await
    io::println(which)
    io::println(winner.await)
    // The loser keeps running; nothing cancels it. Here it is collected.
    io::println(slow.await)
    io::println(task::race(vec![mirror("a", 20), mirror("b", 3)]).await)
    0
}
```

`first` hands back the winner rather than only its value, so the caller knows which it was and can await the others later; it cancels nothing. `race` is `first` with the rest cancelled — and waited for, so that when the value comes back nothing of the race is still running. See **Cancellation and timeouts**.
