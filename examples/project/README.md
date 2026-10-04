# Example project

A two-package workspace showing how `rune` builds a library, links a binary
against it, and runs its tests.

```
statistics/          library package
  Rune.toml          declares `link = ["m"]`, inherited by dependents
  src/lib.rune       the `statistics` module
  src/spread.rune    the `statistics::spread` submodule
  tests/basics.rune  one test program

report/              binary package
  Rune.toml          depends on ../statistics by path
  src/main.rune      the `report` module
```

## Building and running

```bash
cd report
rune run                 # uses the built-in sample
rune run -- 1 2 3 4 5    # arguments after `--` go to the program
rune build --release
```

```bash
cd statistics
rune test
```

`rune` builds dependencies first, stages their `.rul` files into
`target/<profile>/deps/`, and skips any step whose output is already newer
than its inputs. Native libraries a dependency asks for — `libm` here — are
propagated to whatever links against it.

## What it demonstrates

* A library split across several modules (`statistics` and `statistics::spread`)
* `pub` controlling exactly what crosses the package boundary
* `Option` for "there might be nothing to measure"
* `Result` plus `?` for argument parsing that reports the first bad value
* A `bind io::Display to Summary` so the summary prints itself
* `#safe("...")` justifying a foreign call to `sqrt`

## The other packages here

`statistics` and `report` are the pair the walkthrough above follows. The rest
each show one thing:

| Package | What it is for |
|---------|----------------|
| `webserver` | A multi-threaded HTTP server over `std::net` and `std::thread`. Values that own a file descriptor, and what it takes to hand one to another thread. |
| `traits` | Marks, `bind`, operators, weak references, `Option` and `Result`. |
| `json` | A small tool over `std::json` — read a document, follow a path into it, write it back — with a test that exercises the parser on what it should refuse as well as what it should accept. |
| `ffi` | A package with a C half: `[build] c-sources`, and the shims it needs. |
| `cli` | Decorators used to register command handlers. |
| `site` | Sockets reached directly through `extern "C"`, next to what `std::net` now wraps. |
| `tasks` | `async fn` and `.await` over `std::task`: overlapping fetches, a checksum on another thread, a class two tasks share, and a future completed by hand. |
| `carbon` | The smallest package there is. |

The packages here depend on each other by *path*. For the same idea over a
registry — packages published as versions, installed once, pinned by a lock —
see [`examples/package/`](../package/README.md), which is a seven-package
ecosystem with the registry that serves it.
