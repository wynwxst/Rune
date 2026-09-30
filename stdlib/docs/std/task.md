# std::task

One thread doing several things at once. Where `std::thread` runs things *at
the same time* and checks what may cross between them, `std::task` keeps
several things *in progress* on one thread, each waiting its turn — so they
may share a class, a `Vector`, a closure, with no `Send`, no `Sync` and no
lock.

The unit is a `Future<T>`: some work that will produce a `T`. Calling an
`async fn` makes one and starts it; `.await` collects the result, parking the
task that asked until it is there and letting the others run meanwhile.

## `async fn` and `.await`

Both tasks start at their call. The shorter sleep finishes first, and `main`
runs between them without waiting for either.

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
    let a = step("a", 20)
    let b = step("b", 5)
    io::println("main between")
    io::println(a.await + " " + b.await)
    0
}
```

`.await` is only allowed inside an `async fn`, an `async ||` closure or an
`async { }` block, because it parks the task it is in and needs one. From
ordinary code, `wait()` — or `task::run` — blocks the thread until the future
is done, running every other task meanwhile. An `async fn main` is `main`
written that way for you.

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

A future may be awaited more than once; each asking gets the value again.

## What a task is

A task is a stack of its own. `.await` on something that is not finished
saves that stack and switches to another; whatever finishes it switches back.
So everything a function can do, an `async fn` can do — `defer`, `?`, loops,
recursion through `.await` — and nothing is rewritten to suspend.

```rune
import std::io
import std::task

struct Failed { why: String }

async fn parse(text: String) -> Result<i64, Failed> {
    task::yieldNow()
    match text.$toInt() {
        Some(n) => Ok(n),
        None => Err(Failed { why: "not a number: " + text }),
    }
}

async fn total(a: String, b: String) -> Result<i64, Failed> {
    defer io::println("total leaves")
    let x = parse(a).await?
    let y = parse(b).await?
    Ok(x + y)
}

async fn main() -> i64 {
    match total("20", "22").await {
        Ok(v) => io::println(v),
        Err(e) => io::println(e.why),
    }
    match total("20", "twenty").await {
        Ok(v) => io::println(v),
        Err(e) => io::println(e.why),
    }
    0
}
```

What the stack costs is `stackSize()` bytes of address space, reserved rather
than used: a task pays for the pages it touches. `setStackSize` changes it for
tasks started afterwards.

## What a task may take

The body runs after the call that started it has returned, so every parameter
is captured into the task by value. A shared borrow of a class — `&Counter`
— is the handle itself, kept alive by the capture, and is fine; a `&var` of
anything, or a `&` of a value type, would point at a slot the caller may have
left, and is refused. The same goes for `self`: an `async` method takes `self`
by value, or belongs to a class.

## Sharing between tasks

Tasks on one thread take turns, never overlap, so they share a class with no
lock at all.

```rune
import std::io
import std::task

class Counter {
    var hits: i64
    fn init(self) { self.hits = 0 }
    fn bump(&var self) { self.hits += 1 }
}

async fn touch(c: Counter, times: i64) {
    var i = 0
    while i < times {
        c.bump()
        task::yieldNow()          // let the other task have a turn
        i += 1
    }
}

async fn main() -> i64 {
    let c = Counter()
    let first = touch(c, 3)
    let second = touch(c, 3)
    first.await
    second.await
    io::println(c.hits)
    0
}
```

Under `--memory zombie` a value has one owner, so tasks share by borrowing —
`touch(c: &Counter, ...)` — and change what they share through
`mem::Checked<T>`, exactly as two borrows anywhere else would.

## Spawning, blocks and closures

`spawn` starts a closure as a task; an `async { }` block is the same thing
written in place, and an `async ||` closure makes a task each time it is
called.

```rune
import std::io
import std::task

fn main() -> i64 {
    let work = task::spawn(||() -> i64 { 3 + 4 })
    io::println(work.wait())

    let base = 5
    let block = async { base + 3 }
    io::println(block.wait())

    let scale = async ||(x: i64) -> i64 { x * 5 }
    io::println(scale(2).wait())
    0
}
```

## Waiting for several

`all` takes a `Vector` of futures and is done when every one is, with the
results in order.

```rune
import std::io
import std::task
import std::time
import std::collections::vector

async fn fetch(id: i64) -> String {
    task::sleep(time::milliseconds(3 - id)).await
    "item " + id.$str()
}

async fn main() -> i64 {
    let results = task::all(vec![fetch(1), fetch(2), fetch(3)]).await
    for r in results { io::println(r) }
    0
}
```

## The first to finish

`first` is done when any one of its futures is, and says which: the index,
and the future itself — already done, so awaiting it returns at once. It
cancels nothing: the others keep running. `race` is `first` with only the
value, and the rest cancelled and waited for, so nothing of it is left
running when the value comes back.

```rune
import std::io
import std::task
import std::time
import std::collections::vector

async fn mirror(name: String, delay: i64) -> String {
    task::sleep(time::milliseconds(delay)).await
    "from " + name
}

async fn main() -> i64 {
    let slow = mirror("slow", 30)
    let fast = mirror("fast", 5)
    let (which, winner) = task::first(vec![slow, fast]).await
    io::println(which)
    io::println(winner.await)
    io::println(slow.await)            // the loser, collected later
    io::println(task::race(vec![mirror("a", 20), mirror("b", 3)]).await)
    0
}
```

A `Future` is a handle: `$clone()` — and so a read out of a
`Vector<Future<T>>` — shares the task rather than copying what is behind
it, under either memory model.

## Cancelling

`cancel` asks a task to stop. At its next suspension point — an `.await`, a
`sleep`, a `yieldNow`, a `checkpoint` — the task leaves its body the way `?`
leaves a function: its `defer`s run, what it holds is released, and it ends
without a result. `outcome()` waits and says which happened; `.await` on a
cancelled task is a panic. `race` cancels its losers; `timeout` cancels what
took too long.

```rune
import std::io
import std::task
import std::time

