# The standard library

Small on purpose, written in Rune over a C runtime you can read in an
afternoon, and read by every compile whether or not a program imports any of
it — which is why it stays small. Each module below is a page of its own,
with what it is for, a few programs that use it, and then everything public
in it.

`rune doc std::io` opens one from anywhere; `rune doc std` opens this page.

| Module | Is for |
|---|---|
| `std::io` | the console, files, directories, byte buffers and streams |
| `std::text` | Unicode: normalisation, case folding, collation — and the everyday half |
| `std::fmt` | what a format string expands into: `fixed`, `radix`, `pad` |
| `std::option`, `std::result` | the two enums `T?`, `nil`, `??` and `?` are sugar for |
| `std::math` | libm wrapped and named, the constants, and integer answers |
| `std::collections::vector`, `std::collections::slice` | a growable array, and everything an array or slice can do |
| `std::dictionary` | `Map<K, V>` and `Set<T>`, keyed structurally |
| `std::iter` | what `for` drives, and how to reshape it on the way past |
| `std::process` | the program as the OS sees it: arguments, exit, panic |
| `std::env` | environment variables, and where the process is |
| `std::time` | durations, a clock to measure with, and the calendar |
| `std::random` | numbers nobody can predict, and numbers that repeat exactly |
| `std::hash` | FNV-1a, CRC-32 and SHA-256, by name |
| `std::json` | reading and writing JSON |
| `std::cli` | the command line, declared and then asked |
| `std::mem` | layout, an allocator, `Handle<T>` and `Buffer<T>` |
| `std::any` | a value of any type, that knows which |
| `std::reflect` | what the compiler knows about a type, at compile time |
| `std::convert` | `As<Target>`, the mark `into` and `?` dispatch through |
| `std::thread`, `std::atomic` | threads, mutexes, channels; counters and flags |
| `std::net` | TCP, in the shape `io`'s stream marks already describe |
| `std::testing` | what a file under `tests/` reports through |
| `std::asm` | instructions handed to the assembler as written |

Three of them need no import: `Option`, `Result` and their variants, and the
two iterator marks, are in scope everywhere, because the syntax that
dispatches through them cannot need one.
