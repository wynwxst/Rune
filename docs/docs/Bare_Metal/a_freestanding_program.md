# A freestanding program

Two directives at the top of a file — or `runtime` and `entry` under `[build]`, or `--runtime none` and `--entry none` to `runec` — say what the program has around it.

| Directive | Means | Like Rust's |
| --- | --- | --- |
| `#runtime(none)` | no C library and no hosted runtime are linked; `runetime/freestanding.rune` is compiled into the program instead | `#![no_std]` |
| `#entry(none)` | no `main` is generated; an `#export`ed function is where execution starts | `#![no_main]` |

**The shape of a kernel**

```text
#runtime(none)
#entry(none)
import std::asm

#panicHandler
fn panicked(message: CString, location: CString) -> Never {
    // say it somewhere (a serial port, the screen), then stop
    loop { unsafe { asm::run("cli; hlt", "") } }
}

#export("kernel_main")
fn kernelMain(magic: u32, info: u32) -> Never {
    // ...
    loop {}
}
```

The standard library is still there, and still only compiled where it is used, so `T?`, `Result`, `std::asm` and the rest of what is plain Rune work as ever. So do the containers — `Vector`, `Map` and `Set` — which allocate through `std::mem` and so through the program's own `#allocator`; `String`, `std::fmt`, `format!` and printing, which writes through `#output`; and `process::panic`, whose message goes to the `#panicHandler`. What needs an operating system — standard input, files, threads, tasks, the network, `std::text`'s Unicode tables, reference counting — is refused at compile time, against the function of yours that reached it:

```sh
● kernel.rune [4:3..8]
4 ║ fn greet() { let name = io::readLine() }
       ^^^^^ ERROR: 'greet' needs the hosted runtime, and this program is built without one [E0542]
    ─  note: it uses standard input, which needs an operating system to read from
```

Most of the rest of the library is plain Rune too, and comes along: `std::iter`, `std::time`'s arithmetic and calendar, `std::json`, `std::hash`, `std::reflect` and `std::cli`'s parsing among it. What else needs a hosted build is less obvious from a module's name: about half of `std::math` — the trigonometry, logarithms and powers — calls the C maths library; `process::exit`, all of `std::env` and `random`'s entropy need an operating system; `Any::typeName` needs the hosted runtime though the rest of `Any` does not; and `std::cxx`'s `alloc` and `free` need a C++ runtime.

Which is which is not left to finding out: every public function in the standard library's reference carries a badge, and `runec --tiers` prints the same list, worked out by building the whole library as a `#runtime(none)` program would see it — once with each freestanding runtime.

| Badge | `--tiers` says | Means |
| --- | --- | --- |
| **bare metal** | `bare` | works in a `#runtime(none)` program, with either freestanding runtime |
| **bare metal · full runtime** | `full` | works in one with the full freestanding runtime, the default; `freestanding_type = "minimal"` leaves out what it needs |
| **hosted** | `hosted` | needs the hosted runtime; the badge's title and `--tiers` name what of it |

```sh
$ runec --tiers | grep -E "std::(io|fmt)::"
bare    std::io::print
bare    std::io::println
hosted  std::io::readLine  (rune_read_line)
full    std::fmt::fixed  (rune_string_from_f64_fixed)
...
```

A generic is placed by what it does itself: `Option::unwrap` is bare metal, though unwrapping an `Option<TcpStream>` drops a socket — that need is the socket's, and its own badge says so. A mark's requirement, such as `Display::display`, has no badge: it is as bare as the type that answers it. `--tiers --cfg freestanding_type=minimal` answers for the minimal runtime alone.
