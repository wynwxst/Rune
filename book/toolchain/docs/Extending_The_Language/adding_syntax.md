# Adding syntax

A new expression or statement form usually needs a new AST node. There are six
places to touch, and the compiler will only tell you about four of them.

## 1. The node kind

`NodeKind` in `AST.h` — add the tag in the right family (expressions,
statements, declarations, type expressions, patterns; they are grouped and
commented).

## 2. The node

```cpp
struct YieldExpr : Expr {
  ExprPtr Value;
  YieldExpr() : Expr(NodeKind::Yield) {}
};
```

## 3. The cast registration

Near the bottom of `AST.h`:

```cpp
DEF_CLASSOF(YieldExpr, Yield)
```

Without it, `isa<YieldExpr>(n)` does not compile. This one you will notice.

## 4. Parse it

A `parseYield()` in `Parser.cpp`, called from wherever the form is legal.

## 5. The three tree helpers

These are the ones nothing will remind you about:

| File | Add | If you forget |
| --- | --- | --- |
| `ASTWalk.cpp` | the node's children to `forEachChild` | Anything that walks the tree silently skips your subtree |
| `ASTClone.cpp` | a clone arm | **Every generic instantiation gets a null where your node was.** The crash appears a long way from the cause |
| `ASTPrinter.cpp` | a print arm | `--dump-ast` shows nothing, which is exactly when you need it |

`ASTClone` is the dangerous one. Monomorphisation is deep-copying a
declaration, so a node the cloner does not know about simply vanishes from
every instantiation of every generic that contains it.

## 6. Check it, then emit it

- `SemaExpr.cpp` — a case in `checkExpr` that sets `Expr::Ty`.
- `CodeGenExpr.cpp` — a case in `emitRValue` (and `emitLValue` if it can be assigned to).

If the form can produce a value in some paths and diverge in others, give it
`Never` where it diverges; that is what makes `if c { 1 } else { return }`
type-check.

## 7. Test and document

A `tests/cases/` file with `// EXPECT:` lines, and a section in
`docs/reference/content.py`. Samples in the reference are compiled and run when
it is built, so a broken example is a build failure rather than a stale page.
