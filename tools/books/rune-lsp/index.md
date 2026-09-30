# The Rune language server

`rune lsp` is a language server: a program an editor starts in the background
and asks about the code being edited, over the
[Language Server Protocol](https://microsoft.github.io/language-server-protocol/).
Any editor with an LSP client can use it — VS Code through the extension in
`editors/vscode`, Vim and Neovim through `editors/vim`, and Helix, Emacs and others through their
own clients.

What it gives an editor:

| | |
| --- | --- |
| **Diagnostics** | everything the compiler reports — type errors, the borrow checker's, macro expansion — and the linter's findings, as you type |
| **Completion** | locals in scope, the file's declarations, a module's exports after `io::`, a value's members after `.`, intrinsics after `.$`, variants after `Shape::`, decorators after `@`, module paths after `import` |
| **Hover** | a declaration's signature and documentation; a local's inferred type |
| **Go to definition** | into the file, the package's other modules, its dependencies and the standard library |
| **References** and **highlights** | every use of a name, in the open files |
| **Outline** | the file's declarations, with each type's members beneath it |
| **Signature help** | the parameters of the call being typed, with the current one marked |
| **Quick fixes** | the linter's fixes, one at a time or all at once |

## Starting it

An editor starts it, not you — but it is an ordinary command:

```sh
rune lsp
```

It reads requests on standard input and writes answers on standard output, and
runs until the editor tells it to stop. `rune lsp` starts `rune-lsp` with the
toolchain's own standard library and compilers; see
[Configuring it](configuration.md) for running `rune-lsp` directly.

## The rest of this book

- [Editors](editors.md) — setting it up in VS Code, Neovim, Helix and Emacs.
- [Features](features.md) — each capability in detail, and where its answers come
  from.
- [Configuring it](configuration.md) — command-line flags, and the settings an
  editor passes.
- [How it works](how-it-works.md) — the index, the compiler runs, and the limits.
- [Troubleshooting](troubleshooting.md) — when something does not appear.

The lint findings it shows are described rule by rule in the linter's own book,
`rune doc lint`.
