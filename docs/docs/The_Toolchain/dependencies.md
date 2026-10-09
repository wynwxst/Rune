# Dependencies

A dependency is a path, or a version from a registry. `rune build` builds each one first, emits its `.rul`, and passes both the module search path and every native library it asked for down to whatever depends on it — so a package that needs `-l m` says so once, in its own manifest.

```sh
[dependencies]
geometry = { path = "../geometry" }    # a package on this disk
shapes = "1.0"                         # from a registry: ^1.0, the newest 1.x
report = { version = "=0.3.2" }        # exactly that version
fastmath = { cargo = "rust/fastmath" } # a Rust crate; see Calling Rust
```

| Requirement | Accepts |
| --- | --- |
| `"1.2.3"`, `"^1.2.3"` | >=1.2.3 and <2.0.0 — the same leading non-zero part |
| `"0.2"` | >=0.2.0 and <0.3.0 |
| `"~1.2"` | >=1.2.0 and <1.3.0 |
| `"=1.2.3"` | exactly that |
| `">=1.2, <2.0"` | every comparison listed |
| `"*"` | anything |

*Cargo's spellings, since they are the ones people know.*

```sh
$ cd examples/project/report
$ rune run
$ rune test
```
