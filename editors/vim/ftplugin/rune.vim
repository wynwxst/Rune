" Vim filetype plugin
" Language: Rune

if exists('b:did_ftplugin')
  finish
endif
let b:did_ftplugin = 1

let s:cpo_save = &cpo
set cpo&vim

setlocal comments=s1:/*,mb:*,ex:*/,:///,://
setlocal commentstring=//\ %s
setlocal formatoptions-=t formatoptions+=croqnlj
" Rune is indented with four spaces; `rune lint` reports tabs.
setlocal expandtab shiftwidth=4 softtabstop=4 tabstop=4
setlocal suffixesadd=.rune
setlocal textwidth=120

" `:make` checks the file, or its package, and fills the quickfix list.
compiler rune

let b:undo_ftplugin = 'setlocal comments< commentstring< formatoptions< expandtab< shiftwidth<'
      \ . ' softtabstop< tabstop< suffixesadd< textwidth< makeprg< errorformat<'

let &cpo = s:cpo_save
unlet s:cpo_save
