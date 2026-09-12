# The shape of the toolchain

## One compilation is one process

`runec` compiles a whole program at once. There is no separate-compilation
model inside the compiler: every file named on the command line, plus the whole
standard library, plus the source carried inside every imported `.rul`, is
lexed, parsed and type-checked together, into one symbol table and one LLVM
module.

That is unusual, and it is worth being clear about why, because most of the
design follows from it.

Rune's generics are monomorphised, its marks are resolved statically, and a
`bind` written in one file changes what is true of a type in every other file.
A `bind io::Display to i64` in your program is visible to the standard
library's `println`. For that to work, the checker has to see the whole program
at once — there is no header that could summarise it.

The cost is that every compile pays for the standard library again. The
mitigations are the subject of [Performance](../Performance/index.md): the
library is *read* in full but only the parts reached are *emitted*, and the
passes that can be done in parallel are.

## What a `.rul` actually is

A Rune library is not an object file with a header beside it. It is an archive
holding two things:

- the compiled object code for the library's own modules, and
- **the full source text** of each of those modules.

An importer re-parses that source. That is how it gets public signatures, and
it is also how it gets the *bodies* of generic functions, which it must have in
order to instantiate them for its own types. Non-generic code is not
re-emitted: those declarations are marked imported, so the importer emits a
declaration and the linker resolves it against the library's object.

So a library boundary is a source boundary that happens to carry a cache of
compiled code with it.

## The passes

```
    source files ─┐
   stdlib/*.rune ─┼─► lex ─► collect macros ─► expand ─► parse ─► AST
  imported .rul ──┘                                                │
                                                                   ▼
                                                                  Sema
                                          (collect, imports, shapes,
                                           signatures, bodies, ownership)
                                                                   │
                                                                   ▼
                                                     CodeGen ─► LLVM IR
                                                                   │
                                                                   ▼
                                                 prune ─► verify ─► object
                                                                   │
                                                                   ▼
                                                        cc ─► executable
```

Each of these has a page in [The compiler](../The_Compiler/index.md).

## Where the source lives

| Path | Holds |
| --- | --- |
| `runec/include/rune/` | Every header. One per phase, plus `AST.h`, `Type.h` and `TokenKinds.def`. |
| `runec/src/` | The phases themselves. `Sema.cpp` and `SemaExpr.cpp` are the largest by a wide margin. |
| `runec/tools/runec_main.cpp` | Seven lines: calls `runCompilerMain`. |
| `rune/src/` | The package manager: `main.cpp`, plus `Manifest`, `Toml`, `Jobs` and `Fingerprint`. |
| `tests/cases/` | End-to-end tests, one file each. |
| `docs/reference/content.py` | The language reference, as data. |
| `book/` | This book and the beginner's guide. |
