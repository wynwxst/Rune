#!/usr/bin/env python3
"""End-to-end test of editors/vim, in Vim and Neovim run headless.

Vim: filetype, syntax groups, indentation, the background checker (a borrow
checker error in the location list, signed and underlined; unsaved text
checked; lint findings on save) and `:make`. Neovim: `rune lsp` starts by
itself and reports the same error, and without it the checker feeds
`vim.diagnostic`.

    python3 tests/vim_test.py build-release/bin

An editor that is not installed is skipped. CTest runs this as `rune_vim`.
"""
import os, shutil, subprocess, sys, tempfile

BIN = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "build-release/bin")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RUNTIME = os.path.join(ROOT, "editors", "vim")

failures = []

def check(name, cond, detail=""):
    print(("ok   " if cond else "FAIL ") + name)
    if not cond:
        failures.append((name, detail))

def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(text)

# A use after move, which only the borrow checker reports.
MOVED = """class Node {
    pub v: i64
    fn init(self, v: i64) { self.v = v }
}

fn take(n: Node) -> i64 { n.v }

fn main() -> i64 {
    let a = Node(1)
    let b = take(a)
    b + a.v
}
"""
CLEAN = MOVED.replace("    b + a.v\n", "    b\n")

SYNTAX = """#lint(allow(unused-variable), warn(todo))
fn main() -> i64 {
    let f: @function(i64) -> bool = ||(n: i64) -> bool { n > 0 }
    let s = "total: {count}\\n"
    /* outer /* inner */ still */
    0 // done
}
"""

