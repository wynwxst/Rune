" Rune: checking files as they are opened, saved and edited.
"
"   :RuneCheck        check the current file (or its package) now
"   :RuneClear        forget what the checker found in this buffer
"
" g:rune_check            0 turns the checker off (default 1); with ALE
"                         installed it is off and ALE runs `rune lsp`
"                         instead, unless this is 2
" g:rune_check_on_change  0 checks only on open and save (default 1)
" g:rune_check_delay      pause in typing, in ms, before checking (400)
" g:rune_lint             0 leaves the linter out (default 1)
" g:rune_memory           'zombie' checks files outside a package under the
"                         borrow checker (default 'arc'); a package's
"                         Rune.toml decides for its own files
" g:rune_rune, g:rune_runec   the toolchain, if not on PATH
"
" In Neovim the language server does all of this when it is running; see
" plugin/rune.lua.

if exists('g:loaded_rune')
  finish
endif
let g:loaded_rune = 1

command! -bar RuneCheck call rune#check(0, 1)
command! -bar RuneClear call rune#clear(0)

augroup rune_check
  autocmd!
  autocmd BufReadPost,BufWritePost *.rune call rune#check(0, 0)
  autocmd TextChanged,InsertLeave *.rune call rune#checkLater(0)
  autocmd TextChangedI *.rune call rune#checkLater(0)
  autocmd CursorMoved,CursorHold *.rune call rune#echo()
  autocmd BufWinEnter *.rune call setloclist(0, [], 'r', {'title': 'Rune', 'items': rune#items(bufnr('%'))})
augroup END
