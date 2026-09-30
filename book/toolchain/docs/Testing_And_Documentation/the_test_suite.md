# The test suite

```sh
ctest --test-dir build --output-on-failure
```

One CTest test, `rune_end_to_end`, which is a CMake script
(`tests/RunCases.cmake`) that compiles, links and runs every `tests/cases/*.rune`
and compares what it printed against the expectations in its own header.

## Writing a case

```rune
// What this case is about, in a sentence.
// EXPECT: first line of stdout
// EXPECT: second line

import std::io

fn main() -> i64 {
    io::println("first line of stdout")
    io::println("second line")
    0
}
```

| Header line | Means |
| --- | --- |
| `// EXPECT: <line>` | This line of stdout, in this order |
| `// EXPECT-PANIC: <substring>` | The program must abort, and stderr must contain this |
| `// EXPECT-ERROR: <substring>` | Compilation must fail with this in the message (a regex — parenthesise nothing you mean literally) |
| `// SAFETY: <level>` | Compile at this safety level |
| `// FLAGS: <args...>` | Extra `runec` flags for this case — `--no-overflow-checks`, `-O2` |
| `// LIB: <module> <path>` | Build `<path>` into `<module>.rul` first, and hand it over with `-I` |

`// LIB:` is how library-boundary behaviour is tested. The source lives under
`cases/lib/` so the glob — which only looks at the top level — does not pick it
up as a case of its own. Several may be given, and are built in order, so one
library may import another.

## What to test, by kind of change

| Change | Case to write |
| --- | --- |
| Syntax | The form, used and producing output |
| A type rule that rejects something | `EXPECT-ERROR` with enough of the message to be unambiguous |
| A runtime behaviour | `EXPECT` lines, or `EXPECT-PANIC` |
| Anything crossing a `.rul` boundary | A `// LIB:` case — this is where per-type symbol naming goes wrong |
| Codegen | Something that *runs* and prints, not just compiles |

`EXPECT-ERROR` matches a substring, so quote enough to be unambiguous and
little enough that rewording the message later does not break the test.

Two traps in the runner, which is a CMake script: `EXPECT-ERROR` and
`EXPECT-PANIC` are *regular expressions*, so `(`, `+` and `[` in a message
have to be avoided or escaped; and an `EXPECT:` line holding a lone `[` or
`]` breaks CMake's list splitting for every line after it — a pretty-printed
JSON array cannot be expected line by line, but an object can.

## The registry test

The second CTest test, `rune_registry`, is `tests/registry_test.py`: a
registry made from two small packages, added to a client under a temporary
`RUNE_HOME`, then searched, depended on, built against, updated, tampered
with and removed — each step checked through `rune`'s own output. A second
registry carrying the same package at another version covers what names
are for: aliasing, `registry::package`, `--registry` on every command,
`pkg server list` and `remove`, a manifest that insists on a registry, and
`rune doc <package>` with `--no-open`. It touches no real cache. Run it
alone with `python3 tests/registry_test.py build-release/bin/rune`.

The third, `rune_ecosystem`, is `examples/package/ecosystem.py check`: the
example ecosystem's two registries built from source, every package tested
against dependencies installed from them, and both apps run. It is the test
that a change to resolution or installation is felt by a realistic graph.

## The cross-compilation test

`rune_cross` is `tests/cross_test.py`, under a temporary `RUNE_HOME`. Its
first half needs no toolchain: `rune targets` in and out of a package, a
mistyped `--target` and its suggestion, a table with no `triple`, an unknown
`base`, a WASI SDK pointed at an empty directory. Its second half needs the
WASI SDK and wasmtime, and is skipped with a line saying so without them: a
new package built and run for `wasm` and `wasm-threads`, the FFI example's
tests run as WebAssembly, and a handful of `tests/cases` compiled with
`runec --target` for each flavour and compared against their `// EXPECT:`
lines. Run it alone with `python3 tests/cross_test.py build/bin`.

## The other checks

The suite is not the only thing that exercises the compiler:

| Check | Exercises |
| --- | --- |
| `python3 docs/reference/build.py` | Compiles, links and runs **the documentation samples** — 346 at the time of writing |
| `python3 tools/check_stdlib_docs.py` | Compiles and runs every program in the standard library's guide pages |
| `rune build` over `examples/project/*` | Multi-package builds, dependencies, C sources, FFI |
| `rune build --target mingw` plus `wine` | The COFF path, which the host build never touches |
| `rune run --target wasm` and `--target wasm-threads` | A 32-bit target, static linking, and the runtime without sockets, processes or (in the first) threads |

The reference build is the broadest of the three and takes a few minutes. Run
it before anything that touches code generation — and do not rebuild `runec`
while it is running, or it will fail on a missing binary halfway through.

> Two example packages fail `rune test` for reasons of their own:
> `examples/project/cli` needs command-line arguments its test does not pass,
> and `examples/project/site` has a call with the wrong number of arguments.
> Neither is a toolchain problem. Everything else builds and tests clean.
