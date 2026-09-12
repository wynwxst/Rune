# rune

| Command | Does |
| --- | --- |
| `rune new <name>` | a package in a new directory |
| `rune new <name> --lib` | the same, as a library |
| `rune init` | a package here |
| `rune build` | build this package and its dependencies |
| `rune run [-- args]` | build, then run, forwarding arguments |
| `rune test` | build and run every program under `tests/` |
| `rune check` | type-check without producing output |
| `rune doc [--open]` | read `docs/` and the source; write `target/<profile>/docs`, and show it |
| `rune doc std::io` | the standard library's reference, from the cache, at that module |
| `rune targets` | the cross targets this package configures |
| `rune clean` | delete `target/` |

| Option | Does |
| --- | --- |
| `--release` | `-O2`, no debug information |
| `--target <name>` | a `[target.<name>]` toolchain, or a triple |
| `--emit <kind>` | `llvm-ir`, `asm`, `obj`, `lib` or `exe` — what to produce instead of linking |
| `-C, --directory <d>` | operate on the package in *d* |
| `-j, --jobs <n>` | compile at most *n* things at once; default one per core |
| `--cfg <name>` | set *name* for `@Config(...)`, on top of `[build] cfg` |
| `-v, --verbose` | print each command as it runs |
| `--no-color` | plain output |

```sh
$ rune new report && cd report
$ rune run
$ rune run -- 1 2 3 4 5
$ rune test
$ rune build --release
$ rune build -j 4
```
