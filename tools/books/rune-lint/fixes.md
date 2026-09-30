# Fixes

Some findings carry the change that resolves them. `rune lint --fix` makes every
such change, writes the files back, and reports what is left:

```sh
rune lint --fix
```

```text
● src/main.rune [9:4..9]
9 ║     for i in 0..3 {}
                  ^^ WARNING: empty `for` body [empty-block]
● 1 warning and 0 notes in 1 file; fixed 6.
```

What was fixed is still listed, marked `fixed:`; the summary counts only what
remains, and the exit status is 1 only if something does.

## What gets fixed

| Rule | The change |
| --- | --- |
| `unused-variable` | `name` becomes `_name`, with every use; a struct pattern's field becomes `field: _` |
| `unused-import` | the import's line is removed |
| `never-reassigned` | `var` becomes `let` |
| `bool-comparison` | `x == true` and `x != false` become `x` |
| `naming` | a local variable is renamed, with every use |
| `needless-return` | the `return` is removed, leaving its value |
| `while-true` | `while true` becomes `loop` |
| `unnecessary-semicolon` | the `;` is removed |
| `trailing-whitespace` | the whitespace is removed |
| `tab-indentation` | each leading tab becomes four spaces |

Everything else is reported without a fix, because the right change is a choice:
which of two duplicate imports to keep, whether dead code should be deleted or
used, where a long line should break, what a missing comment should say.

## What a fix will not do

A fix never changes what a program does. Each one either removes something that
had no effect, or respells something with the same meaning: an unread
variable keeps its value and only gains an underscore, a `var` that never changed
becomes a `let` that cannot.

Fixes are only offered where the answer is certain from the source alone. That is
why a parameter is never renamed (its name is part of how the function is
called), why a declaration is never renamed (another file may use it), and why an
import is only removed when nothing about it could matter — see
[unused-import](rules.md#unused-import) for the cases it leaves alone.

When two fixes would touch the same text, only one is made; the other is left
for the next run, which will find it again if it still applies.

## In an editor

The language server offers the same fixes as quick fixes on each finding, and
one *Fix all auto-fixable Rune lint problems* action that makes them all at once
— the same change `--fix` would make to the file.
