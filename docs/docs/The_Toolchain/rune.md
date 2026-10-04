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
| `rune lint [path…] [--fix]` | look for likely mistakes and style problems; `--fix` applies the safe fixes |
| `rune fmt [path…] [--check]` | lay source out, writing in types and argument labels; `--check` only reports |
| `rune lsp` | the language server, for an editor, over stdin and stdout |
| `rune doc lint` / `lsp` / `fmt` / `ffi` | the book about that tool |
| `rune ffi <header>…` | Rune bindings for C headers, read with libclang; built the first time it runs — see **Calling C** |
| `rune tools` | the toolchain's tools and where each one is; `rune tools install [name…]` puts them in `~/.rune/bin` |
| `rune targets` | the cross targets this package configures |
| `rune clean` | delete `target/` |
| `rune ws …` | the same commands over every member of a workspace; see **Packages and registries** |

| Option | Does |
| --- | --- |
| `--release` | `-O2`, no debug information |
| `--target <name>` | a `[target.<name>]` toolchain, or a triple |
| `--emit <kind>` | `llvm-ir`, `asm`, `obj`, `lib` or `exe` — what to produce instead of linking |
| `-C, --directory <d>` | operate on the package in *d* |
| `-j, --jobs <n>` | compile at most *n* things at once; default one per core |
| `--cfg <name>` | set *name* for `#Config(...)`, on top of `[build] cfg` |
| `--memory <mode>` | `zombie` or `arc`, over the manifest's `[build] memory` |
| `--cfg <key>=<value>` | give a `[config]` key a value |
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
