# std::task

| Name | Signature | Does |
| --- | --- | --- |
| `Future<T>` | `class` | work that will produce a `T`; what calling an `async fn` hands back |
| `Future::await` | `(&self) -> T` | the result, parking the task until it is there — written `f.await` |
| `Future::wait` | `(&self) -> T` | the same from code that is not `async`: blocks the thread, running the other tasks |
| `Future::isDone` | `(&self) -> bool` | whether the result is there |
| `Future::complete` | `(&var self, value: T)` | hands a `pending` future its value |
| `spawn` | `<T>(body: @function() -> T) -> Future<T>` | starts `body` as a task; what `async { }` is |
| `sleep` | `(duration: time::Time) -> Future<()>` | done once the time has passed |
| `pending` | `<T>() -> Future<T>` | a future somebody will `complete` |
| `blocking` | `<A: Send, R: Send>(entry: @cfunction(A) -> R, argument: A) -> Future<R>` | runs `entry` on a thread of its own |
| `all` | `<T>(futures: Vector<Future<T>>) -> Future<Vector<T>>` | every result, in order |
| `first` | `<T>(futures: Vector<Future<T>>) -> Future<(i64, Future<T>)>` | the first to finish: its index, and the future itself |
| `race` | `<T>(futures: Vector<Future<T>>) -> Future<T>` | the value of the first to finish; the rest are cancelled |
| `timeout` | `<T>(limit: time::Time, future: Future<T>) -> Future<T?>` | the value within `limit`, or `nil` and the task cancelled |
| `Future::cancel` | `(&self)` | asks the task to stop at its next suspension point |
| `Future::outcome` | `(&self) -> Outcome<T>` | waits; `Done(value)` or `Cancelled` |
| `Future::isCancelled` | `(&self) -> bool` | whether a cancel has been asked for |
| `Outcome<T>` | `enum` | `Done(T)` or `Cancelled` |
| `checkpoint` | `()` | leaves the task here if cancelled |
| `offload` | `<R: Send>(body: @function() -> R) -> Future<R>` | a closure on a worker thread; captures must be `Send` |
| `offloadAsync` | `<R: Send>(body: @function() -> Future<R>) -> Future<R>` | an `async` closure on a worker thread's executor |
| `workers` | `() -> i64` | how many worker threads the pool may run |
| `readable` / `writable` | `(descriptor: i64) -> Future<()>` | done when the socket is ready |
| `Future::clone` | `(&self) -> Self` | another handle to the same task; what `$clone()` does |
| `run` | `<T>(future: Future<T>) -> T` | `wait()`, spelled for a `main` |
| `yieldNow` | `()` | let every ready task run first |
| `inTask` | `() -> bool` | inside a task, or on the thread's own stack |
| `stackSize` / `setStackSize` | `() -> i64` / `(bytes: i64)` | the stack each new task is given |

*See **Tasks and futures**. Tasks on one thread take turns, so nothing here asks for `Send` — except `blocking`, which is a thread.*
