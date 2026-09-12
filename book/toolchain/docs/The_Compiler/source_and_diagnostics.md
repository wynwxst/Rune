# Source, locations and diagnostics

## SourceManager

Every byte the compiler reads goes through `SourceManager`. Files are loaded
into one virtual coordinate space: each gets a `StartOffset`, and a `SourceLoc`
is a single 32-bit offset into that space.

```
  file 0            file 1                file 2
  ├────────────────┼─────────────────────┼──────────────┤
  1              4096                  9000          12500
                      ▲
                      SourceLoc(5200) — decodes to file 1, line 12, column 4
```

The point of one flat space is that `SourceLoc` is four bytes and needs no file
id beside it, which matters because every AST node carries a `SourceRange` —
two of them. `SourceManager::decode` finds the owning file and turns an offset
into a line and column using a per-file table of line starts computed once at
load.

After all files are loaded, `SourceManager` is read-only. That is what lets the
lexer and parser run on several threads: they only ever call `file(id)` and
`decode`, and nothing is being added underneath them.

## Diagnostics

`DiagnosticEngine` renders diagnostics; `DiagBuilder` collects one and emits it
when it goes out of scope. The chained form is the normal one:

```cpp
Diags.error(range, "cannot find '{}' in this scope", name)
     .note("check the spelling, or add an `import` for the module that "
           "declares it")
     .related(decl->NameRange, "declared here", "this is the one you mean")
     .code(203);
```

| Piece | Shows as |
| --- | --- |
| `error` / `warn` / `note` / `remark` / `fatal` | The severity glyph and word |
| the range | A source snippet with carets under it |
| `.note(...)` | A `─ note:` line beneath |
| `.related(r, label, hint)` | A second snippet with a `└▶` connector |
| `.code(n)` | `[E0203]`, or `[W0001]` for a warning |

The `{}` in the message is Rune's own tiny formatter (`fmt` in
`Diagnostics.h`), not `printf` and not `std::format`.

Codes are four digits, formatted `E%04u` or `W%04u`, and are grouped loosely by
phase — roughly 100s for the driver and directives, 200s for the checker, 500s
upward for later passes. There is no registry: a code is just the number passed
to `.code()`. Before adding one, grep for it so you do not reuse a number that
already means something else.

## Two things the engine does that are easy to miss

**Speculation.** The parser sometimes tries an ambiguous production and backs
out. `beginSpeculation()` / `endSpeculation()` bracket that, and diagnostics
raised in between are discarded rather than counted. The depth is *per thread*,
because files are parsed concurrently and one parser backing out must not
swallow another's real error.

**Capture.** A pass whose work is spread over threads still has to report in a
settled order. `beginCapture(&bucket)` diverts this thread's diagnostics into a
vector instead of printing them; the pass gives each item a bucket of its own
and replays them in order afterwards. That is how the ownership pass reports the
same findings, in the same order, no matter how the work was divided. See
[Ownership](ownership.md).
