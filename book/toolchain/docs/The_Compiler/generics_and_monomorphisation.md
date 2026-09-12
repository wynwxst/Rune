# Generics and monomorphisation

Rune's generics are monomorphised: `Vector<i64>` and `Vector<String>` are two
different types with two different sets of methods, produced by cloning the
template and substituting.

## Instantiating a type

`Sema::instantiateNominal(tmpl, args, range)`:

1. Build a key from the template's address and the arguments' addresses.
2. If `NominalInstances` already has it, return the existing instantiation.
3. Otherwise `cloneDecl` the template, record it in `Synthesised` (Sema owns it for the rest of the compilation), and register it under the key *before* going further — so a type that mentions itself finds the in-progress instantiation rather than starting another.
4. Bind the arguments, resolve the instantiation's shape and signatures, work out its deinitialiser, and check its bodies.

The key is built from **pointers, not spellings**:

```cpp
std::string key = instantiationKey(tmpl, args);   // "0x1234|0x5678"
```

The comment in the source says why, and it is worth repeating: two modules may
each declare an `Item`, and `Vector<Item>` means a different type in each. A key
built out of names would hand the second one the first one's instantiation, and
every value would then be the wrong type by the name it goes by.

`NominalDepth` caps the recursion at 24 and reports, so
`struct Nest<T> { deeper: Nest<Nest<T>> }` gets a diagnostic instead of a stack
overflow.

## Instantiating a function

Same idea, driven from the call site. `InstantiationStack` records the chain so
that a failure deep inside a generic can point back at the call that triggered
it, rather than at a line in the standard library.

## Deferred methods

Some methods cannot have their signature resolved at instantiation time —
`signatureWrapsSelf(f)` catches the ones whose signature mentions `Self` in a
way that is only settled at the call. Those go into `DeferredMethods` and are
resolved at the call site instead. `drainPendingMethods()` and
`drainStructuralBodies()` are the corresponding worklists.

## Symbol names

`Sema::mangleFunction` builds the linker symbol. The shape is
`_R<len><module>T<len><type>F<len><name>G<type args>`, with everything
non-alphanumeric replaced by `_`:

```
_R24std__collections__vectorT24Vector_std__io__DirEntryF6lengthG17std__io__DirEntry
```

Three exceptions bypass mangling entirely:

| Case | Symbol |
| --- | --- |
| `extern` | The name C exports, or `@as("...")` if given |
| `@export("name")` | Exactly `name` |
| `main` | `main` |

A per-type symbol **must** name the instantiation, not the template. If it
names the template, two instantiations collide at the link and one silently
answers for the other — which is exactly the failure mode a `.rul` boundary
makes easy to introduce, because the library and its importer each generate
their own copies.

## Why this is the expensive part

Monomorphisation is real work proportional to how much generic code a program
uses: a clone, a shape resolution, a signature resolution and a body check per
instantiation. On a program with hundreds of distinct instantiations it
dominates everything else in the compiler. Anything in that path that is linear
in the number of instantiations becomes quadratic overall — see
[Performance](../Performance/index.md) for the one that was.
