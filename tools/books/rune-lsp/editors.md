# Editors

Every editor needs the same three things: to recognise `.rune` files, to start
`rune lsp` for them, and — ideally — to know that a directory with a `Rune.toml`
is a project root. `rune` has to be on the `PATH` the editor sees, or named by
its full path.

## VS Code

The extension in `editors/vscode` is the complete setup: syntax highlighting, the
language server, and commands. Build and install it from the repository:

```sh
cd editors/vscode
npm install
npx @vscode/vsce package --allow-missing-repository --skip-license
code --install-extension rune-lang-0.1.0.vsix
```

It starts `rune lsp` when a `.rune` file is opened or a folder with a `Rune.toml`
is. Its settings:

| Setting | Default | |
| --- | --- | --- |
| `rune.server.path` | `""` | `rune` (started as `rune lsp`) or `rune-lsp` itself; empty means `rune` on `PATH` |
| `rune.server.extraArgs` | `[]` | further arguments for the server |
| `rune.stdlib` | `""` | the directory holding `std/`; empty means the toolchain's |
| `rune.checkOnSave` | `true` | run the compiler on open and save |
| `rune.check.onChange` | `true` | also run it once typing pauses, over unsaved text |
| `rune.check.delay` | `400` | that pause, in milliseconds |
| `rune.check.memory` | `"arc"` | `"zombie"` checks files outside a package with the borrow checker |
| `rune.lint.enable` | `true` | show lint findings |
| `rune.lint.allow` | `[]` | lint rules to turn off |
| `rune.lint.warn` | `[]` | lint rules to turn on |
| `rune.lint.maxLineLength` | `0` | the `line-too-long` limit; `0` keeps the package's, or 120 |
| `rune.trace.server` | `"off"` | log the protocol to the output channel |

And its commands, from the command palette: **Rune: Restart Language Server**,
**Rune: Show Language Server Output**, and **Rune: Fix All Auto-fixable Lint
Problems**.

> [!NOTE]
> VS Code started from the Dock or Finder reads `PATH` from your login shell.
> If `rune` is added to `PATH` somewhere that shell does not read, set
> `rune.server.path` to its full path instead.

## Vim and Neovim

`editors/vim` is a runtime directory for both: filetype detection, syntax
highlighting (`@lint(...)` included), indentation, comment settings, a `:make`
that fills the quickfix list, and checking as you type. Add it to the
`runtimepath` — with a plugin manager, point it at the directory:

```vim
" vim-plug
Plug '~/path/to/Rune/editors/vim'
```

```lua
-- lazy.nvim
{ dir = "~/path/to/Rune/editors/vim" }
```

or by hand, in `~/.vimrc` or `init.vim`:

```vim
set runtimepath^=~/path/to/Rune/editors/vim
filetype plugin indent on
syntax on
```

**Neovim** starts `rune lsp` for Rune files by itself (with `vim.lsp.enable` on
0.11 and later, `vim.lsp.start` before), so everything in
[Features](features.md) works, and diagnostics appear with Neovim's own
underlines, signs and virtual text. Settings go in `vim.g` before the plugin
loads, or through `vim.lsp.config` for anything else:

```lua
vim.g.rune_memory = "zombie"          -- check loose files with the borrow checker
vim.lsp.config("rune", {
  init_options = { lint = { warn = { "missing-docs" } } },
})
```

`vim.g.rune_lsp = 0` stops it starting the server — for nvim-lspconfig, or to
use the checker below instead. `require("rune").config()` returns the
configuration it would use, to hand to another setup.

**Vim** has no language client of its own, so the plugin checks files itself: on
open and save, and once typing pauses, it runs `rune check` (or `runec --check`)
and `rune lint` in the background and shows what they find — a sign in the
column, the range underlined, the message echoed when the cursor is on it, and
everything in the location list (`:lopen`). Unsaved text is checked through
`--source`, as the language server does it.

| Variable | Default | |
| --- | --- | --- |
| `g:rune_check` | `1` | `0` turns the checker off |
| `g:rune_check_on_change` | `1` | `0` checks only on open and save |
| `g:rune_check_delay` | `400` | the pause in typing, in milliseconds |
| `g:rune_lint` | `1` | `0` leaves the linter out |
| `g:rune_memory` | `"arc"` | `"zombie"` checks files outside a package with the borrow checker |
| `g:rune_rune`, `g:rune_runec` | on `PATH` | the toolchain's programs |

`:RuneCheck` checks now and `:RuneClear` clears the buffer's findings.

With [ALE](https://github.com/dense-analysis/ale) installed, the checker stands
aside and ALE runs `rune lsp` through the `rune_lsp` linter the directory
provides; with vim-lsp, register the server yourself:

```vim
autocmd User lsp_setup call lsp#register_server({
      \ 'name': 'rune',
      \ 'cmd': {_ -> ['rune', 'lsp']},
      \ 'allowlist': ['rune'],
      \ 'root_uri': {_ -> lsp#utils#path_to_uri(rune#root(expand('%:p')))},
      \ })
```

`:make` works everywhere, with or without any of this: it runs the compiler with
`--diagnostic-format short`, which prints one `file:line:column: error: message`
line per diagnostic, with its notes and related places as `note:` lines after it.

## Helix

In `~/.config/helix/languages.toml`:

```toml
[[language]]
name = "rune"
scope = "source.rune"
file-types = ["rune"]
roots = ["Rune.toml"]
comment-token = "//"
indent = { tab-width = 4, unit = "    " }
language-servers = ["rune-lsp"]

[language-server.rune-lsp]
command = "rune"
args = ["lsp"]
```

## Emacs

With Eglot, which comes with Emacs 29:

```elisp
(define-derived-mode rune-mode prog-mode "Rune"
  (setq-local comment-start "// "))
(add-to-list 'auto-mode-alist '("\\.rune\\'" . rune-mode))

(with-eval-after-load 'eglot
  (add-to-list 'eglot-server-programs '(rune-mode "rune" "lsp")))
```

Then `M-x eglot` in a Rune buffer, or `(add-hook 'rune-mode-hook #'eglot-ensure)`.

## Anything else

Any LSP client will do. Start `rune lsp` over standard input and output for files
whose language is `rune`, and use `Rune.toml` as the root marker. The server needs
no workspace folder: it finds a file's package by looking for `Rune.toml` in the
file's directory and those above it.
