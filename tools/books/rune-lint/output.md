# Output

## The report

By default each finding is printed the way the compiler prints a diagnostic: the
file and the span, the line, a caret under what was found, the level and the
message, the rule in brackets, and the fix when there is one. A summary follows
on standard error:

```text
● src/shapes.rune [12:4..7]
12 ║     var area = width * height
         ^^^ WARNING: `area` is never changed; declare it with `let` [never-reassigned]
      ─  fix: change `var` to `let` (rune lint --fix)
● 1 warning and 0 notes in 1 file.
```

Columns are counted in characters from 0, as `runec` counts them. Paths are shown
relative to the current directory when they are under it.

## JSON, for tools

`--format json` prints one JSON object per finding, one per line, and nothing
else on standard output — no summary:

```sh
rune lint --format json
```

```json
{"severity":"warning","code":"never-reassigned","message":"`area` is never changed; declare it with `let`","file":"/work/shapes/src/shapes.rune","line":12,"column":4,"endLine":12,"endColumn":13,"fix":{"title":"Change `var` to `let`","edits":[{"start":301,"end":304,"text":"let"}]}}
```

| Field | Meaning |
| --- | --- |
| `severity` | `warning` or `note` |
| `code` | the rule |
| `message` | what was found |
| `file` | the file, as an absolute path |
| `line`, `endLine` | 1-based |
| `column`, `endColumn` | 0-based, in bytes |
| `fix` | present when there is one: its `title`, and `edits` — each replaces the bytes from `start` to `end` in the file with `text` |

The shape is the one `runec --diagnostic-format json` and
`rune check --diagnostic-format json` use for the compiler's diagnostics, with
`code` holding a rule name instead of an error code — so a tool that reads one
reads both.

## In CI

The exit status is the thing to test: `0` when nothing was found, `1` when
something was, `2` when the linter could not do its job. To fail only on
warnings, leave the notes out:

```sh
rune lint -A naming -A needless-return -A while-true -A unnecessary-semicolon \
          -A trailing-whitespace -A tab-indentation -A line-too-long
```

or put the same `allow` list in the package's `[lint]` table, so that the editor
agrees.
