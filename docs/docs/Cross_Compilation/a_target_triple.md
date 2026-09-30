# A target triple

`--target` also takes a triple directly, for a target that needs no toolchain beyond the one already here — which is most of them when the host compiler can reach the target, as Apple's clang can reach `x86_64-apple-darwin`. The compiler emits a real object for that machine, in that machine's format.

```sh
$ runec --target x86_64-w64-mingw32 -c -o hello.o hello.rune
$ file hello.o
hello.o: Intel amd64 COFF object file
```

Linking is the part that needs help. A linker is platform software: it knows one set of startup files, one libc, one executable format. Cross-compiling means naming the one that belongs to the target — which is what a foreign target does for you, and what these flags do by hand.

| Flag | Does |
| --- | --- |
| `--target <triple>` | what to emit for |
| `--cc <program>` | the toolchain driver that links |
| `--sysroot <dir>` | where that target's headers and libraries are |
| `--runtime-dir <dir>` | where its `libruneruntime.a` is |
| `--link-arg <arg>` | appended to the link command verbatim |
| `--link-cxx` | link the C++ runtime (implied by `extern "C++"`) |
| `--cxx-stdlib <lib>` | `libc++` for C++ built with `-stdlib=libc++`, `libstdc++`, or the platform's own |
| `--linker <program>` / `--linker-kind ld` | link with a linker run directly, in its own spelling |
| `--no-default-link-args` | nothing of the compiler's own on the link line |

```sh
$ runec --target wasm32-wasip1 \
        --cc /opt/wasi-sdk/bin/wasm32-wasip1-clang \
        --sysroot /opt/wasi-sdk/share/wasi-sysroot \
        --runtime-dir ~/.rune/runtime/wasm32-wasip1 \
        -o hello.wasm hello.rune
```

> [!NOTE]
> **Why `--cc` and `--target` are separate**
>
> A driver named for its target — `x86_64-w64-mingw32-gcc`, `wasm32-wasip1-clang` — is already the right compiler and is left alone. Only a general one such as `clang` is told the target, because it is one binary for all of them.
