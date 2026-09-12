# A target triple

`--target` takes a triple and the compiler emits for it. Nothing else has to change: the object is a real object for that machine, in that machine's format.

```sh
$ runec --target x86_64-w64-mingw32 -c -o hello.o hello.rune
$ file hello.o
hello.o: Intel amd64 COFF object file
```

Linking is the part that needs help. A linker is platform software: it knows one set of startup files, one libc, one executable format. Cross-compiling means naming the one that belongs to the target.

| Flag | Does |
| --- | --- |
| `--target <triple>` | what to emit for |
| `--cc <program>` | the toolchain driver that links |
| `--sysroot <dir>` | where that target's headers and libraries are |
| `--runtime-dir <dir>` | where its `libruneruntime.a` is |
| `--link-arg <arg>` | appended to the link command verbatim |

> [!NOTE]
> **Why `--cc` and `--target` are separate**
>
> A driver named for its target — `x86_64-w64-mingw32-gcc` — is already the right compiler and is left alone. Only a general one such as `clang` is told the target, because it is one binary for all of them.
