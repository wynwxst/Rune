# C and C++ sources

A package with a C or C++ half lists it, and `rune` compiles it with whichever toolchain the build is using — so the native code crosses along with the Rune, and a package with an FFI shim needs nothing special to target another machine, WebAssembly included. The C++ driver is the one that goes with `cc` unless `[target.<name>] cxx` names another.

**Compiled with the build's own cc and c++**

```text
[build]
c-sources = ["c/shim.c"]
c-flags = ["-Wall", "-Wextra"]
cxx-sources = ["cxx/shim.cpp"]
cxx-flags = ["-Wall", "-Wextra"]
```
