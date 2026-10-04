# What is reported, for these

| Written | Reported |
| --- | --- |
| `#macro` on something that is not a function | `#macro` belongs on a function |
| a `#macro fn` without `pub` | a macro has to be `pub` — the dispatcher the compiler writes is another module |
| `Macro::error("...")` | that message, against the invocation, with an arrow at the line that refused |
| a macro that does not finish | `macro 'x' did not finish`, with the status or signal and how to run it yourself |
| an expansion that does not lex | `macro 'x' produced text that is not valid Rune` |
| `name!(` never closed — often a string left open inside it | `` `name!(` is never closed `` (E0127), rather than the rest of the file read as its arguments |
| a pattern the input does not fit | whatever the macro passes to `Macro::fail` — `c.why()` names what was expected and what came |
