# Adding a keyword

Say you want `yield`.

## 1. The token table

One line in `runec/include/rune/TokenKinds.def`, **inside the keyword block**:

```
TOK(KwYield,        "yield")
```

> **It must go between `KwFn` and `KwNil`.** `isKeyword` is a range check —
> `k >= Tok::KwFn && k <= Tok::KwNil` — and `keywordKind` builds its lookup map
> from tokens for which `isKeyword` is true. A `TOK` line outside that range is
> a token the lexer will never produce, and nothing will tell you: your keyword
> just silently stays an identifier.

That one line gives you the enumerator, the spelling, the name used in
"expected X, got Y", and the keyword lookup, because `TokenKinds.def` is
included four times with `TOK` defined differently each time.

## 2. Can it end a statement?

If your keyword can be the **last token** of a statement, add it to
`canEndStatement` in `Token.cpp`. `nil`, `true`, `false`, `return`, `break` and
`continue` are all in there. `fn`, `struct` and `if` are not, because a
statement never ends on them.

Get this wrong and Rune's newline inference misfires: a statement ending in
your keyword will run on into the next line.

## 3. Parse it

In `Parser.cpp`, wherever it belongs — `parseStatement` for a statement form,
`parseTopLevelDecl` for a declaration, the unary or primary expression parser
for an expression.

## 4. Everything downstream

If it produced a new AST node, follow [Adding syntax](adding_syntax.md). If it
only changed how an existing node is built, you may be done after Sema learns
what it means.

## 5. It is now reserved

A keyword is not just new syntax — it takes a name away. Any program using
`yield` as an identifier stops compiling. Check:

- `stdlib/` and `runetime/` for the name
- `tests/cases/`
- `examples/`
- the reserved-words table in `docs/reference/content.py`

```sh
grep -rw yield stdlib runetime tests examples
```

That last one matters: the reference has a *Reserved words* page listing them,
and it is generated from `content.py`, so the list is only right if you update
it.

## 6. Test and document

```rune
// tests/cases/NN_yield.rune
// EXPECT: 3
```

Then add it to the relevant section of `docs/reference/content.py` and run
`python3 docs/reference/export_markdown.py`.
