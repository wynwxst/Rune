# Macros

A Rune macro is a rewrite from tokens to tokens, done before anything is
parsed. By the time the grammar sees the stream there are no macros left in it,
which is why nothing downstream of the parser knows they exist.

```rune
macro twice {
    ($x: expr) => { ($x) + ($x) }
}

twice!(3)        // becomes  (3) + (3)
```

## Two passes, in this order

**Collection.** `collectMacros(toks, diags, into, record, module)` takes every
`macro` declaration *out* of the token vector. With `record` set it also adds
each to the table and reports a repeated name.

**Expansion.** `expandMacros(toks, diags, table, module)` rewrites every
invocation, to a depth limit of 128 so a macro that expands to itself stops
with a diagnostic rather than the stack running out.

Collection happens for *every file in the compilation* before *any* file is
parsed. That is what makes a macro usable above where it is written, and a
`pub macro` in the standard library usable in your program. It also means the
whole compilation has to be tokenised before parsing can start — which is why
the lex phase is its own step in `Compilation.cpp`, and why the tokens are
handed to the parser rather than produced a second time.

## The order the table is built in

Collection runs over the standard library first, then imported libraries, then
this package's files. That order is deliberate: a duplicate name is reported
against "the earlier declaration", and which one that is should not depend on
how the units happen to be listed. `Compilation.cpp` builds an explicit
`macroOrder` for this rather than relying on the order the units were gathered.

## Fragment kinds

| Kind | Matches |
| --- | --- |
| `expr` | A balanced run of tokens up to a separator |
| `ident` | Exactly one name |
| `type` | A type, matched like an expression |
| `literal` | One literal |
| `block` | A `{ ... }` group |
| `tokens` | One token tree: a token, or a balanced group |

## Diagnostics inside an expansion

Every token an expansion produces carries the *invocation's* range, so an error
inside a macro points at the call — but the code it is really about appears
nowhere in the file. `DiagnosticEngine::noteExpansion` records what each
expansion stood for, and a diagnostic landing inside one is shown the chain:

```
in the expansion of `assert!`
  which stands for: if !(x > 0) { process::panic("...") }
```

Long chains are elided in the middle; the ends are what tells you anything.
