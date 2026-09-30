# Rune.toml

**Every key the manifest understands**

```text
[package]
name = "report"                 # required
version = "0.1.0"               # required
edition = "2025"
description = "Command line front end for the statistics library"
authors = ["you <you@example.com>"]
license = "MIT"

[build]
safety = "full"                 # none | minimal | full
memory = "arc"                  # arc | zombie (single ownership, no count)
emit = "exe"                    # exe | lib | obj | asm | llvm-ir
optimize = 0                    # 0..3, or use --release
debug = true
warnings-as-errors = false
no-stdlib = false
link = ["m"]                    # -l, inherited by dependents
link-paths = ["/usr/local/lib"] # -L
c-sources = ["c/shim.c"]        # compiled with the build's own toolchain
c-flags = ["-Wall"]
cxx-sources = ["cxx/shim.cpp"]  # a C++ half, built by the matching driver
cxx-flags = ["-Wall"]
cxx-standard = "c++17"
link-args = ["-Wl,-z,now"]      # passed to the linker verbatim
target = "mingw"                # build for this target unless told otherwise

[target.mingw]                  # `rune build --target mingw`
triple = "x86_64-w64-mingw32"
cc = "x86_64-w64-mingw32-gcc"
cxx = "x86_64-w64-mingw32-g++"  # derived from `cc` when not given
runner = "wine"                 # how to run one of its binaries here

[dependencies]
statistics = { path = "../statistics" }

[lib]
src = "src/lib.rune"            # override the default root

[bin]
src = "src/main.rune"

[tests]
src = "tests"                   # where `rune test` looks
```

| Table | Key | Default |
| --- | --- | --- |
| `[package]` | `name` | *required* |
|  | `version` | *required* |
|  | `edition` | `"2025"` |
|  | `description`, `authors`, `license` | empty |
| `[build]` | `safety` | `"full"` |
|  | `emit` | empty, meaning an executable |
|  | `optimize` | `0`, or `2` with `--release` |
|  | `debug` | `true`, `false` with `--release` |
|  | `warnings-as-errors` | `false` |
|  | `no-stdlib` | `false` |
|  | `link`, `link-paths` | empty; inherited by dependents |
|  | `c-sources`, `c-flags` | empty |
|  | `cxx-sources`, `cxx-flags` | empty; a C++ half |
|  | `cxx-standard` | `"c++17"` |
|  | `link-args` | empty; passed to the linker verbatim |
|  | `target` | empty, meaning the host |
| `[target.<name>]` | `triple` | *required* |
|  | `cc`, `cxx`, `ar` | the host's, which usually cannot cross |
|  | `sysroot`, `runtime-dir` | empty |
|  | `runner` | empty: its binaries cannot be run here |
|  | `link`, `link-paths`, `link-args` | empty |
| `[dependencies]` | `name = { path = "..." }` | — |
| `[lib]` | `src` | `src/lib.rune` |
| `[bin]` | `src` | `src/main.rune` |
| `[tests]` | `src` | `tests` |
