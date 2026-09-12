# Packaging a C half

A package that ships C alongside its Rune lists it, and `rune` compiles it with the same toolchain the rest of the build uses. That is also what makes such a package cross-compile: the C goes wherever the Rune goes.

**Rune.toml for a package with a C shim**

```toml
[package]
name = "ffi"
version = "0.1.0"

[build]
c-sources = ["c/shim.c"]
c-flags = ["-Wall", "-Wextra"]
link = ["m"]                    # a system library, asked for by name
```

`examples/project/ffi` is that package: a C shim, an `extern` block wrapping it, and a test for every shape that crosses. It is built and run for the host and cross-compiled to Windows, which is how the rules on this page are checked.

```sh
$ cd examples/project/ffi
$ rune test                       # host
$ rune test --target mingw        # built for Windows, run under wine
```
