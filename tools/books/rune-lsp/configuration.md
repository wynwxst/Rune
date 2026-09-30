# Configuring it

## The command line

`rune lsp` passes on anything after it, so these work through either command:

```sh
rune-lsp [--stdlib <dir>] [--runec <path>] [--rune <path>]
```

| Flag | Default |
| --- | --- |
| `--stdlib <dir>` | the directory holding `std/` — see below |
| `--runec <path>` | the compiler, for files outside a package: the `runec` beside `rune-lsp`, or on `PATH` |
| `--rune <path>` | the package manager, for files in a package: the `rune` beside `rune-lsp`, or on `PATH` |
| `--version` | print the version and exit |

`rune lsp` fills in all three with the toolchain's own. Any other argument is
ignored, since clients sometimes add their own, such as `--stdio`.

Without `--stdlib`, the standard library is looked for in `$RUNE_STDLIB`, then
next to the program — `../../stdlib` from a build tree's `bin/`, `../stdlib`,
`../share/stdlib` — and then in `~/.rune/stdlib`. The server logs where it found
it when it starts.

## Settings

An editor passes settings in the `initialize` request's
`initializationOptions`, and may change them later with
`workspace/didChangeConfiguration`, under `settings.rune`. Both take the same
shape:

```json
{
  "stdlib": "",
  "runec": "",
  "rune": "",
  "checkOnSave": true,
  "check": {
    "onChange": true,
    "delay": 400,
    "memory": "zombie"
  },
  "inlayHints": {
    "types": true,
    "parameters": true
  },
  "format": {
    "types": true,
    "labels": true,
    "reorder": true,
    "lintFixes": true
  },
  "lint": {
    "enable": true,
    "allow": ["line-too-long"],
    "warn": ["missing-docs"],
    "maxLineLength": 0
  }
}
```

| Setting | Meaning |
| --- | --- |
| `stdlib`, `runec`, `rune` | as the flags; empty keeps what the command line gave |
| `checkOnSave` | run the compiler on open and save |
| `check.onChange` | also run it once typing pauses, over the unsaved text; with `checkOnSave` also `false`, only lint diagnostics are left |
| `check.delay` | how long the pause is, in milliseconds (400) |
| `check.memory` | `"zombie"` (the default) or `"arc"`: the memory model a file outside any package is checked in; `arc` checks it with reference counting instead of the borrow checker. A package's `Rune.toml` decides for its own files |
| `inlayHints.types` | show each unannotated binding's inferred type |
| `inlayHints.parameters` | show the parameter each positional argument goes to |
| `format.types`, `format.labels` | what Format Document writes in: types, argument labels |
| `format.reorder` | whether it puts directives, imports, aliases and globals first |
| `format.lintFixes` | whether it applies the linter's style fixes |
| `lint.enable` | show lint findings at all |
| `lint.allow`, `lint.warn` | rules to turn off and on, after the package's `[lint]` table |
| `lint.maxLineLength` | the `line-too-long` limit; `0` keeps the package's, or 120 |

Every key may be left out. The VS Code extension builds this object from its
`rune.*` settings.

## The package's own settings

The `[lint]` table in `Rune.toml` applies to everyone who opens the package, in
any editor and on the command line alike; the editor's settings are applied on
top of it. `rune doc lint` describes the table.

The server reads a package's `Rune.toml` when it first needs it, and again after
any file is saved or changes on disk, so an edit to it takes effect without a
restart.
