# Patterns

`$name: kind` captures a piece of the invocation. The kind says how much to take:

| Kind | Matches |
| --- | --- |
| `expr` | a balanced run of tokens, stopping at a top-level `,` or `;` — neither can occur inside one expression |
| `ty` | the same, for a type |
| `ident` | exactly one name |
| `literal` | one literal |
| `block` | a `{ ... }` group |
| `tt` | one token tree: a single token, or a balanced group |

`$( ... )sep*` matches a repetition, with `sep` between rounds and `*` or `+` for none-or-more and one-or-more. The body repeats once per round, and the separator goes **inside** it — `$( push($x); )*` — because the expansion needs one between statements, not the pattern.
