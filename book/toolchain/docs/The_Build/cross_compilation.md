# Cross compilation

## The idea

`runec` links by calling a C toolchain driver. A cross build names its own —
`x86_64-w64-mingw32-gcc` — and that driver already knows its target's libc,
startup files and linker. Nothing in the Rune toolchain has to be taught a
platform.

## Configuring one

```toml
[target.mingw]
triple  = "x86_64-w64-mingw32"
cc      = "x86_64-w64-mingw32-gcc"
runner  = "wine"
link    = ["ws2_32"]
```

| Key | Means |
| --- | --- |
| `triple` | Passed to `runec --target` |
| `cc` | The link driver |
| `ar` | The archiver; derived from `cc` when absent |
| `sysroot` | Passed as `--sysroot` |
| `runner` | How to launch a built binary here — `wine`, `qemu-aarch64` |
| `link`, `link-paths`, `link-args` | Native libraries this *target* needs |

`link` on a target is for libraries the *build* needs rather than the package:
the runtime uses sockets, so a Windows target needs `ws2_32` even though no
Rune source mentions it.

Then `rune build --target mingw`, into `target/mingw/<profile>/`. Without a
`[target.<name>]` section, `--target` takes a bare triple — enough for a target
that needs no toolchain configuration beyond the triple.

## The runtime, per target

`libruneruntime.a` is C plus a Rune object, and both have to be built for the
target before anything can link. `ensureRuntimeFor` does that on demand and
caches the result under `~/.rune/runtime/<triple>/`, which is why the first
cross build of a session prints `Preparing runtime for …` and later ones do
not.

## Two places the target matters inside the compiler

**Pointer width.** `TypeContext` is constructed with the pointer width taken
from the LLVM data layout for the target triple, so `usize` and `isize` are
whatever a pointer is on the machine the code is *for*. The data layout is the
authority, so the type system and the emitted code cannot disagree.

**Object format.** COFF does not mean what ELF means by weak symbols. Both
`setMergeableLinkage` and `setDiscardableLinkage` attach a COMDAT on COFF —
"keep one, discard the rest" said in the form PE understands. A `weak_odr`
definition on COFF becomes a *weak external*, which another object's reference
does not resolve against.

There is also `abiRejectsByValue`: Win64 passes an aggregate in a register only
when it is exactly 1, 2, 4 or 8 bytes wide. Rather than pass something C will
misread, the compiler says so.

## Testing a cross build

If you have `wine` and mingw-w64, both halves are testable on macOS or Linux:

```sh
rune build -C examples/project/ffi --target mingw
wine target/mingw/debug/app.exe
```

Worth doing after any change to linkage or symbol naming — the COFF path is
easy to break without noticing, because everything still works on the host.
