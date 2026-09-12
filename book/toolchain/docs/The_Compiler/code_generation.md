# Code generation

`CodeGen` lowers the typed AST to LLVM IR. `CodeGen.cpp` handles shapes —
types, layouts, functions, globals, vtables, type info, debug info — and
`CodeGenExpr.cpp` handles expressions and intrinsics.

## The reference-counting convention

This is the part worth knowing before changing anything, and it is stated at
the top of `CodeGen.h`:

- A function returns reference-counted results at **+1**: the caller owns it.
- `emitRValue` always hands back a **borrowed (+0)** value. Anything arriving at +1 is parked in the statement's temporary list, so the statement owns it and releases it when it finishes.
- Storing into a slot **retains** the incoming value and **releases** the old one.
- Leaving a scope releases every local it declared.

That costs a little extra traffic compared with a flow-sensitive scheme, but it
never leaks and never over-releases — and it is auditable, which a
flow-sensitive scheme would not be. The one place it is relaxed is
`AdoptedResult`: a fresh construction stored into a local the ownership pass
proved never escapes skips the retain/store/release round trip, because the
allocation's own count becomes the binding's.

## `run()`, in order

```
  lower every nominal type, emit type info for classes
  declare every global and every function
  emit the `rune_debug_build` flag
  init debug info
  emit global initialisers, decorator calls, global teardown
  emit bodies for this artefact's own functions
  emit the entry point
  emit borrowed bodies, to a fixed point        ← see below
  finish debug info
  prune                                          ← GlobalDCE
  verify
  optimise, if -O1 or above
```

## Linkage, and what it means

| Linkage | Given to |
| --- | --- |
| `External` | `main`, `extern` declarations, `@export` |
| `WeakODR` (or COMDAT on COFF) | This artefact's own public functions and methods |
| `LinkOnceODR` (plus COMDAT on COFF) | Anything borrowed: the standard library, an imported library's generics |
| `Internal` / `Private` | Thunks, string literals, non-public non-methods |

The distinction between `weak_odr` and `linkonce_odr` carries the whole of the
emission policy, so it is worth being precise about it. Both mean "identical
wherever it appears, keep one". Only `linkonce_odr` also means "and a copy
nothing uses may be dropped". A library's exports must survive even when
nothing inside the library calls them, so they get `weak_odr`; a copy of
somebody else's code is exactly what may be dropped, so it gets `linkonce_odr`.

`setMergeableLinkage` and `setDiscardableLinkage` are the two helpers, and both
attach a COMDAT on COFF because PE's weak externals do not mean what ELF's weak
symbols mean.

## Intrinsics

An `@intrinsic("name")` function in the standard library has no body. The
compiler answers the call itself, in `CodeGenExpr.cpp`, from the layout it is
already computing:

```rune
@intrinsic("size_of")
pub fn sizeOf<T>() -> usize
```

```cpp
if (target && target->hasAttr("intrinsic")) {
  ...
  if (arg && (which == "size_of" || which == "align_of")) {
    llvm::Type *lowered = lower(arg);
    const llvm::DataLayout &dl = M->getDataLayout();
    ...
  }
}
```

An intrinsic the switch does not recognise reports "this intrinsic is not
supported" rather than emitting something wrong.

## Other things emitted here

| Thing | Made by |
| --- | --- |
| Class deinitialisers | `emitTypeInfo`, walking fields |
| Mark vtables | one per `(mark, concrete type)` |
| Mark thunks | adapting a concrete method to the uniform `(ptr self, ...)` shape |
| Closure environments | a struct per `ClosureExpr`, with its own deinit |
| Box info | type metadata for a value boxed into `Any` or a mark object |
| `rune_debug_build` | a global the runtime reads to decide whether to print a traceback |
