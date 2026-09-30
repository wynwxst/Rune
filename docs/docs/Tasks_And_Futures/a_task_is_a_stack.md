# A task is a stack

Say what the mechanism is first, because everything else follows from it. A task is a piece of code with a stack of its own. `.await` on something that is not finished saves that stack and switches to another; whatever finishes it switches back. That is the whole of it. Nothing is rewritten into a state machine, so everything a function can do, an `async fn` can do — `defer`, `?`, loops, recursion through `.await` — and a task looks in a traceback like what it is: a call, parked.

An `async fn` is an ordinary function whose body runs as a task. Calling it starts the task and hands back a `Future<T>`, the promise of a `T`; `.await` collects the result, parking the task that asked until it is there and letting the others run meanwhile.

**Two tasks, taking turns**

```rune
import std::io
import std::task
import std::time

async fn step(name: String, delay: i64) -> String {
    io::println(name + " starts")
    task::sleep(time::milliseconds(delay)).await
    io::println(name + " ends")
    name + "!"
}

async fn main() -> i64 {
    let a = step("a", 20)          // starts now, runs until its sleep
    let b = step("b", 5)           // so does this
    io::println("main between")
    io::println(a.await + " " + b.await)
    0
}
```

Both tasks start at their call and run until they first have to wait; `main` carries on in between; the shorter sleep finishes first. Nothing here ran at the same time as anything else — the three took turns — which is why `step` could have written to a class the other held with no lock at all.

> [!NOTE]
> **What `async` means**
>
> The rewrite the compiler does is small enough to show. `async fn f(a: A) -> T { body }` becomes `fn f(a: A) -> task::Future<T> { task::spawn(move ||() -> T { body }) }`: the parameters are captured into a closure, and `spawn` starts it on a stack of its own. Generics, libraries and the type system see an ordinary function whose result is a `Future`.
