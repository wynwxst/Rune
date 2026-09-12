# Semantic analysis

`Sema::check()` in `Sema.cpp` is the whole of it, and reads as a list of
passes. Each one exists because the one after it needs something it produces.

```
  1  collectModule           every declaration gets a symbol
  2  findLangItems           locate Option, Result, As, Iterator, Sequence
  3  resolveImports          module scopes point at each other
  4  resolveShapes           fields, superclasses, mark hierarchies
  5  (deferred bounds)       bounds that needed every bind registered first
  6  resolveSignatures       parameter and result types
  7  reportSlotClashes       two binds claiming one method name
  8  checkStrongCycles       a class that can reach itself by strong references
  9  resolveDeinitialisers   which types own something a refcount cannot see
 10  drainPendingInstantiations
 11  checkBodies             statements and expressions
 12  (drains again, deinits again, clashes again)
 13  checkOwnershipOfQueued  borrows and escapes — in parallel
```

## Why this order

The interesting constraints are the ones that are not obvious:

**Shapes before signatures.** A signature can mention a field's type, so the
shape of every type has to be settled first.

**Bounds deferred past shapes.** A bound on an associated type may be answered
by a `bind` written *after* the one that has to satisfy it. Checking bounds
eagerly would make declaration order matter, so they queue in `DeferredBounds`
and are answered once every `bind` is registered.

**Deinitialisers before any body.** Handing an owning value away is a move, and
whether a value owns anything is exactly the question `resolveDeinitialisers`
answers. A body checked before the answer was known would emit the destructor
without the move, and destroy what it had already given away. That is why
`resolveDeinitialisers()` runs before `drainPendingInstantiations()` — before
*any* body, instantiated ones included.

**Slot clashes twice.** A generic binding only claims its method slots once
something instantiates the type it applies to, which may not have happened
until a body asked. So clashes are reported once after signatures and again
after bodies.

## Scopes and symbols

`Scope` is a linked chain: a module scope per module, all sharing one root so
builtins resolve everywhere, then a scope per block, per function, per match
arm. `pushScope` / `popScope` bracket each; `ScopeStorage` owns them for the
life of the compilation because AST nodes hold `Symbol *` into them.

`SyntheticSymbols` is a `std::deque`, not a `std::vector`, and the comment in
`Sema.h` says why: symbols manufactured during path resolution are handed out
as pointers, and a nested lookup can run in the middle of an outer one. A
vector would reallocate and invalidate what the outer lookup was holding.

## The tables

Almost everything Sema concludes about types lands in one of these, all keyed
by `Type *` because types are interned and therefore stable pointers:

| Table | Answers |
| --- | --- |
| `Methods` | `type.name` → the method that wins |
| `MethodOverloads` | every version of `type.name`, in claim order |
| `Operators` | `(type, op)` → every overload of that operator |
| `Conformances` | which marks a type is bound to |
| `MarkImpls` | `(type, mark)` → what that binding supplies |
| `MarkImplSets` | the same, but every version rather than the winner |
| `InherentMethods` | what a type declares itself, which a bind never displaces |
| `MethodClaims` | which binding holds a slot, and how specific it was |
| `AssocTypes` | `(type, mark)` → what the binding chose for each associated type |

`MethodOverloads` is keyed by `(Type *, std::string)` and is *ordered*, which
matters for performance: one type's slots are contiguous, so
`appendOverloadsFor` seeks to the first with `lower_bound` instead of walking
the map. That is asked once per generic instantiation, and the table grows with
every instantiation — walking it would cost the square of how much generic code
a program uses. It did, once. See [Performance](../Performance/index.md).

## What comes out

`SemaResult`, handed to CodeGen:

| Field | Is |
| --- | --- |
| `Functions` | every function to emit, instantiations and lifted closures included |
| `Globals`, `Nominals` | globals to define, types needing a layout |
| `EntryPoint` | `main` |
| `MethodTables`, `MarkTables` | for `dyn` dispatch |
| `DecoratorCalls` | one per `@decorator`, called before `main` |
| `AncillaryModules` | which modules were read rather than produced — see [What comes out](what_comes_out.md) |
