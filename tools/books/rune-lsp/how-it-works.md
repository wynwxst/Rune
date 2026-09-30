# How it works

The server is a Rune program, `tools/rune-lsp.rune`, built on the same modules as
the linter:

| Module | Its part |
| --- | --- |
| `runetools::syntax` | lexing, exactly as the compiler lexes, but without ever stopping at an error |
| `runetools::index` | each file's declarations, imports, and the names each function binds |
| `runetools::project` | the files it has read, where imports lead, `Rune.toml`, and the types of expressions |
| `runetools::lint` | the lint rules |

## The index

When a file is opened, the server reads it and every module it imports, two
levels deep, plus the prelude (`std::option`, `std::result`). Each is lexed and
indexed: declarations with their signatures and documentation, and each
function's bindings with where each is visible. An open file is indexed from the
editor's text, and again on every change; every other file from disk, and again
when the editor reports that it changed there. Indexing a large file takes a few
milliseconds, which is why nothing is cached between edits.

Imports are followed the way the compiler follows them:

| `import` | Leads to |
| --- | --- |
| `std::a::b` | `<stdlib>/std/a/b.rune` |
| `<package>::a` | `src/a.rune` in the package the file is in |
| `<dependency>::a` | `src/a.rune` in a path dependency named in `Rune.toml` |
| `<name>::a`, outside a package | `a.rune` beside the file, or in a folder called `<name>` — how files built together with `runec --module <name>` import each other |

`import a::{b, c}` is followed member by member, and a member that is not a module
is taken as an item inside its parent.

## Types

Member completion, hover on a local and signature help all need the type of an
expression. The server works it out from the source, following what it can be
sure of: literals, annotations, constructor calls and struct literals, `let`
initialisers, parameters, `self`, fields, the declared results of functions and
methods — with the receiver's type arguments put in for the type's parameters —
indexing, `?`, `??`, `as`, `if` and `match` (by their first branch), and loops over
vectors, arrays, maps, ranges, strings and iterators.

It does not check anything, resolve overloads, or look through macros and
closures' inferred results. Where it cannot tell, it answers nothing — no
completion rather than the wrong completion. The compiler remains the only
authority on what a program means.

## The compiler

When a file is opened or saved, and when typing pauses, the server runs the
compiler and waits for it:

- in a package, `rune check --diagnostic-format json`, in the package's root —
  which checks under the memory model its `Rune.toml` names;
- otherwise, `runec --check --memory <check.memory> --diagnostic-format json <file>`.

Each open buffer with unsaved changes is written to a scratch file and passed as
`--source <path>=<scratch>`, which has the compiler read that text for `path`
while still reporting it as `path`. `rune check` hands the flag on to the
compiles it runs, and only to those: a build's cached output is never made from
text that was not saved.

To wait for a pause without a second thread, the server reads its input
unbuffered and asks the operating system, before reading the next message,
whether one arrives within `check.delay` milliseconds. Only when none does is
the check run.

Member completion asks the compiler too, with `runec --query-members
<file>:<start>:<end>`, which checks the buffer and prints the type of the
expression ending there, with its fields and methods, as JSON.

Each diagnostic comes back as one line of JSON with its file, its range, its
notes and its related locations, and is converted to the protocol's positions —
which count UTF-16 units, as the protocol requires, where the compiler counts
bytes. `rune` reports files by their real path, so one reached through a symbolic
link is matched back to the name the editor used.

## The protocol

Requests are answered one at a time, in order. The server announces:

| Capability | |
| --- | --- |
| text document sync | open, change (the whole text), save, close |
| `completionProvider` | triggered by `.`, `:`, `$` and `@` |
| `hoverProvider`, `definitionProvider`, `referencesProvider`, `documentHighlightProvider`, `documentSymbolProvider` | |
| `signatureHelpProvider` | triggered by `(` and `,` |
| `codeActionProvider` | `quickfix` and `source.fixAll.rune` |

It also handles `workspace/didChangeConfiguration` and
`workspace/didChangeWatchedFiles`. Any other request is answered with
*method not found*, never by stopping.

Not provided yet: rename, formatting, workspace-wide symbol search, and semantic
highlighting — syntax highlighting comes from the editor's grammar instead.
