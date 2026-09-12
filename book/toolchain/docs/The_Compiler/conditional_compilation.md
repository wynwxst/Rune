# Conditional compilation

`@Config(...)` is answered in `Config.cpp`, between parsing and everything
else. That position is the whole design: a declaration the condition rules out
is removed from the module before `collectModule` ever sees it, so it is never
named, never resolved, never checked, and never emitted.

It has to be that early. A Windows-only function calls Win32 functions that are
declared nowhere on macOS; a check that ran first would report a dozen "cannot
find" errors about code that was never meant to exist here.

It cannot be much earlier, either. The condition is written as an expression,
so the parser has to have built one.

## The pipeline position

```
  lex ─► macros ─► parse ─► @Config ─► collect ─► … ─► codegen
                             ▲
                             the module still has every declaration;
                             afterwards it has only this build's
```

`Compilation.cpp` builds a `ConfigSet` once from the options and the target
triple, then calls `applyConfig(module, cfg, diags)` on each parsed module.

## The expression language

Deliberately tiny, because it is evaluated without a type checker:

| Form | Is |
| --- | --- |
| `key == "value"`, `key != "value"` | A build fact compared with a string |
| `name` | A flag, set or not |
| `!`, `&&`, `\|\|`, `( )` | The connectives |

`eval` in `Config.cpp` walks the parsed expression and handles exactly
`BoolLitExpr`, `DeclRefExpr`, `UnaryExpr` with `Not`, and `BinaryExpr` with
`Eq`, `Ne`, `LogicalAnd` or `LogicalOr`. Anything else is reported.

Two details worth keeping if you change it:

**Both sides of `&&` and `||` are evaluated.** Short-circuiting would hide a
mistake in the right-hand side whenever the left already settled the answer,
and a typo that only shows up on other people's platforms is the worst kind.

**A condition that could not be evaluated keeps the declaration.** `ok` is
threaded through the walk, and `evaluateConfig` returns true when it comes back
false. Deleting code on the strength of an expression the compiler did not
understand would turn one clear diagnostic into a cascade of "cannot find"
further on.

## Adding a key

`ConfigSet::forOptions` is the only place that decides what a build knows. A
new comparable key is one line in `Values`; a new flag is one `Flags.insert`.
Both are then automatically available to the "did you mean" suggestion, which
walks `known()`.

Keys derived from the triple go through `osName`, `archName` and `familyName`
rather than the triple's own spelling. A triple says `darwin`, `macosx` or
`apple`; everybody writing a condition means `macos`.

## Removing members

`applyToMembers` handles fields, methods, enum variants, `extend` and `bind`
bodies, and `extern` blocks. Fields need one extra step:

```cpp
unsigned index = 0;
for (auto &f : nd->Fields)
  f->Index = index++;
```

`FieldDecl::Index` is what the code generator uses to lay a value out, so a gap
left by a removed field would make every later field read the wrong memory. Any
new member list that carries an index needs the same treatment.

## What it does not cover

`@Config` applies to declarations, because that is where the parser accepts
decorators. Making a *statement* conditional would mean accepting attributes in
statement position — see [Adding syntax](../Extending_The_Language/adding_syntax.md)
— and the workaround is to put the statement in a function of its own, which
also means a missing case is a name that cannot be found rather than silence.

Macros are expanded over tokens before parsing, so a macro cannot be
conditional either.
