# Foreign targets

A handful of targets are built in, by name. Each knows its triple, where its toolchain is usually installed, and what can run its programs here, so building for one takes nothing but the name.

```sh
$ rune build --target wasm         # WebAssembly, with the WASI SDK
$ rune run --target windows        # built with mingw-w64, run under wine
$ rune test --target linux-arm64   # built with GCC, run under qemu
```

| Name | Triple | Builds with | Runs here with |
| --- | --- | --- | --- |
| `wasm` | `wasm32-wasip1` | the WASI SDK | `wasmtime`, `wasmer` or `wasm3` |
| `wasm-threads` | `wasm32-wasip1-threads` | the WASI SDK | `wasmtime`, with threads on |
| `windows` | `x86_64-w64-mingw32` | `x86_64-w64-mingw32-gcc` | `wine` |
| `linux-arm64` | `aarch64-linux-gnu` | `aarch64-linux-gnu-gcc` | `qemu-aarch64` |
| `linux-x64` | `x86_64-linux-gnu` | `x86_64-linux-gnu-gcc` | `qemu-x86_64` |
| `linux-riscv64` | `riscv64-linux-gnu` | `riscv64-linux-gnu-gcc` | `qemu-riscv64` |

*Each also answers to other spellings — `wasi`, `mingw`, `linux-aarch64`, and its own triple.*

`rune targets` lists them, and says for each whether its toolchain was found and whether this machine can run what it builds — and, when something is missing, how to get it. It works outside a package too.

```sh
$ rune targets
Foreign targets  (built in; --target <name>)
  wasm            wasm32-wasip1
      WebAssembly with WASI, built with the WASI SDK
      ✓ builds with /opt/wasi-sdk/bin/wasm32-wasip1-clang
      ✓ runs with wasmtime run -S inherit-env=y --dir=.
  windows         x86_64-w64-mingw32
      64-bit Windows, built with mingw-w64
      ✗ cannot find 'x86_64-w64-mingw32-gcc', which windows builds with
        install mingw-w64: `apt install gcc-mingw-w64-x86-64` or `brew install mingw-w64`
        or name another compiler: `cc = "..."` in [target.windows]
  ...
```

A name that is none of these is a mistake worth catching early, so a close one is suggested:

```sh
$ rune build --target wams
● no target named 'wams'
  ─  note: did you mean 'wasm'?
  ─  note: `rune targets` lists every target this package can build for; a target triple works too
```
