# The Rune formatter

`rune fmt` lays Rune source out one way, so that nobody has to think about it,
and writes in what the compiler knows but the code does not say: the type of
every binding, and the name of every argument.

```sh
rune fmt                   # the package you are in: src/ and tests/
rune fmt src/parser.rune   # one file, or a directory
rune fmt --check           # change nothing; list what is not formatted
```

A file before:

```rune
fn area(width: i64, height: i64) -> i64 { width * height }
import std::io

fn main() -> i64 {
  let a = area(3, 4);
    while true {
        break
    }
    io::println(a)
    0
}
```

and after:

```rune
import std::io

fn area(width: i64, height: i64) -> i64 { width * height }

fn main() -> i64 {
    let a: i64 = area(width: 3, height: 4)
    loop {
        break
    }
    io::println(value: a)
    0
}
```

The import moved to the top; the indentation is four spaces a level; the `;`
that ended nothing is gone and `while true` is a `loop`; `a` says it is an
`i64`; and `area`'s arguments say which is the width. [What it
changes](what-it-changes.md) goes through each of these.

## It never breaks a file

A file that compiles still compiles afterwards, and does the same thing: the
formatter checks it again before writing, and if what it wrote in would not
compile, it leaves that out and only lays the file out. A file that does not
compile yet is laid out but has nothing written in, since the compiler cannot
say what the types are — `rune fmt` says so, and the next run, once it
compiles, finishes the job.

## In an editor

The language server formats with the same rules: **Format Document** in VS Code
(and `vim.lsp.buf.format()` in Neovim) does what `rune fmt` does to that file,
unsaved changes and all. And the types and labels it would write are shown in
the editor as you type, greyed out; double-click one to write it in. `rune doc
lsp` has more.
