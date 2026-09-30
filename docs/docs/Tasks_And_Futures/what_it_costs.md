# What it costs

| Thing | Cost |
| --- | --- |
| starting a task | a stack from a per-thread pool, a small box, and two switches — about half a microsecond in all |
| `.await` on something finished | a check |
| `.await` on something not | two switches of a few dozen instructions each |
| a task's stack | `task::stackSize()` bytes of address space (1 MiB unless changed), *reserved* — a task that touches 20 KB of it costs 20 KB |
| a task that never finishes | its stack and whatever it holds, until the program ends; the exit report counts them |

A task that runs off the end of its stack faults rather than writing over whatever lies beyond it; a deep recursion inside a task wants `task::setStackSize` raised first.

Waiting on a future that nothing can finish — no task ready, no timer pending, no thread working — is reported as a deadlock rather than hung:

**Reported, not hung**

```rune
import std::io
import std::task

async fn main() -> i64 {
    let never = task::pending<i64>()
    io::println("waiting")
    never.await
}
```
