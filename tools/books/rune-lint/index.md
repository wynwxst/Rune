# The Rune linter

`rune lint` reads Rune source for the mistakes the compiler lets through and the
habits that make code harder to read: a variable nobody reads, a `var` that never
changes, an import that does nothing, a statement after `return`, a `while true`
that wants to be a `loop`. Where the right change is certain it can make it for
you.

```sh
rune lint                  # the package you are in: src/ and tests/
rune lint src/parser.rune  # one file
rune lint --fix            # apply every fix that has a certain answer
rune lint --list           # every rule, whether it is on, and what it is for
```

A finding looks like the compiler's own diagnostics, with the rule's name in
brackets so you know what to search for, and — when there is one — the fix it
would make:

```text
● src/main.rune [7:8..14]
7 ║     let unused = 3
            ^^^^^^ WARNING: variable `unused` is never read [unused-variable]
     ─  fix: prefix `unused` with an underscore (rune lint --fix)
● 1 warning and 0 notes in 1 file.
```

## What it is, and what it is not

The linter is not a second compiler. It never decides whether a program is
*correct* — `rune check` does that, and a file with type errors in it is still
linted. What it reads is the shape of the source: its tokens, its declarations,
and the names each function binds. That is why it is fast enough to run on every
keystroke in an editor, and why it can point at a problem in code that does not
compile yet.

Everything it reports is one of two levels:

| Level | Shown as | Meant for |
| --- | --- | --- |
| `warn` | `WARNING` | something that is probably a mistake: dead code, an unread variable, a condition that cannot be what was meant |
| `note` | `NOTE` | style: naming, layout, a construct with a clearer spelling |

A few rules are `allow` — off — until you ask for them, because whether they
apply is a decision about a codebase rather than a fact about a line:
`missing-docs`, `todo` and `unused-parameter`.

## Running it

With no paths, `rune lint` lints the package the current directory is in — every
`.rune` file under its `src/` and `tests/` — or, outside a package, every `.rune`
file under the current directory. Paths may name files or directories; a
directory means every `.rune` file beneath it, leaving out `target/`, hidden
folders and `node_modules/`.

`-C <dir>` runs it as if from `<dir>`, like every other `rune` command:

```sh
rune lint -C examples/project/webserver
```

The exit status is what a script or a CI job needs:

| Status | Meaning |
| --- | --- |
| `0` | nothing was found |
| `1` | something was found, at any level |
| `2` | the command line was wrong, or a file could not be read or written |

## The rest of this book

- [Rules](rules.md) — every rule, what it looks for, an example, and its fix.
- [Configuring it](configuration.md) — turning rules on and off for a package, a
  file, a line or a single run.
- [Fixes](fixes.md) — what `--fix` changes, and what it never touches.
- [Output](output.md) — the report, and the JSON form for tools.
- [How it works](how-it-works.md) — what it reads, and where its judgement stops.

The same rules run inside the language server, so an editor shows these findings
as you type and offers the fixes as quick fixes; `rune doc lsp` is that book.
