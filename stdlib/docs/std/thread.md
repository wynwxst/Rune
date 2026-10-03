# std::thread

Threads, and the rule that keeps them safe: nothing crosses a thread boundary
unless the compiler can see that letting it cross is safe. `Send` and `Sync`
are read off a type — scalars, `String`, and any struct or tuple of them are
`Send`; a class is not, unless it synchronises itself, as `Mutex` does.

## Spawning and joining

`spawn` takes a plain function (a `@cfunction`, not a closure) and one
argument, and hands back a `Handle` to `join` for the result. The argument
moves to the thread, and the result moves back to whoever joins it — once: a
second `join` has nothing left to hand over, and aborts.

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

## Sharing: `thread::scope`, `Mutex` and `Arc`

Handing a value to `spawn` hands it over: the thread owns it, and the caller
does not have it any more. To share one, `thread::scope` takes it in, lends it
to every thread the body starts as a `&`, and joins them all before the
value is dropped — so no thread can outlive what it borrowed.

What is shared through a `&` has to be safe to reach from several threads at
once. A `Mutex<T>` is: it is reachable only while locked, so every method takes
`&self` and it is `Sync` whatever `T` is. So are `atomic::Counter` and
`atomic::Flag`. An `Arc<T>` is one value several threads may read.

```rune
import std::io
import std::thread

fn addOne(n: i64) -> i64 { n + 1 }

fn bump(shared: &thread::Mutex<i64>) -> i64 {
    var i = 0
    while i < 1000 { shared.withLock(addOne); i += 1 }
    0
}

fn readTable(shared: &thread::Arc<[3:i64]>) -> i64 {
    var total = 0
    for v in shared.get() { total += v }
    total
}

fn main() -> i64 {
    let total = thread::scope(thread::Mutex<i64>(0),
                              ||(s: &thread::Scope<thread::Mutex<i64>>) -> i64 {
        var w = s.spawn(bump, s.env())
        var x = s.spawn(bump, s.env())
        w.join(); x.join()
        s.env().get()
    })
    io::println(total)

    let sum = thread::scope(thread::Arc<[3:i64]>([2, 3, 5]),
                            ||(s: &thread::Scope<thread::Arc<[3:i64]>>) -> i64 {
        var r = s.spawn(readTable, s.env())
        r.join()
    })
    io::println(sum)
    0
}
```

## Channels

A `Channel<T>` is a queue between threads, and like a `Mutex` it locks for
itself, so the threads of a scope share one through `&`. `receive` blocks
until a value or the close; `tryReceive` does not. A value sent is moved
through the channel to whoever receives it.

```rune
import std::io
import std::thread

fn consume(line: &thread::Channel<i64>) -> i64 {
    var total = 0
    while line.receive() is Some(v) { total += v }
    total
}

fn main() -> i64 {
    let total = thread::scope(thread::Channel<i64>(),
                              ||(s: &thread::Scope<thread::Channel<i64>>) -> i64 {
        var worker = s.spawn(consume, s.env())
        for i in 1..=100 { s.env().send(i) }
        s.env().close()
        worker.join()
    })
    io::println(total)
    0
}
```

## On WebAssembly

Built with `--target wasm-threads`, threads are WASI threads and everything
here works. Built with `--target wasm`, the module has no threads to start:
`spawn` panics with "cannot start a thread". WASI has no way to ask how many
cores there are, so `hardwareThreads` is 1 under either.
