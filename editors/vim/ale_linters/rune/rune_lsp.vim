" ALE: `rune lsp`, the Rune language server — diagnostics from the compiler
" and the linter as you type, completion, hover and go to definition.

function! ale_linters#rune#rune_lsp#Root(buffer) abort
  let root = rune#root(expand('#' . a:buffer . ':p'))
  return empty(root) ? expand('#' . a:buffer . ':p:h') : root
endfunction

call ale#linter#Define('rune', {
\   'name': 'rune_lsp',
\   'lsp': 'stdio',
\   'executable': {b -> rune#rune()},
\   'command': '%e lsp',
\   'project_root': function('ale_linters#rune#rune_lsp#Root'),
\})
