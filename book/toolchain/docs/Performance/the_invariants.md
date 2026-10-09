# The invariants

Nine properties the toolchain currently has. Each is worth about an order of
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

## 6. Only the standard library a program reaches is checked

`stdlibReach` in `Compilation.cpp` reads the token streams before anything is
parsed and finds every `std::…` path, including `std::{a, b}` and `std::a::*`
imports. It adds the modules the compiler leans on by itself: `option`,
`result`, `convert`, `iter`, `thread`, `dictionary`, `io`, `fmt`, `any` and
`mem`, plus `task` when `async` or `await` appears. Then it closes over what
those name. Every stdlib file is still *lexed*, because a macro declared
anywhere is in scope everywhere.

A `bind` or `extend` in a module nothing reaches would be lost, and `std::json`
binds `As<Value>` to builtins. So the narrowed attempt holds its diagnostics
(`DiagnosticEngine::hold`) until checking passes. If it fails, it is thrown
away and the compile runs again against the whole library. A program that
leant on such a bind still builds, and every error reads exactly as it did.
`--whole-stdlib` skips the attempt.

Tested by comparing the narrowed and whole builds of every case: identical
exit codes, diagnostics and set of emitted functions.

## 7. The borrow checker analyses only reached library bodies

`zombie::checkProgram` lowers the program's own bodies, then follows call
edges into the standard library and imported libraries, wave by wave.
Nothing else consults an unreached body's summary.

> **Lowering is load-bearing for code generation.** It writes `ZombieAlias`,
> `ZombieInPlace`, `SubjectHold` and `ZombieSubjectInPlace` onto the AST, and
> the move pass writes `ZombieMoved`. A deferred body that CodeGen emits
> anyway — through a vtable, a `deinit`, a function value — goes through
> `zombie::prepareDeferred` in `emitFunctionBody` first. Skip that and the
> generated code frees what it should not, or leaks.

`--zombie-whole-stdlib` analyses everything, and the end-to-end case
`B8_zombie_whole_stdlib` runs it, so a finding introduced anywhere in the
library still fails the suite.

## 8. Codegen units depend on the module, never the machine

`codegenUnitsFor` picks one piece per 10,000 instructions, up to 16, or what
`--codegen-units` says. `RUNE_JOBS` and the core count only decide how many
pieces are built at once. Tie the count to the machine and the same program
builds into a different executable on every computer.

The cut is made after the optimiser, and the module is written as bitcode
**once**: each piece loads it lazily and materialises only its own bodies.
`llvm::SplitModule` cloned and wrote the module once per piece, on one
thread, which ate most of the gain. Locals named across a cut become hidden
externals. Pure-data private constants such as string bytes are copied to
each piece instead, so no string becomes a global symbol.

## 9. Symbols do not depend on what else is in the compilation

A closure is named for the function it is written in and its position
there (`main#closure1`, `Box::map#closure0<i64>`). The name is spliced into
the enclosing function's own symbol. It used to come from a counter across the
whole compilation, so a library's build and its importer's numbered the same
closures differently. With `-g`, which makes closures mergeable, two
different closures could share one symbol and the linker would keep either
body for both. Any new generated name has to be a function of its source
position, not of how many things came before it.

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
resolves nothing. Lexing, parsing, the borrow checker and the back end
(codegen units) are parallel for the same reason.
