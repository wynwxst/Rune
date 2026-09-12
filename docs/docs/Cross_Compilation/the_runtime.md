# The runtime

Every Rune program links a small runtime, half C and half Rune, and it is as target-specific as the program. `rune` builds it for a target the first time one is asked for and keeps it under `~/.rune/runtime/<triple>/`, using the same toolchain the rest of the build uses. A prebuilt one is used instead when `runtime-dir` names it.

```sh
$ rune build --target mingw
○ Preparing runtime for x86_64-w64-mingw32
○ Compiling report v0.1.0
● Finished debug profile
```
