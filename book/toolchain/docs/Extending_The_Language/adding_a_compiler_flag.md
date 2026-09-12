# Adding a compiler flag

Four edits, all small, and one decision that is easy to get wrong.

## 1. The option

`CompilerOptions` in `runec/include/rune/Driver.h`:

```cpp
bool KeepGoing = false;
```

## 2. Parse it

`runCompilerMain` in `Driver.cpp` — a linear scan over `argv`, one `if` per
flag:

```cpp
if (a == "--keep-going") { opts.KeepGoing = true; continue; }
```

For a flag that takes a value, `needsValue(i, "--flag")` reads the next
argument and exits with a usage error if there is not one.

## 3. Document it in the usage text

The same file, in `printUsage`, under the right heading — `OUTPUT`, `CODE
GENERATION`, `MODULES AND LINKING`, `CROSS COMPILATION`, `DIAGNOSTICS`,
`INSPECTION` or `ENVIRONMENT`.

## 4. Use it

Wherever it applies. `opts` is threaded through `Sema` and `CodeGen`.

## The decision: does it belong in the build fingerprint?

`rune` decides whether to skip a compile by hashing the exact command line it
would run. So a flag reaches `rune` in one of two ways, and picking the wrong
one is a real bug:

| Kind of flag | Where `rune` adds it | Because |
| --- | --- | --- |
| **Changes what is produced** — `-O`, `-g`, `--safety`, `--target` | `appendBuildFlags` | It must invalidate the cached artefact |
| **Changes only how the build narrates itself** — `-v`, `--color`, `--time` | `runStep`, at spawn time | Otherwise `rune build -v` rebuilds the world, and `rune build` after it rebuilds it again |

There is a comment in `appendBuildFlags` saying exactly this, because the
mistake is invisible until someone notices that toggling verbosity recompiles
everything.

If your flag should also be settable from `Rune.toml`, add it to `Manifest`
(`rune/src/Manifest.h` and `Manifest.cpp`) and read it in `appendBuildFlags`.

## Then

Add the flag to `docs/reference/content.py` — the `runec` or `rune` table in
the toolchain section — and regenerate. The reference tables are the
user-facing documentation for flags; `--help` is not.
