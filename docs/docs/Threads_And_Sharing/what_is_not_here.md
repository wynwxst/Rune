# What is not here

| Missing | Instead |
| --- | --- |
| `async` / `await` | a thread and a join, or a channel |
| a closure as a thread entry | a top-level `fn` and an argument |
| a bounded channel | `Channel` grows; `send` never blocks |
| `select` over several channels | one channel, or a thread for each |
| read/write locks, semaphores | `Mutex` only |
| thread-local storage | nothing; a `global var` is shared, and reaching one from two threads is a race the compiler does not yet catch |

> [!WARNING]
> **Globals are not checked**
>
> That last row is the sharp edge worth knowing: `Send` and `Sync` check what *crosses*, and a `global var` crosses nothing — it is simply already there. Keep globals immutable in a program that starts threads.
