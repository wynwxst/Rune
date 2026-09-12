# Adding a type rule

## Where rules live

| Kind of rule | Goes in |
| --- | --- |
| About an expression — a call, an operator, a member, a cast | `SemaExpr.cpp` |
| About a declaration — a field, a signature, a bind, an extend | `Sema.cpp` |
| About a whole program — cycles, clashes | `Sema.cpp`, in its own pass |

## Which pass

Ask what has to be true already:

- Needs only the *shape* of types (fields, superclasses)? → `resolveShapes`.
- Needs *signatures*? → `resolveSignatures` or later.
- Needs to know whether a type owns anything? → after `resolveDeinitialisers`.
- Needs every `bind` registered? → after `resolveShapes`, or defer it the way `DeferredBounds` does.

Getting this wrong usually shows up as a rule that works in one file order and
not another. If your rule depends on something that is only true later, defer
it into a worklist rather than moving the pass — the pass order encodes real
constraints, and there is a comment above each one saying which.

## Adding a new nominal kind, or a new `TypeKind`

Adding a `TypeKind` reaches further than most changes. Everything in `Type.cpp`
that switches on the kind has to answer for it:

- `isRefCounted`, `isPointerLike`, `containsGenericParam`, `toString`
- `CodeGen::lower` — what LLVM type it becomes
- `collectStrongEdges` in `Sema.cpp` — whether it can hold a strong reference
- `reflectKindOf` and `reflectFieldCount` in `CodeGen.cpp` — what `std::reflect` says about it

Prefer expressing a new idea with the kinds that exist. `DynMark` and `Any` are
both existentials built on the same machinery, and neither needed a kind of its
own until it did. `some Mark` went further: rather than a kind every switch
has to answer for, it is a *flag* on a type that takes on the concrete type's
kind once it is known, so the twenty-odd kind switches never see it — see
[The type system](../The_Compiler/the_type_system.md). A new idea that is
"an existing type, with a rule attached" wants that shape, not a `TypeKind`.

## Poisoning, not cascading

When a rule fails, report once and return `Types.errorType()`. Every rule above
carries on without a second diagnostic, because `Type::isError()` is checked
before anything else complains. The alternative — returning null — means every
caller needs a null check and a user gets six diagnostics for one mistake.

```cpp
if (!ok) {
  Diags.error(range, "…").note("…").code(2NN);
  return Types.errorType();
}
```
