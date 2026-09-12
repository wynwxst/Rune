# Using the package manager

`rune` handles layout, dependencies and linking. `rune new` scaffolds a package; `rune run` builds and runs it.

```sh
rune new hello        # or: rune new mylib --lib
cd hello
rune run
rune test
rune build --release
```

| Path | Meaning |
| --- | --- |
| `Rune.toml` | the package manifest |
| `src/main.rune` | binary root, becomes `target/<profile>/<name>` |
| `src/lib.rune` | library root, becomes `target/<profile>/<name>.rul` |
| `src/other.rune` | a submodule, importable as `<name>::other` |
| `tests/*.rune` | one test program per file, run by `rune test` |
| `target/` | build output; safe to delete |

*Package layout*
