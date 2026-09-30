# Troubleshooting

## Look at the log first

The server writes a line to the editor's log when it starts, saying where it
found the standard library. In VS Code that is the *Rune Language Server* output
channel (**Rune: Show Language Server Output**); set `rune.trace.server` to
`messages` to see every request and answer there as well. In Neovim,
`:LspLog`; in Helix, `hx --log`; in Emacs, the `*EGLOT events*` buffer.

## The server does not start

Check that the command the editor runs works on its own:

```sh
rune lsp --version
```

If it does in a terminal but not in the editor, the editor has a different
`PATH`. Give it the full path — `rune.server.path` in VS Code, `cmd` in Neovim,
`command` in Helix.

## No compiler errors, only lint findings

The compiler runs on open, on save, and once typing pauses; if `check.onChange`
is off, save the file. If that does not help, the server could not run the compiler: it says so once, in a
message naming the command it tried. `rune` is needed for a file in a package and
`runec` for one outside; both are found beside `rune-lsp` or on `PATH`, or named
with `--rune` and `--runec`.

`checkOnSave` and `check.onChange` (`rune.checkOnSave` and `rune.check.onChange`
in VS Code) turn compiling off when both are `false`.

## No borrow checker errors

The borrow checker runs only under `--memory zombie`. In a package, that is
`[build] memory = "zombie"` in `Rune.toml`; for a file outside any package, set
`check.memory` to `"zombie"` (`rune.check.memory` in VS Code, `g:rune_memory` in
Vim).

## Standard library names are not completed

The log's first line says `stdlib at (not found)`. Pass `--stdlib <dir>` — the
directory that holds `std/` — or set `stdlib` in the settings, or
`$RUNE_STDLIB` in the editor's environment.

## A package's other modules are not found

The server finds a file's package by the nearest `Rune.toml` above it, and takes
the package's name from its `[package]` table. An `import` of the package's own
module has to use that name: `import shapes::area` in a package called `shapes`.

## Something is completed, hovered or reported wrongly

The server reads source without compiling it, and there are programs it will
misread. The smallest file that shows the problem, and the position in it, is
everything needed to fix it. For a lint finding, `rune lint` on the same file
will show whether it is the rule or the server.
