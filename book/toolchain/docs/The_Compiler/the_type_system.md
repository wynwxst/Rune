# The type system

## Types are interned

`TypeContext` owns every `Type` and hands out pointers. Two types that are the
same type are the *same pointer*, so comparison is `a == b` and a `Type *` is a
valid map key. That property is relied on all over Sema — every method and mark
table is keyed by `Type *`.

```cpp
Type *a = Types.intType(64, /*signed=*/true);
Type *b = Types.intType(64, /*signed=*/true);
assert(a == b);                       // interned: literally the same object
```

The context is constructed with the target's pointer width, taken from the
LLVM data layout for the triple being built for, so `usize` and `isize` are
whatever a pointer is on the machine the code is *for*, not the machine it is
compiled on.

## The kinds

| Kind | Is |
| --- | --- |
| `Error` | Poisoned. Suppresses cascading diagnostics — nothing reports twice about a type that is already wrong. |
| `Void` | `()`, the empty tuple |
| `Never` | The type of `return`, `break`, and a diverging call |
| `Bool`, `Int`, `Float`, `Char` | Scalars |
| `String` | Heap allocated, reference counted, UTF-8 |
| `CString` | A borrowed NUL-terminated `*u8`, for FFI |
| `Pointer` | `&T`, `&var T`, `*T`, `*var T` |
| `Array`, `Slice`, `Tuple` | `[N:T]`, `[T]`, `(A, B)` |
| `Function`, `CFunction` | With and without an environment |
| `Struct`, `Enum`, `Class`, `Mark` | Nominal |
| `DynMark` | `dyn Mark` — an existential carrying a vtable |
| `Any` | A value of unknown type carrying its own descriptor |
| `Generic` | An unsubstituted parameter |
| `Opaque` | `some Mark` before the function returning it has been checked; see below |

`Error` and `Never` are the two that make the checker pleasant to work in.
Returning `Error` from a rule that has already reported means every rule above
can carry on without a second diagnostic. `Never` means `if c { 1 } else
{ return }` types fine.

## `some Mark` is a veil, not a kind

A function declared `-> some Shape` returns a concrete type its body decides
and its callers never see. The type is interned once per function
(`TypeContext::opaqueOf`), so it compares equal to nothing but itself — that
is the opacity. Inside, once the body has fixed it, it *is* the concrete type:
`resolveOpaque` copies the underlying type's whole representation into it,
kind included, keeping only the flag, the owner and the mark. Every question
about representation — `kind()`, `nominal()`, `isRefCounted()`, `lower()` —
is answered as for the type behind it, without anything having to know it was
opaque. `TypeKind::Opaque` is only what the type is *before* that, and no
kind switch should ever see it after Sema.

The checker is where the veil matters, and `isOpaque()` is the test:
`lookupMethod` reaches only what the mark supplies, `checkMember` shows no
fields, `checkPattern` refuses to take it apart, `isImplicitlyConvertible`
converts it to nothing but wrappers (`Option`, `dyn`, `Any`), and
`requireConvertible` is where the *defining* function — and only it — fixes
the type from the first value it returns.

The order problem: a caller may be checked before the function's body. The
type exists from the signature pass, and `checkExpr` resolves an unresolved
one on demand by checking the owner's body then (`resolveOpaqueNow`), with
`OpaqueContext` holding what that needs — `Self`, the bound generics — and a
`Resolving` flag that turns a body reaching its own result into a diagnostic
rather than a loop. Mangling uses the owner's symbol (`some<_R…F4make>`), so
a generic instantiated over the opaque type is a different instantiation from
one over the concrete type, and the same one in every compilation that sees
the function.

## Questions the rest of the compiler asks a type

| Method | Used for |
| --- | --- |
| `isRefCounted()` | Whether values need retain/release traffic |
| `isPointerLike()` | Whether it is one machine pointer |
| `isTriviallyCopyable()` | Whether a bitwise copy is a valid copy |
| `containsGenericParam()` | Whether it still has to be substituted |
| `isNominal()` / `nominal()` | Getting back to the declaration |

`isRefCounted` is the one that decides most of the code generator's behaviour:
classes, `String` and closures own a heap allocation, and aggregates inherit
the property from their elements.

## Type expressions and resolution

A `TypeRepr` is a type *as written* — `NamedTypeRepr`, `PointerTypeRepr`,
`ArrayTypeRepr` and so on. `Sema::resolveType` turns one into a `Type *`, and
`resolveTypeOrError` does the same but yields `Error` rather than null when it
cannot. Keeping the two apart matters: an alias nobody ever used as a type is
never resolved, which is why `--emit-docs` prints its written form rather than
its resolved one.
