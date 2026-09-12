# The parser and the AST

## Recursive descent, with precedence climbing

`Parser.cpp` is hand-written recursive descent. Binary expressions use operator
precedence; everything else is one function per production, named after it:
`parseFunction`, `parseStruct`, `parseMatch`, `parseBlock`.

The parser **never throws**. On a syntax error it reports, drops an `ErrorExpr`
into the tree, and resynchronises at the next plausible statement or
declaration boundary (`synchronize()`). One bad line therefore produces one
diagnostic, not a cascade — and the tree that comes back is complete enough for
later phases to keep working on, which is what makes multiple errors per run
possible.

Two flags steer ambiguities:

| Flag | Set while | Effect |
| --- | --- | --- |
| `NoStructLiteral` | Parsing the head of `if` / `while` / `for` / `match` | `{` opens the body, not a struct literal |
| `StopAtIs` | Parsing a condition | `x is Pattern` binds, rather than being an ordinary type test |

`consumeCloseAngle()` splits a `>>` token when closing nested generic argument
lists — `Vector<Vector<i64>>` lexes the tail as one shift token, and the parser
takes it apart.

## The AST

Everything derives from `Node`, which holds a `NodeKind` tag and a
`SourceRange`. Three families sit under it: `Expr`, `Stmt` and `Decl`.

```
Node
├── Expr    (Ty, Category, OverloadResolved)
│     IntLitExpr, BinaryExpr, CallExpr, MemberExpr, MatchExpr, ClosureExpr, …
├── Stmt
│     ExprStmt, VarStmtNode, DeclStmt, DeferStmt
└── Decl    (Name, IsPublic, Attrs, ModulePath, Parent, Doc)
      ├── ValueDecl (Ty, LinkName)
      │     FunctionDecl, VarDecl, GlobalVarDecl, FieldDecl
      └── NominalDecl (Generics, Fields, Methods, Bindings, DeclaredType, Deinit)
            StructDecl, EnumDecl, ClassDecl, MarkDecl
```

Casting is by tag, not by `dynamic_cast`: `isa<CallExpr>(n)`, `cast<CallExpr>(n)`,
`dyn_cast<CallExpr>(n)`. Each concrete class registers its tag with one
`DEF_CLASSOF(CallExpr, Call)` line near the bottom of `AST.h`. **A new node
class needs that line**, or `isa` will not compile for it.

## Fields the later phases fill in

The AST is not immutable — it is the compiler's working memory. Sema and
CodeGen write their conclusions onto it:

| Field | Written by | Read by |
| --- | --- | --- |
| `Expr::Ty` | Sema | everything after |
| `Expr::OverloadResolved` | Sema, when an operator turned out to be a call | CodeGen |
| `DeclRefExpr::Resolved` | Sema | CodeGen |
| `FunctionDecl::MangledName` | Sema | CodeGen |
| `FunctionDecl::Instantiations` | Sema | CodeGen |
| `VarDecl::NoEscape` | the ownership pass | CodeGen, to elide reference counting |
| `Decl::CodeGenFn` / `CodeGenGlobal` | CodeGen | CodeGen |

## Three helpers over the tree

| File | Does |
| --- | --- |
| `ASTWalk.cpp` | `forEachChild(node, fn)` — generic traversal |
| `ASTClone.cpp` | Deep copy of a declaration. This is how monomorphisation works: an instantiation is a clone with its generic parameters bound. |
| `ASTPrinter.cpp` | `--dump-ast` |

If you add a node kind, all three need to know about it. `ASTClone` in
particular: a node it does not copy becomes a null in every instantiation, and
the failure shows up a long way from the cause.
