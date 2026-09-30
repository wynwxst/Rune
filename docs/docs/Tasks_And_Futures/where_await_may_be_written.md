# Where `.await` may be written

`.await` parks the task it is in, so it needs one: it is allowed directly inside an `async fn`, an `async ||` closure or an `async { }` block, and nowhere else — not in a plain closure written inside one, which is a function of its own and may be called from anywhere.

**Not from ordinary code**

```rune
import std::task

async fn answer() -> i64 { 42 }

fn main() -> i64 {
    let n = answer().await
    n - 42
}
```

From ordinary code the bridge is `wait()`, which blocks the thread until the future is done — running every other task meanwhile — or `task::run`, the same thing spelled for a `main`. An `async fn main` is that, written for you: the task runs to the end and its result is the exit code.

**`wait()` and `run`**

```rune
import std::io
import std::task

async fn answer() -> i64 { 6 * 7 }

fn main() -> i64 {
    io::println(answer().wait())
    io::println(task::run(answer()))
    0
}
```

A future may be awaited more than once; each asking gets the value again. Inside a task `wait()` does exactly what `.await` does — parks the task — so calling a function that waits is never a thread blocked by mistake. The keyword is there for the reader and the compiler: it marks where a task can be set aside.

Postfix, and bare, because it chains: `fetch(url).await?` reads the result and then propagates its error, and `client.get(id).await.length()` needs no parentheses.
