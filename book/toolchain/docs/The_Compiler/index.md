# The compiler

`runec` is a straight pipeline. Nothing loops back: each phase reads what the
one before it produced and hands on something new, and by the time a phase
runs, everything it could need has already been decided.

```
  ┌────────┐  ┌──────┐  ┌──────┐  ┌──────┐  ┌────────┐  ┌──────┐  ┌───────┐
  │ Source │─►│Lexer │─►│Macros│─►│Parser│─►│ #Config│─►│ Sema │─►│CodeGen│
  └────────┘  └──────┘  └──────┘  └──────┘  └────────┘  └──────┘  └───────┘
    bytes      tokens    tokens     AST       AST, this   typed      IR
                         rewritten            build's       AST       │
                                                                     ▼
                                                          prune, verify, emit
                                                                     │
                                                                     ▼
                                                                  cc ──► exe
```

The entry point is `runCompilerMain` in `Driver.cpp`, which does nothing but
parse `argv` into a `CompilerOptions` and call `compileWithOptions` in
`Compilation.cpp`. That function is the whole pipeline, in order, and is the
best single file to read first.

## Pages

- [Source, locations and diagnostics](source_and_diagnostics.md)
- [The lexer](the_lexer.md)
- [Macros](macros.md)
- [The parser and the AST](the_parser_and_the_ast.md)
- [Conditional compilation](conditional_compilation.md)
- [Semantic analysis](semantic_analysis.md)
- [The type system](the_type_system.md)
- [Generics and monomorphisation](generics_and_monomorphisation.md)
- [Ownership](ownership.md)
- [Code generation](code_generation.md)
- [Inline assembly](inline_assembly.md)
- [What comes out](what_comes_out.md)
