# 32-bit targets

`usize` and `isize` are the target's pointer width, so a 32-bit build — `wasm32`, `i686` — sizes them at 4 bytes and everything measured in them — an allocation, `mem::offset`, a container's index — follows. The standard library declares C's `size_t` as `usize` and a file offset as `isize` for the same reason: a `u64` in either place would pass a doubled argument to libc on such a target.

```sh
$ runec --target i686-w64-mingw32 -c -o hello.o hello.rune
$ rune test --target wasm           # wasm32: a 32-bit target too
```

> [!NOTE]
> **Where `isize` is not exactly `long`**
>
> C's `long` is pointer-sized everywhere Rune targets except 64-bit Windows, where it stays 32 bits. `isize` is the closest spelling Rune has, and it is what the file-offset declarations use; on Windows x64 the value rides in the low half of the register, which is why the wider spelling works there.

> [!NOTE]
> **No conditional compilation**
>
> Nothing here changes the language. A program that cross-compiles is the same program; only the toolchain around it differs.
