# What is reported, for these

| Written | Reported |
| --- | --- |
| `@macro` outside a macro package | `a procedural macro belongs in a macro package`, naming `@type(Macros)` |
| a `@macro fn` without `pub` | a macro has to be `pub` — the dispatcher the compiler writes is another module |
| `Macro::error("...")` | that message, against the invocation, with an arrow at the line that refused |
| a macro that does not finish | `macro 'x' did not finish`, with the status or signal and how to run it yourself |
| an expansion that does not lex | `macro 'x' produced text that is not valid Rune` |
