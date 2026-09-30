# Cross compilation

## The idea

`runec` links by calling a C toolchain driver. A cross build names its own —
`x86_64-w64-mingw32-gcc`, `wasm32-wasip1-clang` — and that driver already
knows its target's libc, startup files and linker. Nothing in the Rune
toolchain has to be taught a platform's linking rules.

What `rune` *does* know is where those drivers usually live. That knowledge is
in one file, `rune/src/Targets.cpp`, and nowhere else.

## Where a target comes from

`--target <name>` (or `[build] target`) is resolved once, by `resolveTarget`,
into a `ResolvedTarget`: triple, `cc`, `cxx`, `ar`, sysroot, runner, extra C
flags and link inputs. The build carries that one struct through every step,
so no other code asks where a compiler is. The name is looked up in order:

1. **A `[target.<name>]` table.** If it has a `base`, or is named after a
   foreign target and has no `triple`, it starts from that foreign target and
   its own keys win. Otherwise it stands alone and needs a `triple`.
2. **A foreign target**, by name or alias (`wasm`, `wasi`, `wasm32-wasip1`
   are one target). It builds into `target/<its name>/` whichever spelling
   asked for it.
3. **A bare triple** — anything with a dash — used as it is, with the host's
   `cc`.

Anything else fails, with the closest known name suggested.

```toml
[target.wasm]            # the foreign target, with the SDK pointed at
sdk = "../wasi-sdk"

[target.web]             # the same toolchain under another name
base = "wasm"
runner = "wasmer run --dir=."

[target.mingw]           # from scratch
triple  = "x86_64-w64-mingw32"
cc      = "x86_64-w64-mingw32-gcc"
runner  = "wine"
link    = ["ws2_32"]
```

| Key | Means |
| --- | --- |
| `base` | The foreign target to start from |
| `triple` | Passed to `runec --target`; needed unless there is a base |
| `cc` | The link driver, and what compiles the runtime's C |
| `cxx` | The C++ driver; derived from `cc` when absent |
| `ar` | The archiver; derived from `cc` when absent |
| `sysroot` | Passed as `--sysroot` |
| `sdk` | The WASI SDK's directory, for a target based on `wasm` |
| `runner` | How to launch a built binary here — `wine`, `qemu-aarch64` |
| `runtime-dir` | A prebuilt `libruneruntime.a`; skips building one |
| `c-flags` | Added to every C compile for the target, the runtime's included |
| `link`, `link-paths`, `link-args` | Native libraries this *target* needs |

`link` on a target is for libraries the *build* needs rather than the package:
the runtime uses sockets, so a Windows target needs `ws2_32` even though no
Rune source mentions it.

## Foreign targets

`foreignTargets()` is a table. Each entry is a name and aliases, a triple, a
`ToolchainKind` saying how to find its compiler, the runners to try, and the
flags and libraries the runtime needs there:

| Kind | Finds |
| --- | --- |
| `GnuPrefix` | `<prefix>-gcc` on `PATH`; `-g++` and `-ar` beside it |
| `WasiSdk` | `sdk`, `$WASI_SDK_PATH`, `/opt/wasi-sdk`, `/opt/wasi-sdk-*`, `~/.rune/toolchains/wasi-sdk`, `~/wasi-sdk`; then `bin/<triple>-clang`, `bin/llvm-ar`, `share/wasi-sysroot` |
| `Clang` | `clang` and `ld.lld` on `PATH`, told the triple |

`Clang` (host clang and ld.lld, told the target) is the bare-metal targets'
default, and they are also `Freestanding`: built `@runtime(none)` whatever
the sources say. See *Bare metal*.

### Flags belong to tools

