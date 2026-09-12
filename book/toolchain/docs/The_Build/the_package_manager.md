# The package manager

## The files

| File | Holds |
| --- | --- |
| `main.cpp` | Commands, the package graph, the steps |
| `Manifest.h` / `.cpp` | `Rune.toml` as a struct; layout discovery |
| `Toml.h` / `.cpp` | A small TOML reader |
| `Jobs.h` / `.cpp` | The thread pool, output serialisation, `runCaptured` |
| `Fingerprint.h` / `.cpp` | Content digests and the stamp files |
| `Registry.h` / `.cpp` | Registries, `Rune.lock`, the install store — see [The package registry](the_package_registry.md) |
| `Console.h` / `.cpp` | The status lines, shared with the registry |

## Layout discovery

`loadManifest` reads the manifest and then *looks* at `src/`. Every file that
declares an output of its own becomes a target:

| File | Becomes |
| --- | --- |
| `src/lib.rune` | the package library, `<name>.rul` |
| `src/main.rune` | the package binary, `<name>` |
| any file with a top-level `main` | its own executable, named after the file |
| any file with `@type(...)` | whatever it says |
| anything else | a *component* — importable, compiled into each target that needs it |

`[[bin]]` entries in the manifest add targets too. A `[[bin]]` whose file also
defines `main` is therefore named twice — once by the manifest, once by the
scan — and `targetsOf` deduplicates by output name and kind. Without that,
two steps would write the same executable at the same time.

## What a package produces

```
target/<profile>/
  <name>.rul            the library
  <name>                the binary
  <other>               one per extra root
  deps/                 every dependency's .rul, staged flat
  c/                    objects from `c-sources`
  tests/                one executable per tests/*.rune
  .fingerprints/        one stamp per output
```

A cross build gets `target/<target-name>/<profile>/` instead, so host and cross
output never overwrite each other and neither has to be rebuilt after
switching.

## Why dependencies are staged flat

Every library in the subtree is copied into the dependent's `deps/`, not just
the direct ones. The compiler resolves imports from a single search path, and a
public API may well mention types from a dependency's own dependency — so they
all have to be visible at once.

## Module names, and the one subtlety

A binary in a package that also builds a library gets a module name of its own:
`<pkg>__bin_<stem>`, not `<pkg>`. The package name then belongs to the library
alone, so a binary that says `import <pkg>` resolves *outward* to the real
`.rul` instead of to the module it is already inside.

## Commands

| Command | Does |
| --- | --- |
| `new`, `init` | Scaffold |
| `build` | The graph, then the steps |
| `run [name]`, `run --all` | Build, then launch |
| `test` | Build every `tests/*.rune`, run them in order |
| `check` | Build libraries, `--check` the rest |
| `doc` | A `.rdoc` sidecar per target, then `rune-doc` |
| `clean` | Delete `target/` |
| `targets` | List configured cross targets |
