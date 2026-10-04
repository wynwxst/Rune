# Rune for Visual Studio Code

Language support for Rune: syntax highlighting, and — through the
Rune language server — diagnostics, completion, hover, navigation and quick fixes.

## What you get

- **Syntax highlighting** for `.rune` files, and for ```` ```rune ```` blocks in Markdown.
- **Compiler diagnostics as you type** — every error a build reports: types, macro
  expansion, and the borrow checker's under `--memory zombie`, with where a value was
  moved or borrowed underlined as a hint. Unsaved text is checked once typing pauses.
  Inside a package (a directory with a `Rune.toml`) the server runs `rune check`, which
  knows the package's other files, dependencies and memory model; a file on its own is
  checked with `runec --check` (set `rune.check.memory` to `zombie` for the borrow checker).
- **Inlay hints**: each binding's inferred type and each argument's parameter name,
  greyed out. Double-click one to write it into the code.
- **Format Document** formats like `rune fmt`: directives and imports first, four-space
  indentation, and the types and argument labels the compiler knows written in.
- **Lint diagnostics as you type**, from the same rules as `rune lint`, with quick fixes
  for the ones that have a certain answer (`var` that could be `let`, an unused import,
  `while true`, trailing whitespace…) and a *Fix All* source action.
- **Completion**: locals and parameters in scope, the file's declarations, imported
  modules and what they export (`io::`), the members of a value (`list.`), the compiler's
  intrinsics (`text.$`), enum variants (`Shape::`), decorators (`@`), and module paths
  after `import`. Functions complete with their parameters as a snippet.
- **Hover** with a declaration's signature and its `///` documentation, and the inferred
  type of a local.
- **Go to definition**, **find references** and **highlight occurrences** — into the
  standard library and a package's other modules as well.
- **Document outline**, and **signature help** while typing arguments.
- Comment toggling, bracket matching, auto-closing, and `///` continuation on Enter.

## Setting it up

The extension starts the language server as `rune lsp`, so it needs the Rune toolchain:

```bash
cmake -S . -B build -G Ninja && cmake --build build
export PATH="$PWD/build/bin:$PATH"
```

If `rune` is not on `PATH` for VS Code, point `rune.server.path` at it (or at `rune-lsp`
directly).

To install the extension from this folder:

```bash
cd editors/vscode
npm install
npx @vscode/vsce package
code --install-extension rune-lang-0.1.0.vsix
```

## Settings

| Setting | Default | |
| --- | --- | --- |
| `rune.server.path` | `""` | `rune` (started as `rune lsp`) or `rune-lsp`; empty means `rune` on `PATH` |
| `rune.server.extraArgs` | `[]` | further arguments for the server |
| `rune.stdlib` | `""` | the directory holding `std/`; empty means the toolchain's own |
| `rune.checkOnSave` | `true` | run the compiler on open and save |
| `rune.check.onChange` | `true` | also run it once typing pauses, over unsaved text |
| `rune.check.delay` | `400` | that pause, in milliseconds |
| `rune.check.memory` | `"zombie"` | `"arc"` checks files outside a package with reference counting instead of the borrow checker |
| `rune.inlayHints.types` | `true` | show inferred types; double-click to write one in |
| `rune.inlayHints.parameters` | `true` | show argument labels; double-click to write one in |
| `rune.format.types`, `.labels`, `.reorder`, `.lintFixes` | `true` | what Format Document does |
| `rune.lint.enable` | `true` | show lint findings as you type |
| `rune.lint.allow` | `[]` | rules to turn off, e.g. `["naming"]` |
| `rune.lint.warn` | `[]` | rules to turn on, e.g. `["missing-docs"]` |
| `rune.lint.maxLineLength` | `0` | the `line-too-long` limit; `0` keeps the package's, or 120 |
| `rune.trace.server` | `"off"` | log the protocol to the output channel |

A package can configure the linter for everyone who works on it, in `Rune.toml`:

```toml
[lint]
allow = ["line-too-long"]
warn = ["missing-docs"]
max-line-length = 100
```

and a single declaration, statement or whole file with an `#lint` directive:

```rune
#lint(allow(naming))             // at the top, before any declaration: the file
import std::io

#lint(allow(unused-variable))    // anywhere else: just the next item
let spare = compute()
```

## Commands

- **Rune: Restart Language Server**
- **Rune: Show Language Server Output**
- **Rune: Fix All Auto-fixable Lint Problems**

## More

`rune doc lsp` opens the language server's book — every feature, the settings
other editors pass, and troubleshooting — and `rune doc lint` the linter's, with
every rule described.

## Developing

`npm test` checks the grammar with the TextMate engine VS Code uses: named tokens in a
sample get the scopes they should, and every `.rune` file in the repository tokenises
with nothing left open. The server has its own tests in `tests/tooling_test.py`.

## Licence

Distributed under the same terms as the Rune repository it comes from.