async fn slow(name: String, ms: i64) -> String {
    defer io::println(name + " leaves")
    task::sleep(time::milliseconds(ms)).await
    io::println(name + " finished")
    name.$clone()
}

async fn count(steps: i64) -> i64 {
    var i = 0
    while i < steps {
        task::checkpoint()            // a loop with no await notices here
        task::yieldNow()
        i += 1
    }
    i
}

async fn main() -> i64 {
    let t = slow("tortoise", 40)
    task::sleep(time::milliseconds(5)).await
    t.cancel()
    match t.outcome() {
        task::Outcome::Done(v) => io::println("done " + v),
        task::Outcome::Cancelled => io::println("cancelled"),
    }
    match task::timeout(time::milliseconds(10), slow("late", 60)).await {
        Some(v) => io::println(v),
        None => io::println("too slow"),
    }
    let c = count(1000000)
    task::yieldNow()
    c.cancel()
    match c.outcome() {                   // waits for it to leave
        task::Outcome::Done(n) => io::println(n),
        task::Outcome::Cancelled => io::println("stopped"),
    }
    0
}
```

Cancellation is cooperative and not recursive: a task that never suspends is
never interrupted, and cancelling a task leaves the tasks it started running.

## Completing one by hand

`pending` makes a future with no task behind it, for a result that some other
code on this thread will hand over with `complete` — how a callback becomes
something awaitable.

```rune
import std::io
import std::task

async fn main() -> i64 {
    let answer = task::pending<i64>()
    let doubled = async { answer.await * 2 }
    io::println(doubled.isDone())
    answer.complete(21)
    io::println(doubled.await)
    0
}
```

## Work on other threads

Anything that would block — a slow read, a long computation — would stop
every task on the thread if it ran there. `blocking` runs a `fn` on a worker
thread and hands back a future that is done when it returns; `offload` does
the same for a closure, `offloadAsync` for an `async` closure, whose task then
runs on the worker's own executor. The workers are a pool of at most
`workers()` threads, started as needed and kept.

The rule is `thread::spawn`'s: what crosses must be `Send` — the argument and
result for `blocking`, and every capture of a closure, which is checked where
the closure is written, so it has to be written in the call.

```rune
import std::io
import std::task
import std::time

fn slowSquare(n: i64) -> i64 {
    var i = 0
    var noise = 0
    while i < 100000 { noise += (n * n) % 7; i += 1 }
    n * n
}

async fn compute(n: i64) -> i64 {
    task::sleep(time::milliseconds(1)).await
    n * 2
}

async fn main() -> i64 {
    let a = task::blocking(slowSquare, 12)
    let b = task::blocking(slowSquare, 13)
    io::println(a.await + b.await)

    let base = 100
    io::println(task::offload(||() -> i64 { base + 1 }).await)

    let total = task::offloadAsync(async ||() -> i64 {
        let x = compute(1)                // tasks on the worker thread
        let y = compute(2)
        x.await + y.await
    })
    io::println(total.await)
    0
}
```

## Sockets

`net::AsyncListener` and `net::AsyncStream` are sockets driven by tasks:
`accept`, `read` and `write` park the task on the socket until it is ready,
so one thread serves many connections. See `std::net`.

## When nothing will ever finish

Waiting on a future that nothing can complete — no task ready, no timer
pending, no thread working — is reported as a deadlock rather than hung. A
task that is parked forever keeps its stack and whatever it holds until the
program ends, and the exit report says so.

## On WebAssembly

A task needs a stack of its own to park on, and WebAssembly's stack is not
memory a program can point at. Built with `--target wasm-threads`, each task
runs on a thread of its own, one at a time, and everything here works. Built
with `--target wasm`, there is only the one stack: a task runs on it from
start to finish when it is started, and one that has to wait for something
unfinished panics, saying so.

## Reference

| Name | Signature | Does |
|------|-----------|------|
| `Future<T>` | `class` | work that will produce a `T` |
| `Future::await` | `(&self) -> T` | the result, parking the task until it is there; written `f.await` |
| `Future::wait` | `(&self) -> T` | the same from code that is not `async`: blocks the thread, running the other tasks |
| `Future::isDone` | `(&self) -> bool` | whether the result is there |
| `Future::complete` | `(&var self, value: T)` | hands a `pending` future its value |
| `spawn` | `<T>(body: @function() -> T) -> Future<T>` | starts `body` as a task |
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
| `checkpoint` | `()` | leaves the task here if it has been cancelled |
| `offload` | `<R: Send>(body: @function() -> R) -> Future<R>` | a closure on a worker thread; its captures must be `Send` |
| `offloadAsync` | `<R: Send>(body: @function() -> Future<R>) -> Future<R>` | an `async` closure, run on a worker's executor |
| `workers` | `() -> i64` | how many worker threads the pool may run |
| `readable` / `writable` | `(descriptor: i64) -> Future<()>` | done when a socket is ready |
| `Future::clone` | `(&self) -> Self` | another handle to the same task; what `$clone()` does |
| `run` | `<T>(future: Future<T>) -> T` | `wait()`, spelled for a `main` |
| `yieldNow` | `()` | let every ready task run first |
| `inTask` | `() -> bool` | inside a task, or on the thread's own stack |
| `stackSize` / `setStackSize` | `() -> i64` / `(bytes: i64)` | the stack each new task is given |
