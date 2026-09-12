# std::thread

| Name | Signature | Does |
| --- | --- | --- |
| `Send` | `mark` | may a value of this type move to another thread? Not declared — read off the type |
| `Sync` | `mark` | may one be reached from several at once? |
| `spawn` | `<A: Send, R: Send>(entry: @cfunction(A) -> R, argument: A) -> Handle<R>` | runs `entry(argument)` on a thread |
| `Handle::join` | `(&var self) -> R` | waits, and hands back what the thread returned |
| `Mutex<T>` | `class` | shared mutable state, reachable only while locked |
| `Mutex::withLock` | `(&var self, body: @cfunction(T) -> T)` | replaces the value with `body(value)`, locked |
| `Mutex::get` / `set` | `(&self) -> T` / `(&var self, next: T)` | read and write, locked |
| `Arc<T: Sync>` | `class` | one value several threads may read |
| `Arc::get` | `(&self) -> T` | a copy of what is inside |
| `Channel<T: Send>` | `class` | a queue between threads; `send`, `receive`, `tryReceive`, `close`, `pending` |
| `sleep` | `(duration: time::Time)` | stops this thread for at least that long |
| `hardwareThreads` | `() -> i64` | how many run at once; never zero |
| `yieldNow` | `()` | offer the rest of this turn |

*See **Threads and sharing**. Nothing crosses a thread boundary that the compiler cannot vouch for.*
