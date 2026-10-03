# What the freestanding runtime provides

`runetime/freestanding.rune` is compiled into the program for the program's own target, the way Rust builds `core` for the target it compiles for. It depends on nothing.

|  | Provides |
| --- | --- |
| Panics | `rune_panic_bounds`, `_overflow`, `_div_zero`, `_nil`, `_no_match`, `_any`, `_unwrap` and `rune_panic`, each turned into a message for the `@panicHandler` |
| The heap | `rune_alloc` and `rune_drop` over the `@allocator` and `@deallocator`: an object's header, its `deinit`, and `--safety full`'s check that a dropped object had one owner |
| Memory | `memcpy`, `memmove`, `memset`, `memcmp` — LLVM emits calls to them for large copies whatever the target |
| Raw memory | `rune_raw_alloc`, `_realloc`, `_free` and `_is_zero`, which `std::mem` is built on. Each block carries its size in 16 bytes ahead of it, since `@deallocator` is never told one and growing a block has to copy it |
| 32-bit targets | `__divdi3`, `__udivdi3`, `__moddi3` and `__umoddi3`: 64-bit division, which a 32-bit processor does with a library call |
| Classes | `$clone()`, `is` and `Any` checks, hashing |

A library that wants to work either way asks `@Config(runtime == "none")` — `std::process` does, for its `panic`.

> [!NOTE]
> **Why the loops stay loops**
>
> A freestanding program is compiled with `no-builtins`, as C's `-ffreestanding` does, so LLVM never turns a loop that copies bytes into a call to `memcpy` — least of all inside `memcpy`. Every one of these is `@weak`: a program with faster ones keeps its own.
