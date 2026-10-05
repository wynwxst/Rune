# Building it

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
export PATH="$PWD/build/bin:$PATH"
```

You need CMake 3.20 or newer, a C++20 compiler, and LLVM 17 or newer with
development headers. If LLVM is somewhere CMake will not find on its own, say
where:

```sh
cmake -S . -B build -DLLVM_DIR=$(llvm-config --cmakedir)
```

On a Homebrew machine the top-level `CMakeLists.txt` already runs
`llvm-config` out of `/opt/homebrew/opt/llvm/bin` and `/usr/local/opt/llvm/bin`
before giving up, because Homebrew keeps LLVM out of the default prefix.

## What gets built, in what order

The order matters and is written into the top-level `CMakeLists.txt`, because
part of the toolchain is written in Rune and needs the rest of it first.

1. **`runec`** — the compiler. Links `librunec_core.a` against LLVM's `core`, `support`, `irreader`, `bitwriter`, `analysis`, `passes`, `target`, and **every target LLVM was built with**, so that one binary can cross-compile anywhere.
2. **`runetime`** — the memory core, written in Rune. Compiled by the `runec` that was just built.
3. **`runtime`** — the C runtime, plus the object from step 2, archived into `libruneruntime.a`. Every Rune program links this.
4. **`rune`** — the package manager.
5. **`rune-doc`** — the documentation generator, which is itself a Rune program (`tools/rune-doc.rune`), compiled by the compiler from step 1.

So the build is a small bootstrap: the compiler compiles part of its own
runtime, and the toolchain documents itself with a program it compiled.

## Build types, and why it matters more than usual

`runec` statically links LLVM, so the compiler's own build type shows up in
every compile it performs. Measured on this tree, an eight-core Apple M3:

| | `Debug` | `Release` |
| --- | --- | --- |
| Binary size | 177 MB | 167 MB |
| Start-up, before any work | 24 ms | 14 ms |
| One `hello_world` compile | 141 ms | 67 ms |
| The 123-case suite | 36 s | 26 s |

Roughly twice as fast, for a flag.

| Build type | Use it when |
| --- | --- |
| `Debug` | You are stepping through the compiler in a debugger. |
| `RelWithDebInfo` | Default when nothing says otherwise: fast, still debuggable. |
| `Release` | Shipping, and benchmarking. |

If compile times look bad while you are working on something else, check what
`CMAKE_BUILD_TYPE` your build directory was configured with before concluding
anything about the code. Keeping two directories around — `build` for
debugging the compiler, `build-release` for using it — costs nothing but disk.

## Which LLVM

Any LLVM 17 or newer with development headers. Homebrew's `llvm` formula is a
**Release** build with assertions off, which is what you want: an
assertions-enabled LLVM makes the compiler noticeably slower and is only worth
it when you suspect you are misusing an LLVM API.

```sh
llvm-config --version --build-mode --assertion-mode
```

The size of `runec` is mostly LLVM, and mostly unavoidable: it links every
target LLVM was built with, which is what lets one binary cross-compile
anywhere. Of a 167 MB Release binary, 95 MB is code and 53 MB is constant data,
and stripping recovers about 14 MB of symbol table.

## Paths baked in at configure time

Three compile definitions tell the binaries where things are, so a toolchain
built anywhere can find its own parts:

| Definition | Means |
| --- | --- |
| `RUNE_DEFAULT_STDLIB_DIR` | Where `stdlib/` is. Overridable with `--stdlib`. |
| `RUNE_RUNTIME_LIB_DIR` | Where `libruneruntime.a` is. Overridable with `--runtime-dir`. |
| `RUNE_TOOLCHAIN_ROOT` | The source tree, used by `rune doc` to find the doc UI assets. |

## Useful environment variables

| Variable | Does |
| --- | --- |
| `RUNEC` | Which compiler `rune` should call. Handy for testing a build against a package tree. |
| `RUNE_JOBS` | How many threads `runec` may use inside one compile. |
| `RUNE_CC` | The link driver, when `--cc` does not name one. |
| `RUNE_HOME` | Where `rune` keeps its cached generator, stylesheet and stdlib copy. Defaults to `~/.rune`. |
| `RUNE_DEBUG_RC` | Set when *running* a compiled program: poisons freed objects and reports reference-counting violations. |