A foreign target's flags come in two lists. `CFlags` and `LinkArgs` are the
target's own — what any compiler for it takes (`-ffreestanding`, `-fno-pic`;
`ws2_32` for Windows is a library, not a flag). `ToolchainCFlags` and
`ToolchainLinkArgs` belong to the toolchain `fillFromForeign` found — clang's
`--target=` and `-fuse-ld=lld` — and are used only while it is: a table
that names its own `cc` gets none of the compile ones, and one that names a
`cc` or a `linker` none of the link ones. `default-flags = false` keeps
neither list. The same rule covers `--target=`, which `rune` adds to a C
compile only when no `cc` was named, since that `cc` is the host's.

`linker` (a program, with arguments if it wants them, or `"build-script"`)
and `linker-kind` go to `runec` as `--linker` and `--linker-kind`.
`linkerKindOf` infers the kind from the program's name — `ld`, `*-ld`,
`ld.*`, `ld64*`, `wasm-ld`, `lld` are linkers, anything else a driver — and
`linkerScriptArgs` spells `[build] linker-script` for it: `-T <path>` or
`-Wl,-T,<path>`.

In `runec`, `linkExecutable` puts every flag it adds of its own through one
lambda, `own`. Under `--no-default-link-args` it drops them all, so the line
is the objects, `-o` and exactly what `--link-arg`, `-L` and `-l` said. For a
linker (`--linker-kind ld`) it rewrites them as a linker takes them —
`-Wl,a,b` becomes `a b`, `-rdynamic` becomes `--export-dynamic` — and drops
what only a driver understands: `--target=`, `-fuse-ld`, `-nostdlib`,
`-static`, `-pthread`. Arguments the build passed through are never touched.

A missing toolchain is an error at resolution, before anything compiles, with
the entry's `InstallHint` as the note. A missing runner is not: the target
still builds, and `rune run` says what to install. `rune targets` runs the
same resolution for every entry and prints the answers, which makes it the
first thing to try when a cross build misbehaves.

### Adding one

Append an entry in `foreignTargets()`. If its toolchain is a GNU cross
compiler, that is all: a `ToolPrefix`, a `Runners` list and an `InstallHint`.
A toolchain found some other way is a new `ToolchainKind`, handled in
`fillFromForeign`. Then list it in the reference's *Cross compilation* table
(`docs/reference/content.py`).

## The runtime, per target

`libruneruntime.a` is C plus a Rune object, and both have to be built for the
target before anything can link. `ensureRuntimeFor` does that on demand and
caches the result under `~/.rune/runtime/<triple>/`, which is why the first
cross build of a session prints `Preparing runtime for …` and later ones do
not. It compiles with the target's `cc`, `c-flags` and sysroot, and with
`-fPIC` everywhere but WebAssembly (`wantsPic`).

## Places the target matters inside the compiler

**One target machine.** `createTargetMachine` (CodeGen.cpp) is the only place
an `llvm::TargetMachine` is made; the pointer width, the module's data layout
and the machine code all come from it, so they cannot disagree. It picks the
relocation model and features: PIC everywhere, except WebAssembly, which
`wasm-ld` links statically, and whose `-threads` flavour needs `+atomics` and
`+bulk-memory` before its memory can be shared.

**Pointer width.** `TypeContext` is constructed with the pointer width taken
from that data layout, so `usize` and `isize` are whatever a pointer is on the
machine the code is *for*.

**Object format.** COFF does not mean what ELF means by weak symbols. Both
`setMergeableLinkage` and `setDiscardableLinkage` attach a COMDAT on COFF —
"keep one, discard the rest" said in the form PE understands. A `weak_odr`
definition on COFF becomes a *weak external*, which another object's reference
does not resolve against.

**Structs across the C boundary.** LLVM passes a first-class aggregate
argument by splitting it into its fields, which is what AArch64's C
convention does for a small struct and what x86-64's does not: SysV packs
fields into eightbytes and puts anything over 16 bytes in memory, and Win64
passes anything but 1, 2, 4 or 8 bytes by pointer. So `cSignatureFor` in
`CodeGenCxx.cpp` classifies a C signature with the same `classifyCxxArgument`
the C++ interop uses — in a C mode that accepts a Rune struct, since both
sides of a C call see Rune's layout — whenever a struct, tuple or array
crosses by value. It is used for a foreign `extern "C"` function's
declaration and calls, and for every call through a `@cfunction` value, both
through `emitAbiCall`. A Rune function taken as a `@cfunction` would then be
called the C way while defined the Rune way, so `cAdapterFor` gives it an
internal adapter with the C signature that unpacks its arguments and makes
the Rune call. A signature of nothing but scalars gets none of this and is
unchanged.

