# Naming a target in the manifest

A package that is built for the same machines repeatedly says so once. `[target.<name>]` describes a toolchain; naming one does not build for it, `--target` does.

**Two targets in Rune.toml**

```toml
[package]
name = "report"
version = "0.1.0"

[target.mingw]
triple = "x86_64-w64-mingw32"
cc = "x86_64-w64-mingw32-gcc"
# Extra libraries this target needs, added to the package's own.
link = ["ws2_32"]
# How to run one of its binaries on *this* machine. Without it, `rune run`
# and `rune test` build and stop, rather than pretend.
runner = "wine"

[target.pi]
triple = "aarch64-unknown-linux-gnu"
cc = "aarch64-linux-gnu-gcc"
sysroot = "/opt/pi-sysroot"
runner = "qemu-aarch64"
```

```sh
$ rune targets
  mingw
      triple  x86_64-w64-mingw32
      cc      x86_64-w64-mingw32-gcc
      runner  wine
  pi
      triple  aarch64-unknown-linux-gnu
      cc      aarch64-linux-gnu-gcc
      runner  qemu-aarch64

$ rune build --target mingw
$ rune test --target mingw          # built, then run under wine
```

`--target` also takes a triple directly, for a target that needs no configuration beyond one — which is most of them when the host compiler can already reach the target, as Apple's clang can reach `x86_64-apple-darwin`.
