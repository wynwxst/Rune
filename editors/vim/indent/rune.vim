" Vim indent file
" Language: Rune
"
" Rune has no semicolons, so C indenting would take every line for a
" continuation of the one before. This indents after a line that ends in an
" opening bracket and dedents a line that starts with a closing one — the same
" rules the VS Code extension uses.

if exists('b:did_indent')
  finish
endif
let b:did_indent = 1

setlocal nolisp nosmartindent nocindent autoindent
setlocal indentexpr=GetRuneIndent(v:lnum)
setlocal indentkeys=0{,0},0),0],!^F,o,O,e
let b:undo_indent = 'setlocal lisp< smartindent< cindent< autoindent< indentexpr< indentkeys<'

if exists('*GetRuneIndent')
  finish
endif

" The code on `lnum`, with strings emptied and a trailing comment removed.
function! s:Code(lnum) abort
  let line = getline(a:lnum)
  let line = substitute(line, '"\%([^"\\]\|\\.\)*"', '""', 'g')
  let line = substitute(line, "'\\%([^'\\\\]\\|\\\\.\\)*'", "''", 'g')
  let line = substitute(line, '//.*$', '', '')
  let line = substitute(line, '/\*.\{-}\*/', '', 'g')
  return trim(line)
endfunction

function! GetRuneIndent(lnum) abort
  let prev = prevnonblank(a:lnum - 1)
  " Inside a block comment, keep to its column.
  while prev > 0 && getline(prev) =~# '^\s*\*'
    let prev = prevnonblank(prev - 1)
  endwhile
  if prev == 0
    return 0
  endif
  let ind = indent(prev)
  let before = s:Code(prev)
  if before =~# '[\[({]$'
    let ind += shiftwidth()
  endif
  if s:Code(a:lnum) =~# '^[\])}]'
    let ind -= shiftwidth()
  endif
  return ind < 0 ? 0 : ind
endfunction
