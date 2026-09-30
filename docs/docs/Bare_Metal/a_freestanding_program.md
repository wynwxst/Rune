# A freestanding program

Two directives at the top of a file — or `runtime` and `entry` under `[build]`, or `--runtime none` and `--entry none` to `runec` — say what the program has around it.

| Directive | Means | Like Rust's |
| --- | --- | --- |
| `@runtime(none)` | no C library and no hosted runtime are linked; `runetime/freestanding.rune` is compiled into the program instead | `#![no_std]` |
| `@entry(none)` | no `main` is generated; an `@export`ed function is where execution starts | `#![no_main]` |

**The shape of a kernel**

```text
@runtime(none)
@entry(none)
import std::asm

@panicHandler
fn panicked(message: CString, location: CString) -> Never {
    // say it somewhere (a serial port, the screen), then stop
    loop { unsafe { asm::run("cli; hlt", "") } }
}

@export("kernel_main")
fn kernelMain(magic: u32, info: u32) -> Never {
    // ...
    loop {}
}
```

The standard library is still there, and still only compiled where it is used, so `T?`, `Result`, `std::asm` and the rest of what is plain Rune work as ever. What reaches the hosted runtime — `String`, `std::io`, threads, tasks, reference counting — is refused at compile time, against the function of yours that reached it:

```sh
● kernel.rune [4:3..8]
4 ║ fn greet() { io::println("hi") }
       ^^^^^ ERROR: 'greet' needs the hosted runtime, and this program is built without one [E0542]
    ─  note: it uses `String`, which lives in the hosted runtime; a freestanding program works in `CString` and byte arrays
```

> [!NOTE]
> **Single ownership**
>
> A freestanding program is built with `--memory zombie`. Every object has one owner and nothing is counted, so the heap needs nothing but an allocator; reference counting would need the hosted runtime's atomics and weak table, and is refused like any other use of it.
