" Vim compiler file
" Compiler: Rune (`rune check` in a package, `runec --check` for a file)
"
" `:make` checks without building and fills the quickfix list with every error
" the compiler reports — type errors, the borrow checker's under
" `--memory zombie`, macro expansion — and the notes that go with them.

if exists('current_compiler')
  finish
endif
let current_compiler = 'rune'

if exists(':CompilerSet') != 2
  command -nargs=* CompilerSet setlocal <args>
endif

let s:cpo_save = &cpo
set cpo&vim

execute 'CompilerSet makeprg=' . escape(rune#makeprg(), ' \|"')

CompilerSet errorformat=
      \%-G○\ %.%#,
      \%-G●\ Build\ failed%.%#,
      \%f:%l:%c:\ %trror:\ %m,
      \%f:%l:%c:\ %tarning:\ %m,
      \%f:%l:%c:\ %tote:\ %m,
      \%f:%l:%c:\ fatal:\ %m,
      \%f:%l:%c:\ remark:\ %m,
      \runec:\ %trror:\ %m,
      \runec:\ fatal:\ %m,
      \●\ %m,
      \%-G%.%#

let &cpo = s:cpo_save
unlet s:cpo_save
