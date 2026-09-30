" Rune support for Vim and Neovim: finding a file's package, the command
" that checks it, and a checker that runs in the background and shows what
" the compiler and the linter say — in the location list, as signs, and
" underlined where they point.
"
" Neovim with the language server running (see plugin/rune.lua) has all of
" this from the server instead, and the checker stays out of its way.

let s:cpo_save = &cpo
set cpo&vim

"=== Settings ===============================================================

function! s:Get(name, default) abort
  return get(b:, 'rune_' . a:name, get(g:, 'rune_' . a:name, a:default))
endfunction

function! rune#rune() abort
  return s:Get('rune', 'rune')
endfunction

function! rune#runec() abort
  return s:Get('runec', 'runec')
endfunction

"=== Packages ===============================================================

" The directory holding the `Rune.toml` above `path`, or ''.
function! rune#root(path) abort
  let dir = fnamemodify(a:path, ':p:h')
  let manifest = findfile('Rune.toml', fnameescape(dir) . ';')
  return empty(manifest) ? '' : fnamemodify(manifest, ':p:h')
endfunction

" What `:make` runs for the current buffer: `rune check` from its package's
" root, or `runec --check` on the file alone.
function! rune#makeprg() abort
  let root = rune#root(expand('%:p'))
  if !empty(root)
    return 'cd ' . shellescape(root) . ' && ' . rune#rune() . ' check --diagnostic-format short'
  endif
  return rune#runec() . ' --check --diagnostic-format short --memory ' . s:Get('memory', 'arc') . ' %'
endfunction

"=== The checker ============================================================

" The last findings for each buffer, as location-list items; kept apart so
" a compile does not throw away the linter's findings, or the reverse.
let s:found = {}
let s:jobs = {}
let s:timers = {}
let s:sequence = 0

function! s:Enabled(buf) abort
  if !s:Get('check', 1) || getbufvar(a:buf, '&filetype') !=# 'rune'
    return 0
  endif
  " Neovim's language server, or ALE running it, reports the same things.
  if has('nvim') && (get(g:, 'rune_lsp_active', 0)
        \ || luaeval('#vim.lsp.get_clients({bufnr = _A, name = "rune"}) > 0', a:buf))
    return 0
  endif
  if exists('g:loaded_ale') && get(g:, 'ale_enabled', 1) && s:Get('check', 1) != 2
    return 0
  endif
  return 1
endfunction

