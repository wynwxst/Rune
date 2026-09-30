" Vim syntax file
" Language: Rune
" Kept in step with editors/vscode/syntaxes/rune.tmLanguage.json.

if exists('b:current_syntax')
  finish
endif

let s:cpo_save = &cpo
set cpo&vim

syn case match

" Operators first: an item defined later wins where both start, and `/`
" must lose to `//` and `/*`.
syn match   runeOperator  "->\|=>\|??\|\.\.[.=]\=\|[-+*/%&|^!=<>~?]"
syn match   runeSep       "::"

" Comments. Block comments nest.
syn keyword runeTodo contained TODO FIXME XXX NOTE
syn match   runeDocCode contained "`[^`]*`"
syn region  runeDocComment start="///\%(/\)\@!" end="$" contains=runeTodo,runeDocCode,@Spell
syn region  runeLineComment start="//" end="$" contains=runeTodo,@Spell
syn region  runeBlockComment start="/\*" end="\*/" contains=runeBlockComment,runeTodo,@Spell

" Strings: "…" (with {} placeholders and escapes), """…""" and r#"…"#.
syn match   runeEscape contained "\\\%([ntr0\\\"'e{}]\|x\x\{2}\|u{\x\{1,6}}\)"
syn match   runePlaceholder contained "{[A-Za-z0-9_.:]*}"
syn match   runePlaceholder contained "{{\|}}"
syn region  runeString start=+"+ skip=+\\\\\|\\"+ end=+"\|$+ contains=runeEscape,runePlaceholder,@Spell
syn region  runeString start=+"""+ end=+"""+ contains=runeEscape,runePlaceholder,@Spell
syn region  runeRawString matchgroup=runeRawDelim start=+\<r\z(#*\)"+ end=+"\z1+ contains=@Spell
syn match   runeCharacter "'\%(\\\%([ntr0\\\"'e]\|x\x\{2}\|u{\x\{1,6}}\)\|[^\\']\)'" contains=runeEscape

" Numbers, with an optional type suffix.
syn match   runeNumber "\<0[xX][0-9A-Fa-f_]\+\%([iu]\%(8\|16\|32\|64\|size\)\)\=\>"
syn match   runeNumber "\<0[bB][01_]\+\%([iu]\%(8\|16\|32\|64\|size\)\)\=\>"
syn match   runeNumber "\<0[oO][0-7_]\+\%([iu]\%(8\|16\|32\|64\|size\)\)\=\>"
syn match   runeNumber "\<\d[0-9_]*\%([iu]\%(8\|16\|32\|64\|size\)\|f32\|f64\)\=\>"
syn match   runeFloat  "\<\d[0-9_]*\.\d[0-9_]*\%([eE][+-]\=\d[0-9_]*\)\=\%(f32\|f64\)\=\>"
syn match   runeFloat  "\<\d[0-9_]*[eE][+-]\=\d[0-9_]*\%(f32\|f64\)\=\>"

" Keywords.
syn keyword runeConditional if elif else match
syn keyword runeRepeat      while loop for in
syn keyword runeFlow        return break continue defer await
syn keyword runeKeyword     fn nextgroup=runeFuncName skipwhite
syn keyword runeStructure   class struct enum mark nextgroup=runeTypeName skipwhite
syn keyword runeStructure   type
syn keyword runeModifier    pub var let mut global extern unsafe async weak uniq move dyn some ref
syn keyword runeKeyword     bind to into extend operator where as is super
syn keyword runeKeyword     macro nextgroup=runeMacroName skipwhite
syn keyword runeInclude     import nextgroup=runeModPath skipwhite
syn keyword runeSelf        self Self
syn keyword runeBoolean     true false
syn keyword runeNil         nil
syn keyword runePrimitive   i8 i16 i32 i64 isize u8 u16 u32 u64 usize f32 f64 bool
syn keyword runeBuiltinType String CString Character Never Any Option Result
syn keyword runeCoreConst   Some None Ok Err

syn match   runeFuncName  contained "\h\w*"
syn match   runeTypeName  contained "\h\w*"
syn match   runeMacroName contained "\h\w*"
syn match   runeModPath   contained "\h\w*\%(::\h\w*\)*"

" SCREAMING_CASE constants before other capitalised names, which are types.
syn match   runeType      "\<\u\w*\>"
syn match   runeConstant  "\<\u[A-Z0-9]*_[A-Z0-9_]*\>"
syn match   runeConstant  "\<\u\{2,}\d*\>"

" `name!(…)`, `.$length()`, `name(…)`, `path::`.
syn match   runeMacroCall "\<\h\w*!\ze\s*[(\[{]"
syn match   runeIntrinsic "\.\zs\$\h\w*"
syn match   runeFuncCall  "\<\l\w*\ze\s*("
syn match   runeFuncCall  "\<_\w*\ze\s*("
syn match   runePath      "\<\h\w*\ze::"

" Decorators and the linter's directive: `@lint(allow(rule, …), warn(…))`.
syn match   runeDecorator "@\h\w*"
" Defined after it, so it wins: `@function(…)` is a type, not a decorator.
syn match   runeFnType    "@c\=function\>"
syn region  runeLint matchgroup=runeDecorator start="@lint\s*(" end=")" contains=runeLintLevel,runeLintComma,runeLineComment,runeBlockComment
syn region  runeLintLevel contained matchgroup=runeLintKeyword start="\<\%(allow\|warn\|note\)\s*(" end=")" contains=runeLintAll,runeLintRule,runeLintComma
syn match   runeLintRule  contained "\a[A-Za-z0-9_-]*"
syn keyword runeLintAll   contained all
syn match   runeLintComma contained ","


hi def link runeTodo         Todo
hi def link runeDocComment   SpecialComment
hi def link runeDocCode      SpecialComment
hi def link runeLineComment  Comment
hi def link runeBlockComment Comment
hi def link runeString       String
hi def link runeRawString    String
hi def link runeRawDelim     String
hi def link runeCharacter    Character
hi def link runeEscape       SpecialChar
hi def link runePlaceholder  SpecialChar
hi def link runeNumber       Number
hi def link runeFloat        Float
hi def link runeConditional  Conditional
hi def link runeRepeat       Repeat
hi def link runeFlow         Statement
hi def link runeKeyword      Keyword
hi def link runeStructure    Structure
hi def link runeModifier     StorageClass
hi def link runeInclude      Include
hi def link runeSelf         Identifier
hi def link runeBoolean      Boolean
hi def link runeNil          Constant
hi def link runePrimitive    Type
hi def link runeBuiltinType  Type
hi def link runeType         Type
hi def link runeTypeName     Typedef
hi def link runeCoreConst    Constant
hi def link runeConstant     Constant
hi def link runeFuncName     Function
hi def link runeFuncCall     Function
hi def link runeMacroName    Macro
hi def link runeMacroCall    Macro
hi def link runeIntrinsic    Special
hi def link runeModPath      Identifier
hi def link runePath         Identifier
hi def link runeFnType       Type
hi def link runeDecorator    PreProc
hi def link runeLintKeyword  Keyword
hi def link runeLintRule     Constant
hi def link runeLintAll      Constant
hi def link runeOperator     Operator
hi def link runeSep          Delimiter

let b:current_syntax = 'rune'

let &cpo = s:cpo_save
unlet s:cpo_save
