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

## Procedural macros

A `#macro fn` is not rewritten by matching: it is ordinary Rune, compiled into
a program of its own and *run* to expand each invocation. The pieces live in
`MacroEval.{h,cpp}` and in `Compilation.cpp`.

**Gathering.** Right after lexing, `compileWithOptions` walks every unit that
is not the standard library — this package's files and the units of every
imported `.rul` — and sorts them:

- a file that opens with `#type(Macros)` (`declaresMacroPackage`) is a macro source as it stands, and is taken out of the compilation;
- any other file with a `#macro` in it (`hasProcMacros`) has its macros *lifted*: `liftProcMacros` erases each `#macro fn` from the token stream and returns the file's text with everything blanked except those functions and its `import std::...` lines, so lines and columns survive;
- `collectProcMacros` records each macro's name and the module it will have inside the package, and reports a duplicate or a missing `pub`.

Lifted files are written into the package as `lifted/<basename>`, so a
diagnostic from the nested build names the file the macro was written in. A
library's units are generated under a prefixed name, since two libraries may
both have a `macros.rune`.

**Building.** `buildMacroPackage` writes a dispatcher `main`
(`macroDispatcherSource`) and calls `compileWithOptions` again with
`MacroPackage = true`: for the host triple, under ARC, with minimal safety and
**no import paths**, because the libraries on them were built for the target
and the program's memory mode and could not be linked into a host program.

**Running.** `runProcMacro` writes `<name>\n<serialised tokens>` to a request
file, runs the package with `RUNE_MACRO_REQUEST` and `RUNE_MACRO_ANSWER` set,
and reads back `ok\n<source>` or `error\n<message>`. A bracketed group travels
as one token with its children after it.

## The macro package cache

The built package lives in `$TMPDIR/rune-macros/macros-<identity>-<stamp>`.

- **identity** hashes where the sources came from and the module each became — which package this is, whatever its macros currently say;
- **stamp** hashes what the sources *say*, token by token (`stampTokens`: kinds and spellings, never positions or comments), plus the size and time of every standard library file and of `runec` itself.

So editing a macro builds the package again, and editing the code around a
lifted macro, moving it, or rewording a comment does not. A stdlib or compiler
fix always does.

The program is linked to a `.partial` name and renamed into place, so a build
that fails or is killed never leaves a file that a later build takes for a
finished one. After a successful build, older builds with the same identity
are deleted, and so is anything in the directory nobody has used for a
fortnight; a cache hit touches the program's time to say it is still in use.

## Exporting macros

A library's `.rul` stores its `#type(Macros)` files as extra units
(`exportedMacroFiles`, appended in the library branch of `compileWithOptions`),
and its lifted `#macro`s are still in the sources it stores anyway. An importer
gathers both like its own, as above, so importing a library brings its macros
along. Erasing a library's macro unit shifts `firstStdlibUnit`,
`firstUserModule` and `libraryConfigs` with it.
