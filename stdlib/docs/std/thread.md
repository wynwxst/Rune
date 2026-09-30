# std::thread

Threads, and the rule that keeps them safe: nothing crosses a thread boundary
unless the compiler can see that letting it cross is safe. `Send` and `Sync`
are read off a type — scalars, `String`, and any struct or tuple of them are
`Send`; a class is not, unless it synchronises itself, as `Mutex` does.

## Spawning and joining

`spawn` takes a plain function (a `@cfunction`, not a closure) and one
argument, and hands back a `Handle` to `join` for the result.

```rune
import std::io
import std::thread

fn sumTo(upTo: i64) -> i64 {
    var total = 0
    var i = 1
    while i <= upTo { total += i; i += 1 }
    total
}

fn shout(name: String) -> String { name + "!" }

fn main() -> i64 {
    var a = thread::spawn(sumTo, 100000)
    var b = thread::spawn(sumTo, 100000)
    io::println(a.join() + b.join())
    var s = thread::spawn(shout, "hello")
    io::println(s.join())
    io::println(thread::hardwareThreads() >= 1)
    0
}
```

## Sharing: `Mutex` and `Arc`

A `Mutex<T>` is reachable only while locked, so it is `Sync` whatever `T`
is. An `Arc<T>` is one value several threads may read.

```rune
import std::io
import std::thread

fn addOne(n: i64) -> i64 { n + 1 }

fn bump(shared: thread::Mutex<i64>) -> i64 {
    var i = 0
    while i < 1000 { shared.withLock(addOne); i += 1 }
    0
}

fn readTable(shared: thread::Arc<[3:i64]>) -> i64 {
    var total = 0
    for v in shared.get() { total += v }
    total
}

fn main() -> i64 {
    let total = thread::Mutex<i64>(0)
    var w = thread::spawn(bump, total)
    var x = thread::spawn(bump, total)
    w.join(); x.join()
    io::println(total.get())

    let table = thread::Arc<[3:i64]>([2, 3, 5])
    var r = thread::spawn(readTable, table)
    io::println(r.join())
    0
}
```

## Channels

A `Channel<T>` is a queue between threads. `receive` blocks until a value or
the close; `tryReceive` does not.

```rune
import std::io
import std::thread

fn consume(line: thread::Channel<i64>) -> i64 {
    var total = 0
    while line.receive() is Some(v) { total += v }
    total
}

fn main() -> i64 {
    let line = thread::Channel<i64>()
    var worker = thread::spawn(consume, line)
    for i in 1..=100 { line.send(i) }
    line.close()
    io::println(worker.join())
    0
}
```

## On WebAssembly

Built with `--target wasm-threads`, threads are WASI threads and everything
here works. Built with `--target wasm`, the module has no threads to start:
`spawn` panics with "cannot start a thread". WASI has no way to ask how many
cores there are, so `hardwareThreads` is 1 under either.