What is left for `abiRejectsByValue` is a Rune function `@export`ed to C,
whose definition still takes its arguments as LLVM lowers them: on Win64 a
struct that is not 1, 2, 4 or 8 bytes wide is refused there rather than
misread.

**Macros run here.** A package's `@type(Macros)` files are compiled into a
program that runs during the build, so it is always built for the host — with
the host's runtime, not the `--runtime-dir` a cross build passes.

## WebAssembly

Everything above applies; these are the parts particular to it.

**The entry point.** wasi-libc's start code calls `__main_argc_argv`, which is
what clang renames a C `main(argc, argv)` to on wasm. `emitEntryPoint` emits
that name on a wasm triple.

**Linking.** `linkExecutable` leaves out `-rdynamic` (there is no dynamic
linker to export to), refuses `--shared`, asks for an 8 MB stack
(`wasm-ld` reserves 64 KB otherwise), and adds what WASI leaves out: the SDK's
`-lwasi-emulated-pthread` for plain `wasm32-wasip1`, or `-pthread` with an
imported, exported, 4 GB-ceilinged memory for `-threads`, where every thread is
an instance of its own sharing that memory.

**The C runtime.** `rune_runtime.c` and `rune_task.c` define
`RUNE_SINGLE_THREADED` and `_WASI_EMULATED_PTHREAD` on WASI without
`_REENTRANT` — that is, without `-pthread`, which the `wasm-threads` foreign
target passes through `c-flags`. Then:

| | Plain `wasm` | `wasm-threads` |
| --- | --- | --- |
| Locks, condition variables | wasi-libc's single-threaded stubs | real |
| `rune_thread_start` | fails, so `thread::spawn` panics | real |
| The worker pool | runs each job inline | real |
| Sockets | every call fails with "permission denied" | the same |
| `rune_command_run` | fails | fails |
| Entropy | `arc4random_buf` | the same |

**Tasks.** A task needs a stack of its own, and WebAssembly's stack is not
memory a program can point at. `rune_task.c` has a third implementation of
`fiber_make` / `switch_to` / `fiber_finish` for it:

- With threads, each task is a detached thread and the processor is a
  *baton*: a mutex, a condition variable and a flag per task, plus one for the
  executor's own thread. `switch_to` gives the next baton and takes its own
  back; the first switch into a task starts its thread instead. A task's
  thread sets its thread-local executor to the one that owns it before
  running anything, and `fiber_finish` gives the baton back and returns
  without touching the task again, since the receiver may free it at once.
  The idle loop waits on the executor's condition variable, which a remote
  completion already broadcasts.
- Without threads, the first switch into a task is a call: the task runs on
  the starter's stack and is complete when it returns. Any other switch — a
  task parking, yielding or being resumed — panics with a message that says
  so and names `wasm-threads`.

## Testing a cross build

With the WASI SDK and wasmtime installed, both WebAssembly flavours are
testable on any host, and are the quickest cross check there is:

```sh
rune new hello && cd hello
rune run --target wasm
rune run --target wasm-threads
```

`tests/cases` compiles for `wasm32-wasip1` as it stands; what differs is only
what the table above says cannot work, and the cases whose expected output
names the host (`family == "unix"`, a 64-bit `usize`).

If you have `wine` and mingw-w64, the Windows half is testable on macOS or
Linux:

```sh
rune test -C examples/project/ffi --target windows   # run under wine
```

Worth doing after any change to linkage or symbol naming — the COFF path is
easy to break without noticing, because everything still works on the host.
