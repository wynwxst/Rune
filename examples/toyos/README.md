# toyos

A small operating system kernel in Rune, for 32-bit x86. QEMU boots it
straight from the ELF file with multiboot; it looks at what the loader handed
it, runs a handful of checks of the machine and of itself, and switches the
machine off.

```sh
rune run                        # build for bare-x86, boot under QEMU
rune run -- -append panic       # trip a bounds check on purpose
rune run --release
```

Needs `qemu-system-i386`, and either clang and ld.lld (the default) or a GNU
cross toolchain — `rune run --target i686-elf` builds with `i686-elf-gcc` and
`i686-elf-ld` instead (on macOS: `brew install i686-elf-gcc
i686-elf-binutils`). Both are tables in `Rune.toml`; change either to suit
your system. Everything the kernel prints goes
to the VGA screen and to the first serial port, which QEMU connects to your
terminal:

```
toyos 0.1 - a kernel written in Rune

[ ok ] booted by a multiboot loader
       memory: 639 KB low, 126 MB above 1 MB
[ ok ] 64-bit division on a 32-bit processor
[ ok ] signed 64-bit division rounds toward zero
[ ok ] unsigned 64-bit division
[ ok ] bounds-checked indexing
[ ok ] wrapping arithmetic where it is asked for
[ ok ] two objects on the kernel heap
[ ok ] both freed when their owner went away
[ ok ] freed space is reused
       spawning three processes
       pid 3 logger finished
       pid 1 init finished
       pid 2 shell finished
[ ok ] round-robin: 10 units of work in 3 turns of 2
       reaped pid 3 (logger)
       reaped pid 2 (shell)
       reaped pid 1 (init)
[ ok ] every process reaped, every byte back
       heap: 7 allocations, 7 frees, 288 bytes carved

all checks passed; switching off
```

QEMU exits 0 when every check passes, 5 when one fails, and 3 when the kernel
panics:

```
command line says panic: indexing past the end of an array

KERNEL PANIC: index 4 is out of bounds for a collection of length 4
  at main.rune:53:34
```

## What is where

| File | Is |
| --- | --- |
| `Rune.toml` | `runtime = "none"`, `entry = "none"`, the `bare-x86` target, and QEMU as its runner |
| `kernel.ld` | where each section goes: the image at 1 MB, the multiboot header first |
| `boot/boot.s` | the multiboot header, a 64 KB stack, and the call to `kernel_main` — the one part that cannot be Rune, because until it runs there is no stack |
| `src/main.rune` | `@runtime(none)`, `@entry(none)`, `kernel_main`, the checks, and the `@panicHandler` |
| `src/console.rune` | the VGA text screen and the COM1 serial port |
| `src/cpu.rune` | I/O ports through `std::asm`, halting, switching off |
| `src/heap.rune` | a first-fit allocator, the kernel's `@allocator` and `@deallocator` |
| `src/process.rune` | processes and a round-robin scheduler, as classes |
| `src/multiboot.rune` | what the boot loader left in EBX |

## What it shows

- **The whole checking dial, on bare metal.** It is built with
  `safety = "full"`: every index is bounds-checked, every `+` is
  overflow-checked, and the Zombie borrow checker proves every borrow. A
  failed check goes to `panicked` in `main.rune`, which is the kernel's
  `@panicHandler`.
- **A heap of its own.** Every class the kernel makes comes from
  `heap.rune`'s allocator. Under single ownership nothing is counted: a
  process is owned by the one before it, the scheduler owns the first, and
  when the scheduler goes out of scope each process's `deinit` runs and its
  memory goes back — which the checks confirm, to the byte.
- **The standard library, where it is plain Rune.** `Unique<T>`,
  `Option::take`, `std::asm` — compiled in where used, as any program would.
- **64-bit numbers on a 32-bit machine.** Every `/` and `%` of an `i64` is a
  call to `__divdi3` and friends, which the freestanding runtime provides in
  Rune.
- **Globals that need setting.** `console.rune`'s colour starts at 15, not 0,
  so it is set by `rune_init` — which `kernel_main` calls first, since there
  is no `main` to do it.

See the reference's *Bare metal* section for the language side, and the
toolchain book's *Bare metal* chapter for how it is built.