" Checks `buf` now. `unsaved` checks the buffer as it stands rather than the
" file on disk, which is how checking as you type works.
function! rune#check(buf, unsaved) abort
  let buf = a:buf == 0 ? bufnr('%') : a:buf
  if !s:Enabled(buf)
    return
  endif
  let path = fnamemodify(bufname(buf), ':p')
  if empty(path) || !filereadable(path)
    return
  endif
  let root = rune#root(path)
  let s:sequence += 1
  let run = {'buf': buf, 'path': path, 'seq': s:sequence, 'pending': 0}

  let compile = empty(root)
        \ ? [rune#runec(), '--check', '--diagnostic-format', 'json', '--memory', s:Get('memory', 'arc')]
        \ : [rune#rune(), 'check', '--diagnostic-format', 'json']
  let scratch = ''
  if a:unsaved && getbufvar(buf, '&modified')
    let scratch = tempname() . '.rune'
    call writefile(getbufline(buf, 1, '$'), scratch)
    let compile += ['--source', path . '=' . scratch]
  endif
  if empty(root)
    let compile += [path]
  endif
  let run.scratch = scratch
  call s:Start(run, 'compile', compile, empty(root) ? fnamemodify(path, ':h') : root)
  " The linter reads files from disk: it runs when there is something new
  " there, and what it said last stands in the meantime.
  if s:Get('lint', 1) && (!a:unsaved || !getbufvar(buf, '&modified'))
    let lint = [rune#rune(), 'lint', '--format', 'json', path]
    call s:Start(run, 'lint', lint, empty(root) ? fnamemodify(path, ':h') : root)
  endif
endfunction

" Checks `buf` once typing has paused for `g:rune_check_delay` milliseconds.
function! rune#checkLater(buf) abort
  if !s:Get('check_on_change', 1) || !exists('*timer_start')
    return
  endif
  let buf = a:buf == 0 ? bufnr('%') : a:buf
  if has_key(s:timers, buf)
    call timer_stop(s:timers[buf])
  endif
  let s:timers[buf] = timer_start(s:Get('check_delay', 400), {-> s:Fire(buf)})
endfunction

function! s:Fire(buf) abort
  silent! unlet s:timers[a:buf]
  if bufexists(a:buf)
    call rune#check(a:buf, 1)
  endif
endfunction

function! s:Start(run, kind, cmd, cwd) abort
  if !executable(a:cmd[0])
    if !get(s:, 'warned_' . a:kind, 0)
      let s:['warned_' . a:kind] = 1
      echohl WarningMsg | echomsg 'rune: `' . a:cmd[0] . '` is not on PATH; set g:rune_rune or g:rune_runec' | echohl None
    endif
    return
  endif
  let key = a:run.buf . ':' . a:kind
  call s:Stop(key)
  let a:run.pending += 1
  let state = {'run': a:run, 'kind': a:kind, 'lines': [], 'key': key, 'stopped': 0}
  if has('nvim')
    let job = jobstart(a:cmd, {
          \ 'cwd': a:cwd,
          \ 'stdout_buffered': 1, 'stderr_buffered': 1,
          \ 'on_stdout': {_, data, __ -> extend(state.lines, data)},
          \ 'on_stderr': {_, data, __ -> extend(state.lines, data)},
          \ 'on_exit': {_, status, __ -> s:Done(state, status)},
          \ })
  else
    let job = job_start(a:cmd, {
          \ 'cwd': a:cwd, 'in_io': 'null',
          \ 'out_cb': {_, line -> add(state.lines, line)},
          \ 'err_cb': {_, line -> add(state.lines, line)},
          \ 'exit_cb': {_, status -> s:Done(state, status)},
          \ })
  endif
  let s:jobs[key] = {'job': job, 'state': state}
endfunction

function! s:Stop(key) abort
  if !has_key(s:jobs, a:key)
    return
  endif
  let entry = remove(s:jobs, a:key)
  let entry.state.stopped = 1
  let job = entry.job
  if has('nvim')
    silent! call jobstop(job)
  elseif job_status(job) ==# 'run'
    call job_stop(job)
  endif
endfunction

let s:types = {'error': 'E', 'fatal': 'E', 'warning': 'W', 'note': 'I', 'remark': 'I'}

" `d`'s place as location-list fields: 1-based lines and columns.
function! s:Place(d) abort
  let line = get(a:d, 'line', 1)
  let col = get(a:d, 'column', 0) + 1
  let end_line = get(a:d, 'endLine', line)
  let end_col = get(a:d, 'endColumn', col - 1) + 1
  if end_line < line || (end_line == line && end_col <= col)
    let end_line = line
    let end_col = col + 1
  endif
  return {'filename': get(a:d, 'file', ''), 'lnum': line, 'col': col,
        \ 'end_lnum': end_line, 'end_col': end_col}
endfunction

function! s:Done(state, status) abort
  let run = a:state.run
  let run.pending -= 1
  if run.pending == 0 && !empty(run.scratch)
    call delete(run.scratch)
  endif
  if a:state.stopped
    " Stopped for a newer run, which will say what is true now.
    return
  endif
  silent! unlet s:jobs[a:state.key]
  let items = []
  let words = []
  let source = a:state.kind ==# 'lint' ? 'rune-lint' : 'runec'
  for line in a:state.lines
    let line = substitute(line, '\r$', '', '')
    if line !~# '^{'
      if !empty(line) && line !~# '^○' && line !~# '^● Build failed'
        call add(words, substitute(line, '^● ', '', ''))
      endif
      continue
    endif
    try
      let d = json_decode(line)
    catch
      continue
    endtry
    let item = s:Place(d)
    if empty(item.filename)
      let item.filename = run.path
    endif
    let item.type = get(s:types, get(d, 'severity', 'error'), 'E')
    let text = get(d, 'message', '')
    for n in get(d, 'notes', [])
      let text .= ' — note: ' . n
    endfor
    if has_key(d, 'code')
      let text .= ' [' . d.code . ']'
    endif
    let item.text = text
    let item.source = source
    call add(items, item)
    " Where a value was moved, borrowed or declared: marked too.
    for r in get(d, 'related', [])
      let rel = s:Place(r)
      if empty(rel.filename)
        continue
      endif
      let rel.type = 'N'
      let rel.text = get(r, 'message', '') . ' (see: ' . get(d, 'message', '') . ')'
      let rel.source = source
      call add(items, rel)
    endfor
  endfor
  if empty(items) && a:status != 0 && a:state.kind ==# 'compile'
    " It failed and said why only in words.
    call add(items, {'filename': run.path, 'lnum': 1, 'col': 1, 'end_lnum': 1, 'end_col': 2,
          \ 'type': 'E', 'source': source,
          \ 'text': empty(words) ? 'the check failed with status ' . a:status : join(words, ' ')})
  endif
  " A run that a newer one has overtaken says nothing.
  let latest = get(getbufvar(run.buf, '', {}), 'rune_check_seq', 0)
  if run.seq < latest
    return
  endif
  call setbufvar(run.buf, 'rune_check_seq', run.seq)
  call s:Record(run, a:state.kind, items)
endfunction

" Keeps `items` as what `kind` last said about the files it names, and shows
" everything known for each of them.
function! s:Record(run, kind, items) abort
  let byBuf = {}
  let byBuf[a:run.buf] = []
  for item in a:items
    let b = bufnr(item.filename)
    if b < 0
      " A file that is not open: its findings stay in the location list.
      let b = a:run.buf
    endif
    let byBuf[b] = add(get(byBuf, b, []), item)
  endfor
  for [b, list] in items(byBuf)
    let b = str2nr(b)
    let s:found[b] = get(s:found, b, {})
    let s:found[b][a:kind] = list
    call s:Show(b)
  endfor
endfunction

function! rune#items(buf) abort
  let all = []
  for list in values(get(s:found, a:buf, {}))
    call extend(all, list)
  endfor
  return sort(all, {a, b -> a.lnum == b.lnum ? a.col - b.col : a.lnum - b.lnum})
endfunction

function! rune#clear(buf) abort
  let buf = a:buf == 0 ? bufnr('%') : a:buf
  silent! unlet s:found[buf]
  call s:Show(buf)
endfunction

"=== Showing it ==============================================================

let s:severity = {'E': 1, 'W': 2, 'I': 3, 'N': 4}

function! s:Show(buf) abort
  let items = rune#items(a:buf)
  if has('nvim')
    call luaeval('require("rune").show(_A[1], _A[2])', [a:buf, items])
  else
    call s:Signs(a:buf, items)
    call s:Underline(a:buf, items)
  endif
  " The location list of every window showing the buffer.
  for win in win_findbuf(a:buf)
    call setloclist(win, [], 'r', {'title': 'Rune', 'items': items})
  endfor
  if a:buf == bufnr('%')
    call rune#echo()
  endif
endfunction

function! s:DefineVim() abort
  if get(s:, 'defined', 0)
    return
  endif
  let s:defined = 1
  highlight default RuneErrorSign   ctermfg=Red     guifg=#e5484d
  highlight default RuneWarningSign ctermfg=Yellow  guifg=#e5a000
  highlight default RuneInfoSign    ctermfg=Blue    guifg=#3e8ed0
  highlight default RuneError   term=undercurl cterm=undercurl gui=undercurl ctermul=Red    guisp=#e5484d
  highlight default RuneWarning term=undercurl cterm=undercurl gui=undercurl ctermul=Yellow guisp=#e5a000
  highlight default RuneInfo    term=underline cterm=underline gui=underline ctermul=Blue   guisp=#3e8ed0
  highlight default link RuneHint RuneInfo
  call sign_define('RuneE', {'text': 'E>', 'texthl': 'RuneErrorSign'})
  call sign_define('RuneW', {'text': 'W>', 'texthl': 'RuneWarningSign'})
  call sign_define('RuneI', {'text': 'I>', 'texthl': 'RuneInfoSign'})
  call sign_define('RuneN', {'text': '->', 'texthl': 'RuneInfoSign'})
endfunction

function! s:Signs(buf, items) abort
  call s:DefineVim()
  call sign_unplace('rune', {'buffer': a:buf})
  let done = {}
  for item in a:items
    " One sign a line: the most serious.
    let old = get(done, item.lnum, 'N')
    if has_key(done, item.lnum) && s:severity[old] <= s:severity[item.type]
      continue
    endif
    let done[item.lnum] = item.type
    call sign_place(0, 'rune', 'Rune' . item.type, a:buf,
          \ {'lnum': item.lnum, 'priority': 20 - s:severity[item.type]})
  endfor
endfunction

let s:props = {'E': 'rune_error', 'W': 'rune_warning', 'I': 'rune_info', 'N': 'rune_hint'}

function! s:Underline(buf, items) abort
  if !exists('*prop_type_add')
    return
  endif
  for [type, name] in items(s:props)
    if empty(prop_type_get(name))
      let group = {'E': 'RuneError', 'W': 'RuneWarning', 'I': 'RuneInfo', 'N': 'RuneHint'}[type]
      call prop_type_add(name, {'highlight': group, 'priority': 20 - s:severity[type], 'combine': 1})
    endif
    call prop_remove({'type': name, 'bufnr': a:buf, 'all': 1})
  endfor
  let last = getbufinfo(a:buf)[0].linecount
  for item in a:items
    if item.lnum > last
      continue
    endif
    let end_lnum = min([item.end_lnum, last])
    let end_col = item.end_col
    if end_lnum != item.end_lnum
      let end_col = len(getbufline(a:buf, end_lnum)[0]) + 1
    endif
    silent! call prop_add(item.lnum, item.col, {'end_lnum': end_lnum, 'end_col': end_col,
          \ 'type': s:props[item.type], 'bufnr': a:buf})
  endfor
endfunction

" What is reported at the cursor, echoed in the command line.
function! rune#echo() abort
  if has('nvim') || &filetype !=# 'rune'
    return
  endif
  let [lnum, col] = [line('.'), col('.')]
  let best = {}
  for item in rune#items(bufnr('%'))
    if item.lnum > lnum || item.end_lnum < lnum
      continue
    endif
    let inside = (item.lnum < lnum || item.col <= col) && (item.end_lnum > lnum || col < item.end_col)
    if empty(best) || (inside && !best.inside)
          \ || (inside == best.inside && s:severity[item.type] < s:severity[best.item.type])
      let best = {'item': item, 'inside': inside}
    endif
  endfor
  if empty(best)
    if get(b:, 'rune_echoed', 0)
      let b:rune_echoed = 0
      echo ''
    endif
    return
  endif
  let b:rune_echoed = 1
  let word = {'E': 'error', 'W': 'warning', 'I': 'note', 'N': 'hint'}[best.item.type]
  let text = word . ': ' . substitute(best.item.text, '\n', ' ', 'g')
  let width = &columns - 12
  if strdisplaywidth(text) > width
    let text = strcharpart(text, 0, width - 1) . '…'
  endif
  echohl {'E': 'ErrorMsg', 'W': 'WarningMsg', 'I': 'None', 'N': 'None'}[best.item.type]
  echo text
  echohl None
endfunction

let &cpo = s:cpo_save
unlet s:cpo_save
