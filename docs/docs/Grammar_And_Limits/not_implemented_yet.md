# Not implemented yet

Stated plainly, because a reference that hides its edges wastes your time.

| Area | Where it stops |
| --- | --- |
| collections | arrays, slices, `Vector`, `Map` and `Set`; no ordered map, and no persistent collections |
| concurrency | threads, channels, atomics, `Send`, `Sync`, `Arc` and `Mutex` on pthreads platforms; tasks with `async fn` and `.await`, cancellation, timeouts and `task::first` to race them, on one executor thread with blocking work handed to others; a thread entry is a `@cfunction` rather than a closure, and the Windows thread backing still faults when a handle is destroyed and another thread spawned |
| generics | monomorphised, and a narrower `bind` displaces a general one (see **Specialisation**); no higher-kinded parameters and no constant generics |
| marks | associated types, their bounds and `where` clauses are all checked; a mark still cannot require an operator on an associated type of another mark |
| leaks | at `--safety full` a class that can reach itself strongly is refused, so reference cycles cannot be built; the rule reads types rather than objects, so it also refuses shapes that would not have looped, and it cannot see a closure's captures |
| closures | captures are copied into a heap environment when the closure is made; `move` says so explicitly, and a class is how you share one value instead |
| strings | UTF-8 with character indexing; `std::text` does NFC/NFD and a root collation, but not the Unicode Collation Algorithm and not per-locale ordering |
| errors | `Result` and `?` propagate; a panic aborts and cannot be caught — there is no unwinder, and unwinding past a scope would have to release everything it owns |
| debug info | `-g` emits subprograms, line tables, types and local variables, and a traceback on abort; lexical sub-scopes are flattened into the function, so a shadowed name shows the outer one |

> [!NOTE]
> **Direction**
>
> None of these are load-bearing for the language's design — they are unwritten, not blocked.
