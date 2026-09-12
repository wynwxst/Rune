# Extending the language

Every page here is a recipe: what to touch, in what order, and what breaks if
you miss a step. They all end the same way — add a case under `tests/cases/`
and document it in `docs/reference/content.py` — because a feature that is not
tested and not written down is not finished.

## The general shape

A change to the language moves through the pipeline in order, and you can
usually predict how far it reaches:

```
   TokenKinds.def ─► Parser.cpp ─► AST.h ─► Sema ─► CodeGen ─► tests ─► docs
        │               │           │        │        │
     spelling        grammar      shape    meaning   code
```

A change that stops early is cheap. Adding an attribute touches the parser and
one phase. Adding a keyword that introduces a new declaration form touches all
of it.

## Before you start

Two habits save time:

**Work outward from a test.** Write the `tests/cases/` file first, with the
`// EXPECT:` lines you want. It fails at the lexer, then at the parser, then in
Sema, and each failure tells you exactly where the next edit goes.

**Use the dumps.** `--dump-ast` after the parser change and `--dump-symbols`
after the Sema change answer "did the thing I wrote actually get built" without
a debugger.

## Two worked examples

Both were added the same week, and they sit at opposite ends of how far a
feature reaches:

- **`@Config`** needed a phase of its own between parsing and checking, but no grammar: it is a decorator, and decorators already parse. See [Conditional compilation](../The_Compiler/conditional_compilation.md).
- **Inline assembly** needed nothing but two standard-library declarations and one case in the code generator, because the intrinsic mechanism already existed for it. See [Inline assembly](../The_Compiler/inline_assembly.md).

Neither touched the lexer, the parser, the AST or the three tree helpers. That
is worth aiming for.

## Pages

- [Adding a keyword](adding_a_keyword.md)
- [Adding syntax](adding_syntax.md)
- [Adding a type rule](adding_a_type_rule.md)
- [Adding an attribute](adding_an_attribute.md)
- [Adding an intrinsic](adding_an_intrinsic.md)
- [Adding a compiler flag](adding_a_compiler_flag.md)
- [Adding a diagnostic](adding_a_diagnostic.md)
- [Adding to the standard library](adding_to_the_standard_library.md)
