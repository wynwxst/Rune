# The invariants

Five properties the toolchain currently has. Each is worth about an order of
magnitude somewhere, each is easy to break by accident, and none of them
announces itself when broken — the tests still pass, things just get slower.

## 1. Only reached code is lowered

Sema hands CodeGen every function in the compilation, standard library
included — around a thousand for a hello-world. CodeGen emits bodies eagerly
only for code the artefact **owns**, then loops emitting borrowed functions
whose `llvm::Function` is still a declaration with a non-empty use list, to a
fixed point.

Borrowed symbols get `linkonce_odr` so `GlobalDCE` may drop them. A
hello-world object holds 17 functions rather than 918, and is 12 KB rather than
626 KB.

> **`offered` is load-bearing.** Generic templates and bodyless mark
> requirements never gain a body, so a loop that judged by the result would
> offer them again forever. Each candidate is offered once. Removing that set
> hangs the compiler.

Breaking this — by giving borrowed code `weak_odr`, or emitting all bodies
eagerly — costs roughly 10× on machine-code emission and does not fail a single
test.

The same rule now covers *declarations*. `CodeGen::run` used to call
`declareFunction` on every function Sema handed it and `emitTypeInfo` on every
class, borrowed or not — cheap per item, but a thousand items, each lowering
its whole signature, was most of what a hello world spent in the code
generator. Both are made on demand, where a call, a vtable, a global or a
construction first names them, and the eager loop runs only over what the
artefact owns. That took a hello world's `codegen` stage from ten milliseconds
to under one. Nothing forces a borrowed declaration to exist before something
reaches it; the fixed-point loop above asks `Functions.find(fn)`, which is
exactly the "has anything reached it" test.

Two smaller ones in the same spirit, in `emitArrayLit` and the standard
library:

- an array of scalar literals — `[0u32; 256]`, a table of round constants —
  is one constant aggregate, not a store per element. A global table
  initialised the store-per-element way costs every program its
  initialiser;
- a library global whose initialiser does work — `random`'s shared generator,
  `hash`'s CRC table — is initialised lazily on first use, with a value the
  code can recognise as "not yet" (an all-zero generator state, an empty
  table entry that is never zero once built). Globals are always emitted,
  whether or not anything reaches them, so an eager one is a cost every
  program pays at start-up.

## 2. The back end's optimisation level follows `-O`

`createTargetMachine` defaults to `CodeGenOptLevel::Default`, which is `-O2`,
if it is not told otherwise. `Compilation.cpp` passes
`codeGenOptLevel(opts.OptLevel)`. Drop that argument and every debug build pays
for scheduling and register allocation it did not ask for.

## 3. Nothing in the instantiation path walks a whole table

See [Where the time goes](where_the_time_goes.md). The cost is quadratic in how
much generic code a program uses, so it is invisible on small tests and
crippling on real ones.

## 4. Reporting flags are not build inputs

`rune` decides whether to skip a compile by hashing the command line it would
run. `-v`, `--color` and `RUNE_JOBS` are appended at spawn time in `runStep`,
never in `appendBuildFlags`. Put one of them in the fingerprint and every
toggle rebuilds the world, twice.

## 5. Parallel passes stay deterministic

The ownership pass runs on every core, and reports in the order its bodies were
**queued** rather than the order threads finished — each body reports into its
own bucket, and the buckets are replayed in sequence.

Any pass parallelised later should do the same. Deterministic output is what
makes build logs diffable and `EXPECT-ERROR` tests meaningful; a pass that
reports in completion order is a flaky test generator.

## What is not parallel, and why

Type checking itself is sequential. Its passes share a `TypeContext` that
interns types, a scope arena that every block allocates from, a symbol deque
handed out as pointers, and a dozen tables keyed by `Type *` — and generic
instantiation, which is where most of the time goes on large programs, mutates
all of them. Parallelising it would mean either a lock held almost
continuously, which buys nothing, or giving each worker its own arena and
walk context and restoring a deterministic order afterwards.

The ownership pass is parallel precisely because it is the one part that shares
none of that: it takes a function, reads only that function's tree, and
resolves nothing.
