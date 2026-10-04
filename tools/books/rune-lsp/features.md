# Features

## Diagnostics

Two sources, reported side by side and labelled so you can tell them apart:

| Source | When | What |
| --- | --- | --- |
| `runec` | a file is opened or saved, and when typing pauses | everything the compiler reports |
| `rune-lint` | a file is opened, and on every change | the linter's findings |

**The compiler** runs over what the file belongs to. Inside a package — a
directory with a `Rune.toml` above it — that is `rune check`, run in the
package's root, which knows the package's other files and builds its
dependencies; an error in another module of the package is shown in that file. A
file on its own is checked with `runec --check`. Both are asked for
`--diagnostic-format json`, so nothing is scraped from text meant for a terminal.
Every kind of error comes through: the lexer's and parser's, name resolution and
types, macro expansion, and — in a package whose `Rune.toml` says `[build] memory =
"zombie"`, or with `check.memory` for a file on its own — the borrow checker's:
use after move, a borrow outliving its owner, a second mutable borrow. Each keeps
the compiler's notes in its message. A second location — where the value was
moved, where a borrow began — is attached as related information and also
underlined, as a hint that points back to the error. An error with nowhere to
point, such as a dependency that cannot be found, is shown on the file's first
line with the words the build used.

The compiler checks the text in the editor, not only what is on disk: every open
buffer with unsaved changes is handed to it in place of its file. It runs when a
file is opened or saved and, with `check.onChange`, once typing has paused for
`check.delay` milliseconds — never while there is a message waiting to be
answered, so completion is not held up by a check. An untitled buffer is never
compiled.

**The linter** runs over the text in the editor, saved or not. Its warnings are
shown as warnings and its notes as information; unused variables, imports, dead
and unreachable code are also marked as unnecessary, which most editors show
faded. The package's `[lint]` table applies, and then the editor's settings.

## Completion

What is offered depends on what is just before the cursor:

| Before the cursor | Offered |
| --- | --- |
| `value.` | the value's fields and methods — from its type, every `extend` and `bind` of it, and its superclass — then its intrinsics |
| `value.$` | the intrinsics for the value's type: `length`, `substring`, `byteAt`, `str`, `clone`… |
| `io::` | what the module exports: its `pub` functions, types, globals and macros |
| `Shape::` | the enum's variants and the type's static functions |
| `@` | the decorators |
| `import ` and `import std::` | the modules there are: `std` and the standard library's folders and files, the package's own modules, its dependencies |
| anything else | locals and parameters in scope (with their types), `self`, the file's declarations, imported modules and items, `Some`/`None`/`Ok`/`Err`, the built-in types, `println!` and the other macros, keywords, and snippets for `fn`, `if`, `for`, `match`, `struct`, `class`, `enum` and `main` |

Functions and methods complete as a call with a placeholder for each parameter.
Nothing is offered inside a comment or a string. A type's private members are
only offered inside the file that declares them.

After `.`, the compiler itself is asked what the value is, so the answer is
exact: inherited members, what a generic chain returns, what a `bind` adds, a
closure parameter's inferred type. It is asked about the buffer as it stands,
and answers in a few tens of milliseconds. When it cannot — the code around the
cursor does not parse yet, say — the server falls back to its own reading of
the code: a `let`'s annotation or initialiser, a constructor call, a field, a
method's declared result with the standard library's generics filled in —
`vector::Vector<Token>`'s `pop()` gives a `Token?`. Where neither can tell, it
offers nothing rather than guess; see [How it works](how-it-works.md).

## Inlay hints

What the compiler knows but the code does not say is shown in the text, greyed
out:

```rune
let total: i64 = area(width: 3, height: 4)
//       ^^^^^        ^^^^^^    ^^^^^^^  hints
```

- after a `let` or `var` without a type, and a closure parameter without one,
  the type the compiler inferred, spelled the way the file would write it —
  through its own imports, `vec::Vector<i64>` for `import std::collections::vector
  as vec`;
- before an argument given by position, the parameter it goes to — except where
  the argument already is a variable of that name, `area(width, height)`;
- after a `for` loop's variable, its element type, which has no place to be
  written and so is only shown.

Double-click a hint to write it into the code — the label, or `: Type` — exactly
as `rune fmt` would. They come from the compiler, over the text as it stands,
and are worked out again for each new version of the file; while the file is too
broken to check, the last ones stay. Nothing is hinted inside a generic function,
whose types differ with each use, nor inside a macro invocation.
`inlayHints.types` and `inlayHints.parameters` turn each kind off.

## Formatting

**Format Document** (`textDocument/formatting`) formats the file with the same
rules as `rune fmt`: the linter's style fixes, types and argument labels written
in, directives and imports first, indentation from the brackets, and tidy blank
lines. Unsaved changes are formatted as they stand. Whatever it holds back —
because the file does not compile yet, say — it says in the server's log.
`rune doc fmt` describes each step; the `format.*` settings choose which run.

## Hover

- On a declaration or a use of one: its signature, the kind of thing it is and
  the module it is in, and its `///` documentation (or `#Doc`), rendered.
- On a local or a parameter: its type, inferred where it is not written.
- On an import: the module, and the comment at the top of its file.
- On an intrinsic, `.$length`: its signature.

## Go to definition

On any name the server can resolve: a local goes to where it is bound, a function
or type to its declaration — in this file, another module of the package, a path
dependency, or the standard library — and a module to its file.

## References and highlights

*Find references* lists every use of the name under the cursor: for a local,
within its function; for anything else, across the open files and the file that
declares it. *Highlight* marks the uses in the current file, distinguishing the
ones that assign to it.

## Outline

The file's declarations, each type with its fields, variants and methods beneath
it. Members added to a type declared elsewhere — `extend String { … }` — are
listed on their own.

## Signature help

While typing a call's arguments: the function's signature and documentation, with
the parameter being typed marked. Calling a type — `Point(1, 2)` — shows its
`init`.

## Quick fixes

Every lint finding with a fix is offered as a quick fix on its range. The *Fix all
auto-fixable Rune lint problems* source action makes every fix in the file at
once — the same change `rune lint --fix` would make.
