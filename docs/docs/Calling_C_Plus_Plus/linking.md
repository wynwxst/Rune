# Linking

A file with an `extern "C++"` block links the target's C++ runtime automatically — libc++ where Apple ships it, libstdc++ where GCC does, and statically on MinGW so the executable carries no `libstdc++-6.dll`. Pass `--link-cxx` by hand for a program whose C++ only arrives through objects it links.

```sh
$ runec -o demo src/main.rune -L /usr/local/lib -l mylib
```

A package that ships C++ alongside its Rune lists it, and `rune` compiles it with the C++ driver that goes with the build's `cc` — so a package with a C++ half cross-compiles like any other.

**Rune.toml for a package with a C++ half**

```toml
[package]
name = "ffi"
version = "0.1.0"

[build]
cxx-sources = ["cxx/shim.cpp"]
cxx-flags = ["-Wall", "-Wextra", "-fno-exceptions", "-fno-rtti"]
cxx-standard = "c++17"          # the default

[target.mingw]
triple = "x86_64-w64-mingw32"
cc = "x86_64-w64-mingw32-gcc"
cxx = "x86_64-w64-mingw32-g++"  # derived from `cc` when not given
runner = "wine"
```

`examples/project/ffi` is that package: a C shim, a C++ shim, both declared and both tested, built for the host and cross-compiled to Windows. `tests/cases/95_cxx_llvm.rune` goes further and drives LLVM's own C++ API — a context, a module, a function, an `add` and a `ret`, then the verifier — with no wrapper of any kind.

```sh
$ cd examples/project/ffi
$ rune test                       # host
$ rune test --target mingw        # built for Windows, run under wine
```
