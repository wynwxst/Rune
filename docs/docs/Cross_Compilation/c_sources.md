# C sources

A package with a C half lists it, and `rune` compiles it with whichever toolchain the build is using — so the C crosses along with the Rune, and a package with an FFI shim needs nothing special to target another machine.

**Compiled with the build's own cc**

```text
[build]
c-sources = ["c/shim.c"]
c-flags = ["-Wall", "-Wextra"]
```
