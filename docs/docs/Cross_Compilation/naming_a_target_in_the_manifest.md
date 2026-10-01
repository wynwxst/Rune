# Naming a target in the manifest

A package that is built for the same machines repeatedly, or whose toolchain is somewhere a foreign target would not look, says so once. `[target.<name>]` describes a toolchain; naming one does not build for it — `--target` does, or `[build] target` when the command line does not say.

A table may start from a foreign target and change only what differs: with `base`, or by being named after one and giving no `triple`. What it names wins; what it leaves out is found as the foreign target would find it.

**Three targets in Rune.toml**

```toml
[package]
name = "report"
version = "0.1.0"

# The foreign target `wasm`, with the SDK somewhere of this project's own.
[target.wasm]
sdk = "../toolchains/wasi-sdk"

# Another name for it, run under a different runtime.
[target.web]
base = "wasm"
runner = "wasmer run --dir=."

# A target from scratch: everything named.
[target.pi]
triple = "aarch64-unknown-linux-gnu"
cc = "aarch64-linux-gnu-gcc"
sysroot = "/opt/pi-sysroot"
# How to run one of its binaries on *this* machine. Without it, `rune run`
# and `rune test` build and stop, rather than pretend.
runner = "qemu-aarch64 -L /opt/pi-sysroot"
```

| Key | Means |
| --- | --- |
| `base` | the foreign target to start from |
| `triple` | passed to `runec --target`; needed unless there is a base |
| `cc` | the C compiler for the runtime and `c-sources`; also what links, unless `linker` says otherwise |
| `linker` | what links: a compiler (`i686-elf-gcc`) or a linker (`i686-elf-ld`, `ld.lld`), with any arguments of its own, or `"build-script"` |
| `linker-kind` | `"driver"` or `"ld"`: how `linker` takes flags; worked out from its name when not given |
| `default-flags` | `false`: add no flags of the build's own to compiles or links — only what this table says |
| `cxx` | the C++ driver; derived from `cc` when absent |
| `ar` | the archiver; derived from `cc` when absent |
| `sysroot` | passed as `--sysroot` |
| `sdk` | where the WASI SDK is, for a target based on `wasm` |
| `runner` | how to start a built program here; `{}` marks where the program goes, and a word naming a file in the package is read from Rune.toml's directory |
| `runtime-dir` | a prebuilt `libruneruntime.a` to use instead of building one |
| `c-flags` | added to every C compile for the target, the runtime's included |
| `link`, `link-paths`, `link-args` | native libraries the *target* needs, on top of the package's |

*Relative paths are relative to Rune.toml.*

```sh
$ rune targets
In Rune.toml
  wasm            wasm32-wasip1
      the foreign target wasm, adjusted
      ✓ builds with /work/toolchains/wasi-sdk/bin/wasm32-wasip1-clang
      ✓ runs with wasmtime run -S inherit-env=y --dir=.
  web             wasm32-wasip1
      the foreign target wasm, adjusted
      ...

$ rune build --target web
$ rune test --target pi             # built, then run under qemu
```

[`examples/wasm-wordfreq`](examples/wasm-wordfreq/README.md) is a whole package built this way: `[build] target = "wasm"` makes WebAssembly its default, and `rune run -- sample.txt` counts the words in a file under wasmtime, which lets it read only the directory it was started in.
