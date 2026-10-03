# std::thread

| Name | Signature | Does |
| --- | --- | --- |
| `Send` | `mark` | may a value of this type move to another thread? Not declared — read off the type |
| `Sync` | `mark` | may one be reached from several at once? |
| `spawn` | `<A: Send, R: Send>(entry: @cfunction(A) -> R, argument: A) -> Handle<R>` | runs `entry(argument)` on a thread |
| `Handle::join` | `(&var self) -> R` | waits, and hands over what the thread returned — once |
| `scope` | `<E, R>(env: E, body: @cfunction(&Scope<E>) -> R) -> R` | lends `env` to the threads `body` starts, and joins them all |
| `Scope::spawn` / `env` | `(&self, entry, argument: A from self)` / `(&self) -> &E` | a thread over the environment; the environment, borrowed |
| `Mutex<T>` | `class` | shared mutable state, reachable only while locked |
| `Mutex::withLock` | `(&self, body: @cfunction(T) -> T)` | replaces the value with `body(value)`, locked |
| `Mutex::get` / `set` | `(&self) -> T where T: Clone` / `(&self, next: T)` | read a copy and write, locked |
| `Arc<T: Sync>` | `class` | one value several threads may read |
| `Arc::look` / `get` | `(&self) -> &T` / `(&self) -> T where T: Clone` | what is inside, borrowed or copied |
| `Channel<T: Send>` | `class` | a queue between threads; `send`, `receive`, `tryReceive`, `close`, `pending`, all through `&self` |
| `sleep` | `(duration: time::Time)` | stops this thread for at least that long |
| `hardwareThreads` | `() -> i64` | how many run at once; never zero |
| `yieldNow` | `()` | offer the rest of this turn |

*See **Threads and sharing**. Nothing crosses a thread boundary that the compiler cannot vouch for.*
