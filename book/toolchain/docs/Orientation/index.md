# Orientation

> This book is about the toolchain itself, not about the language. It is for
> someone who wants to change the compiler: add a keyword, add a type, fix a
> diagnostic, understand why a build does what it does. The language is
> documented separately, in the reference.

Two programs make up the toolchain.

| Binary | Source | What it is |
| --- | --- | --- |
| `runec` | `runec/` | The compiler. Reads `.rune` files, writes an object, a library, or an executable. |
| `rune` | `rune/` | The package manager. Reads `Rune.toml`, works out what to build, and calls `runec`. |

Neither knows much about the other. `rune` drives `runec` through its command
line and nothing else; `runec` has no idea packages exist. That boundary is
deliberate — everything `runec` needs to compile something has to be sayable as
a flag, which is what makes the compiler usable on its own and testable without
a package around it.

Two more directories hold code that ships *with* programs rather than code that
compiles them.

| Directory | What it is |
| --- | --- |
| `runtime/` | The C runtime: allocation, reference counting, panics, tracebacks, the parts of `std::io` and `std::net` that call the operating system. Linked into every program. |
| `runetime/` | The memory core, written in Rune itself: the allocator's Rune-side interface, the weak-reference table, raw memory. |
| `stdlib/` | The standard library, written in Rune. Compiled from source into every program that uses it. |

## Pages

- [The shape of the toolchain](the_shape_of_the_toolchain.md)
- [Building it](building_it.md)
- [Finding your way around](finding_your_way_around.md)
