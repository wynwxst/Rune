# Finding your way around

The compiler is about 33,000 lines. Most changes touch two or three files, and
which ones is usually predictable from what kind of change it is.

| File | Lines | What lives here |
| --- | --- | --- |
| `Sema.cpp` | 6,200 | Declaration collection, imports, shapes, signatures, marks, binds, generics, instantiation, statements. |
| `SemaExpr.cpp` | 3,200 | Type checking for expressions: calls, operators, members, closures, patterns. |
| `CodeGen.cpp` | 3,200 | Types and layout, functions, globals, vtables, type info, debug info, the entry point. |
| `CodeGenExpr.cpp` | 2,900 | Lowering expressions, and the intrinsics. |
| `Parser.cpp` | 2,800 | The grammar. |
| `Macro.cpp` | 1,300 | Collecting and expanding macros, over tokens. |
| `Compilation.cpp` | 1,100 | The driver: what to read, in what order, and what to write. |
| `Type.cpp` | 820 | The type context: interning, printing, structural questions. |
| `ASTClone.cpp` | 740 | Deep-copying declarations, which is how monomorphisation works. |
| `Lexer.cpp` | 670 | Text to tokens, including newline inference. |
| `Ownership.cpp` | 490 | Borrows, escapes, and the no-escape flag the code generator reads. |

`rune/src/main.cpp` (1,900 lines) is the package manager: the graph, the steps,
the commands. `rune/src/Targets.cpp` is what `--target` means: the foreign
targets and how their toolchains are found.

## Where to start, by kind of change

| You want to | Start in |
| --- | --- |
| Add a keyword | `TokenKinds.def`, then `Parser.cpp` |
| Change the grammar | `Parser.cpp`, and `AST.h` if it needs a node |
| Add or change a type rule | `SemaExpr.cpp` for expressions, `Sema.cpp` for declarations |
| Change how something is emitted | `CodeGenExpr.cpp` for expressions, `CodeGen.cpp` for shapes |
| Add a compiler flag | `Driver.h`, `Driver.cpp` |
| Add a standard-library function | `stdlib/std/*.rune` — nothing in C++ |
| Add something the compiler must answer itself | An `@intrinsic` in `stdlib/`, handled in `CodeGenExpr.cpp` |
| Change what a build does | `rune/src/main.cpp` |
| Add a cross target, or change how one is found | `rune/src/Targets.cpp` |
| Change what a freestanding program is given | `runetime/freestanding.rune` |
| Add a test | `tests/cases/` |
| Document something | `docs/reference/content.py` |

## Reading the compiler while it runs

```sh
runec --dump-tokens f.rune     # the token stream, one per line
runec --dump-ast f.rune        # the parse tree
runec --dump-symbols f.rune    # the resolved module scope
runec --dump-types f.rune      # the inferred type of every expression
runec --emit-llvm -o - f.rune  # the IR
runec --time f.rune            # how long each stage took
runec -v f.rune                # which stage is running, and the link command
```

`--dump-ast` and `--dump-symbols` are the two worth reaching for first when a
change is not doing what you expected: they say whether the problem is that the
parser did not build what you thought, or that the checker did not resolve it
the way you thought.
