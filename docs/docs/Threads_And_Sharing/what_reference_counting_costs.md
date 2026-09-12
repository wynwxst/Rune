# What reference counting costs

A count two threads may touch has to be changed in one indivisible step. Doing that to *every* count would be the simple answer, and it is what Swift does — but measured on a loop that does little besides retain and release, it costs about twice what an ordinary add does.

So the compiler picks from the static type instead. `String`, `Arc` and anything marked `@sync` — the types that can actually be reached from two threads — use an atomic pair; everything else uses a plain one. Nothing branches at run time, and a class that cannot be shared pays nothing for the fact that some other type can.

| Counted with | Which types |
| --- | --- |
| an ordinary add | classes, closures, `Any`, `dyn Mark` — none of which is `Send` |
| an atomic add | `String`, `Arc<T>`, `Mutex<T>`, and any `@sync` type |

The weak-reference table is the one piece of runtime state every thread shares, and it has a lock of its own. Everything else the runtime keeps is per-object.
