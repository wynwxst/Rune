# Rune for Vim and Neovim

A runtime directory: filetype detection, syntax highlighting, indentation,
comment settings, `:make` into the quickfix list, and errors shown as you type —
type errors, the borrow checker's, macro expansion, and the linter's findings.

```vim
" vim-plug
Plug '~/path/to/Rune/editors/vim'
```

```lua
-- lazy.nvim
{ dir = "~/path/to/Rune/editors/vim" }
```

Or `set runtimepath^=~/path/to/Rune/editors/vim` followed by
`filetype plugin indent on` and `syntax on`.

- **Neovim** starts the language server, `rune lsp`, for Rune files: completion,
  hover, go to definition, references, outline, quick fixes, and diagnostics as
  you type. `vim.g.rune_lsp = 0` turns that off.
- **Vim** runs `rune check` and `rune lint` in the background on open, on save
  and when typing pauses, and shows the results as signs, underlines, a message
  under the cursor and the location list. With ALE installed, ALE runs
  `rune lsp` instead.
- **`:make`** checks the file, or its package, anywhere.

`rune` (and `runec`, for files outside a package) must be on `PATH`, or named
with `g:rune_rune` and `g:rune_runec`. Borrow-checker errors need
`--memory zombie`: a package's `Rune.toml` says so with `[build] memory`, and
`g:rune_memory = 'zombie'` does for loose files.

`:help rune` has the details, as does `rune doc lsp`.
