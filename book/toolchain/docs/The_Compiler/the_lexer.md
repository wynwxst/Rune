# The lexer

`Lexer::tokenize()` reads one buffer and returns the whole token vector at
once. There is no streaming: the parser indexes into a `std::vector<Token>` and
can look as far ahead as it likes, which the grammar needs in several places.

## The token table

Every token kind is one line of `runec/include/rune/TokenKinds.def`:

```
TOK(KwFn,           "fn")
TOK(Arrow,          "->")
TOK(IntLiteral,     "<integer literal>")
```

The file is an X-macro list, included three times with `TOK` defined
differently each time — once to build the `Tok` enum, once for the spellings
table, once for the names table, and once more to build the keyword lookup map.
Adding a line gives you all four.

## Newline inference

Rune has no semicolons, so the lexer decides where statements end. It emits a
`Tok::Newline` only where a newline could plausibly terminate a statement, by
tracking two things:

- **`GroupDepth`** — how deep inside `(`, `[` we are. Inside brackets a newline is layout, not a terminator.
- **`canEndStatement(prev)`** — whether the previous token is one a statement could end on: an identifier, a literal, a closing bracket, `self`, `return`, `break`, `continue`, `true`, `false`, `nil`, `?`, `_`.

So this stays one statement, because `+` cannot end one:

```rune
let total = a +
            b
```

and this is two, because `1` can:

```rune
let a = 1
let b = 2
```

This is the reason `canEndStatement` exists and the reason a new keyword
sometimes has to be added to it. If your keyword can be the *last* token of a
statement — as `nil` is — it belongs in that switch. If it always introduces
something — as `fn` does — it does not.

## What else the lexer carries

| Field on `Token` | For |
| --- | --- |
| `Text` | Identifiers, string bodies, number suffixes |
| `IntValue` / `FloatValue` | Decoded literal values |
| `Suffix` | The `i32` in `10i32` |
| `Doc` | `///` lines written above this token, attached to the first token of a declaration |
| `AtLineStart` | Whether a newline preceded it — used for recovery hints |

Doc comments are gathered by the lexer rather than the parser because by the
time the parser sees the stream, comments are gone. `PendingDoc` accumulates
them and the first token of the next declaration takes them.
