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
| `Targets.h` / `.cpp` | What a build is for: foreign targets and `[target.<name>]` tables |
| `Workspace.h` / `.cpp` | `[workspace]` members, `rune ws`, dependency order between members |

## Layout discovery

`loadManifest` reads the manifest and then *looks* at `src/`. Every file that
declares an output of its own becomes a target:

| File | Becomes |
| --- | --- |
| `src/lib.rune` | the package library, `<name>.rul` |
| `src/main.rune` | the package binary, `<name>` |
| any file with a top-level `main` | its own executable, named after the file |
| any file with `#type(...)` | whatever it says |
| anything else | a *component* — importable, compiled into each target that needs it |
| a folder under `src/` | a *local unit* — compiled on its own into `deps/<folder>.rul` |

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

## Local units

`loadManifest` splits `src/`: the files at its top level are the package's
`Sources`, and each folder is a `LocalUnit`. `buildLocalUnits` compiles every
unit before the package itself, to `target/<profile>/deps/<unit>.rul` with
`--module <unit>`. Putting it in `deps/` is the whole trick: every existing
`-I deps` — the package's own compile, its tests, the editor's `--source`
check — already sees it, and the unit's library joins the package node's
inputs so that dependents get it too.

`runec` skips any `.rul` whose module name is the module being compiled, so a
unit's own stale library on the import path is never mistaken for a
dependency of itself. The language server's `runetools/project.rune` knows
units too, in `compileCommand` and `resolveImport`.

## Workspaces

A directory whose `Rune.toml` has a `[workspace]` table lists packages under
it as `members`. `rune ws` re-invokes the `rune` binary once per member with
`-C <dir>`, ordered so that a member depending on another by `path` comes
after it. At a workspace root with no `[package]` of its own, a bare `rune
build`, `check`, `test`, `clean` or `doc` delegates the same way — decided in
`main.cpp` before any target is resolved. `ws add` and `ws remove` rewrite
the `members` line textually and leave the rest of the file as it was.

## Installed, or built from source

`rune` and `runec` find the standard library, the runtime and the tools'
sources through `runec/include/rune/Install.h`: beside the executable when
it is in an installed layout (`bin/`, `lib/rune/`, `share/rune/`), and at the
paths the build compiled in otherwise. That is what lets a release archive
be unpacked anywhere; `cmake --install` produces the layout, and
`scripts/package.sh` packs it. `.github/workflows/release.yml` runs that on
macOS, Linux (manylinux_2_28, glibc 2.28) and Windows (MSYS2), against a
static LLVM from `scripts/build-llvm.sh`.

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
| `lint`, `fmt`, `lsp` | Run `rune-lint`, `rune-fmt` or `rune-lsp` from `~/.rune/bin`: copied there from beside the compiler (`adoptBuiltTool`) when that one is newer, or built from `tools/` when there is none |
| `doc lint`, `doc lsp`, `doc fmt`, `doc ffi` | Build and open that tool's book from `tools/books/` |
| `ffi` | Run `rune-ffi`, building it into `~/.rune/bin` the first time (`ensureFfiTool`: the `interface` unit as a library, then the program, against the libclang `findLibclangDir` finds) |
| `tools`, `tools install [name...]` | List the tools in `kTools` and where each is; build or copy them into `~/.rune/bin` |
| `ws <command>` | The command in every workspace member, dependencies first |
