# Two questions

Every value that reaches a thread is asked one of two things, the pair Rust and Swift settled on:

| Mark | Asks |
| --- | --- |
| `Send` | may a value of this type *move* to another thread? |
| `Sync` | may one be *reached from* several at once? |

Neither is declared. The compiler reads both off the type, the way it reads whether a type is reference counted, so they stay true as a program changes instead of drifting out of date. A struct of numbers is `Send` because of what it is; adding a class field takes it away, at the point of the change rather than later.

| Type | Send | Sync | Why |
| --- | --- | --- | --- |
| `i64`, `f64`, `bool`, `Character` | yes | yes | there is nothing to share |
| `String` | yes | yes | counted, but its contents never change and the count is atomic |
| struct, tuple, array, enum | if its parts are | if its parts are | an aggregate is whatever it holds |
| class | no | no | a reference to something any holder can write to |
| closure | no | no | carries the values it captured |
| `*T`, `Any`, `dyn Mark` | no | no | the compiler cannot see what they reach |
| `thread::Mutex<T>` | yes | yes | it synchronises its own access, and says so |

> [!WARNING]
> **Why a class is neither**
>
> A class is a *shared, mutable* reference: `let` fixes the binding, not the object, so any holder can write to its fields. That is why no ordinary class is `Send`, and why `Mutex` is not a convenience but the way.
