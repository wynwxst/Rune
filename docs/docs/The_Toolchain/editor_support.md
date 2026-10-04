# Editor support

Three tools ship with the toolchain, all written in Rune and all asking the compiler rather than guessing: `rune lint`, `rune fmt` and `rune lsp`. Built with the toolchain, they are copied to `~/.rune/bin` (or `$RUNE_HOME/bin`), where `rune` and an editor find them; `rune tools` lists them and says where each one is. Each has a book of its own — `rune doc lint`, `rune doc fmt`, `rune doc lsp` — and what follows is the outline.

| Tool | What it does |
| --- | --- |
| `rune lint` | twenty rules — unused bindings, parameters and imports, dead and unreachable code, `x == true`, naming, empty blocks, needless `return`, line length, missing docs, `TODO`s — at `warn`, `note` or `allow`; `--fix` applies what is safe, and `--list` shows every rule |
| `rune fmt` | one layout: the linter's style fixes, then the types of bindings and the labels of arguments written in from `--query-hints`, then the top of the file in order and the indentation. It re-checks with the compiler and leaves a file alone rather than break it |
| `rune lsp` | diagnostics as you type (unsaved buffers go to the compiler through `--source`), completion, inlay hints, hover, go to definition, references, outline, signature help, quick fixes and Format Document |

Lint rules are set in four places, each over the one before: the package's `[lint]` table, the command line or the editor's settings, an `#lint(...)` at the top of a file, and an `#lint(...)` before one declaration or statement.

```sh
# Rune.toml
[lint]
allow = ["line-too-long"]
warn = ["missing-docs", "todo"]
max-line-length = 100
```

**`#lint` on one item**

```rune
#lint(allow(naming))           // just this function
fn Parse_Header() -> i64 { 0 }

fn main() -> i64 {
    #lint(allow(unused-variable))
    let spare = 1
    Parse_Header()
}
```

| Editor | Setup, in `editors/` |
| --- | --- |
| VS Code | `editors/vscode`: grammar, the language server and commands, packaged as a `.vsix` |
| Vim and Neovim | `editors/vim`: syntax, indent, `:make` through the compiler, an asynchronous checker for Vim, and `rune lsp` started for Neovim (and ALE) |
| Helix | `editors/helix`: a tree-sitter grammar, with `rune lsp` as the language server |
| anything else | start `rune lsp` for `.rune` files, with the directory holding `Rune.toml` as the root |