def run(cmd, out, cwd, timeout=60):
    """Runs an editor until it quits, and returns what its script wrote."""
    if os.path.exists(out):
        os.remove(out)
    try:
        subprocess.run(cmd, cwd=cwd, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None
    if not os.path.exists(out):
        return None
    return open(out).read().splitlines()

def vim_tests(tmp):
    vim = shutil.which("vim")
    if not vim:
        print("skip vim: not installed")
        return
    write(os.path.join(tmp, "vimrc"), f"""set nocompatible
let &rtp = '{RUNTIME},' . $VIMRUNTIME
filetype plugin indent on
syntax on
let g:rune_rune = '{BIN}/rune'
let g:rune_runec = '{BIN}/runec'
let g:rune_memory = 'zombie'
""")
    write(os.path.join(tmp, "syntax.rune"), SYNTAX)
    write(os.path.join(tmp, "syntax.vim"), """
let out = ['ft=' . &ft, 'syntax=' . get(b:, 'current_syntax', '')]
function! Group(line, text) abort
  let col = stridx(getline(a:line), a:text) + 1
  return synIDattr(synID(a:line, col, 1), 'name')
endfunction
call add(out, 'lint=' . Group(1, '#lint'))
call add(out, 'allow=' . Group(1, 'allow'))
call add(out, 'rule=' . Group(1, 'unused-variable'))
call add(out, 'fn=' . Group(2, 'fn'))
call add(out, 'main=' . Group(2, 'main'))
call add(out, 'fntype=' . Group(3, '@function'))
call add(out, 'placeholder=' . Group(4, '{count}'))
call add(out, 'escape=' . Group(4, '\\n'))
call add(out, 'nested=' . Group(5, 'still'))
call add(out, 'line=' . Group(6, 'done'))
call add(out, 'arrow=' . Group(3, '->'))
call add(out, 'indent=' . GetRuneIndent(3) . ',' . GetRuneIndent(7))
call add(out, 'commentstring=' . &commentstring . ' shiftwidth=' . &shiftwidth . ' expandtab=' . &expandtab)
call writefile(out, 'syntax.out')
qa!
""")
    lines = run([vim, "-u", "vimrc", "-i", "NONE", "-n", "--not-a-term", "-c", "source syntax.vim", "syntax.rune"],
                os.path.join(tmp, "syntax.out"), tmp)
    got = dict(l.split("=", 1) for l in (lines or []) if "=" in l)
    check("vim: .rune files are the rune filetype", got.get("ft") == "rune", lines)
    check("vim: the syntax loads", got.get("syntax") == "rune", lines)
    check("vim: #lint, its levels and its rules", got.get("lint") == "runeDecorator" and got.get("allow") == "runeLintKeyword"
          and got.get("rule") == "runeLintRule", lines)
    check("vim: keywords and function names", got.get("fn") == "runeKeyword" and got.get("main") == "runeFuncName", lines)
    check("vim: @function is a type", got.get("fntype") == "runeFnType", lines)
    check("vim: placeholders and escapes in strings", got.get("placeholder") == "runePlaceholder"
          and got.get("escape") == "runeEscape", lines)
    check("vim: block comments nest", got.get("nested") == "runeBlockComment", lines)
    check("vim: line comments, and operators outside them", got.get("line") == "runeLineComment"
          and got.get("arrow") == "runeOperator", lines)
    check("vim: indentation follows brackets", got.get("indent") == "4,0", lines)
    check("vim: comments and indentation settings", "commentstring=// %s shiftwidth=4 expandtab=1" in (lines or []), lines)

    # The checker: a borrow checker error, found on open.
    write(os.path.join(tmp, "moved.rune"), MOVED)
    write(os.path.join(tmp, "check.vim"), """
function! Wait(cond) abort
  for i in range(100)
    if eval(a:cond) | return | endif
    sleep 100m
  endfor
endfunction
call Wait('len(getloclist(0)) >= 2')
let out = []
for it in getloclist(0)
  call add(out, printf('open %s %d:%d-%d:%d %s', it.type, it.lnum, it.col, it.end_lnum, it.end_col, it.text))
endfor
call add(out, 'signs ' . len(sign_getplaced(bufnr(''), {'group': 'rune'})[0].signs))
for p in prop_list(1, {'end_lnum': -1})
  call add(out, printf('prop %d:%d+%d %s', p.lnum, p.col, p.length, p.type))
endfor
silent make
for it in getqflist()
  call add(out, printf('make %s %d:%d %s', it.type, it.lnum, it.col, it.text))
endfor
call writefile(out, 'check.out')
qa!
""")
    lines = run([vim, "-u", "vimrc", "-i", "NONE", "-n", "--not-a-term", "-c", "source check.vim", "moved.rune"],
                os.path.join(tmp, "check.out"), tmp) or []
    check("vim: the borrow checker's error is in the location list",
          any(l.startswith("open E 11:9-11:12 cannot use 'a.v'") for l in lines), lines)
    check("vim: where the value moved is listed too", any(l.startswith("open N 10:18-10:19 moved here") for l in lines), lines)
    check("vim: signs are placed", "signs 2" in lines, lines)
    check("vim: the error is underlined", "prop 11:9+3 rune_error" in lines and "prop 10:18+1 rune_hint" in lines, lines)
    check("vim: :make fills the quickfix list", any(l.startswith("make e 11:9 cannot use 'a.v'") for l in lines)
          and any(l.startswith("make n 10:18 moved here") for l in lines), lines)

    # Unsaved text is checked once typing pauses; the linter joins on save.
    write(os.path.join(tmp, "edit.rune"), CLEAN)
    write(os.path.join(tmp, "edit.vim"), """
function! Wait(cond) abort
  for i in range(100)
    if eval(a:cond) | return | endif
    sleep 100m
  endfor
endfunction
sleep 1
let out = ['before ' . len(getloclist(0))]
call setline(11, '    let unused = 1')
call append(11, '    b + a.v')
doautocmd TextChanged
call Wait('len(getloclist(0)) >= 2')
for it in getloclist(0)
  call add(out, printf('unsaved %s %d:%d %s', it.type, it.lnum, it.col, it.text))
endfor
write
call Wait('len(filter(getloclist(0), "v:val.type ==# ''W''")) > 0')
for it in getloclist(0)
  call add(out, printf('saved %s %d:%d %s', it.type, it.lnum, it.col, it.text))
endfor
call writefile(out, 'edit.out')
qa!
""")
    lines = run([vim, "-u", "vimrc", "-i", "NONE", "-n", "--not-a-term", "-c", "source edit.vim", "edit.rune"],
                os.path.join(tmp, "edit.out"), tmp) or []
    check("vim: a clean file has nothing listed", "before 0" in lines, lines)
    check("vim: an unsaved error is found once typing pauses",
          any(l.startswith("unsaved E 12:9 cannot use 'a.v'") for l in lines), lines)
    check("vim: the linter's findings join on save",
          any(l.startswith("saved W 11:9 variable `unused` is never read") for l in lines), lines)

def vim_format_tests(tmp):
    vim = shutil.which("vim")
    if not vim:
        return
    write(os.path.join(tmp, "fmt.rune"), "fn main() -> i64 {\n  let x = 1;\n    x\n}\n")
    write(os.path.join(tmp, "fmt.vim"), """
RuneFmt
call writefile(getline(1, '$') + ['modified=' . &modified], 'fmt.out')
qa!
""")
    lines = run([vim, "-u", "vimrc", "-i", "NONE", "-n", "--not-a-term", "-c", "source fmt.vim", "fmt.rune"],
                os.path.join(tmp, "fmt.out"), tmp) or []
    check("vim: :RuneFmt formats the buffer, types written in",
          lines[:4] == ["fn main() -> i64 {", "    let x: i64 = 1", "    x", "}"] and "modified=1" in lines, lines)

def nvim_tests(tmp):
    nvim = shutil.which("nvim")
    if not nvim:
        print("skip nvim: not installed")
        return
    for lsp in (1, 0):
        write(os.path.join(tmp, f"init{lsp}.lua"), f"""
vim.opt.rtp = {{ "{RUNTIME}", vim.env.VIMRUNTIME }}
vim.g.rune_rune = "{BIN}/rune"
vim.g.rune_runec = "{BIN}/runec"
vim.g.rune_memory = "zombie"
vim.g.rune_lsp = {lsp}
vim.cmd("filetype plugin indent on")
vim.cmd("syntax on")
""")
    write(os.path.join(tmp, "moved.rune"), MOVED)
    write(os.path.join(tmp, "nvim.lua"), """
local out = {}
vim.wait(15000, function() return #vim.diagnostic.get(0) >= 2 end, 100)
table.insert(out, "clients " .. #vim.lsp.get_clients({ bufnr = 0 }))
for _, d in ipairs(vim.diagnostic.get(0)) do
  table.insert(out, string.format("diag %d %d:%d-%d:%d %s", d.severity, d.lnum, d.col, d.end_lnum, d.end_col, d.message))
end
vim.fn.writefile(out, "nvim.out")
vim.cmd("qa!")
""")
    for lsp, what in ((1, "the language server"), (0, "the checker")):
        lines = run([nvim, "--headless", "-u", f"init{lsp}.lua", "-i", "NONE", "-n", "moved.rune", "-c", "luafile nvim.lua"],
                    os.path.join(tmp, "nvim.out"), tmp) or []
        check(f"nvim: {what} runs", ("clients 1" if lsp else "clients 0") in lines, lines)
        check(f"nvim: {what} reports the borrow checker's error",
              any(l.startswith("diag 1 10:8-10:11 cannot use 'a.v'") for l in lines), lines)
        check(f"nvim: {what} marks where the value moved",
              any(l.startswith("diag 4 9:17-9:18 moved here") for l in lines), lines)

def main():
    with tempfile.TemporaryDirectory() as tmp:
        tmp = os.path.realpath(tmp)
        vim_tests(tmp)
        vim_format_tests(tmp)
        nvim_tests(tmp)
    if failures:
        print(f"\n{len(failures)} failure(s):")
        for name, detail in failures:
            print(" - " + name)
            if detail:
                print("    " + str(detail)[:2000])
        sys.exit(1)
    print("\nall vim checks passed")

if __name__ == "__main__":
    main()
