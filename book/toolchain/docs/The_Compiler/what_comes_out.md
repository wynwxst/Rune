# What comes out

## The emission policy

A compilation reads far more than it produces. The whole standard library is
lexed, so a macro declared anywhere in it is in scope everywhere. The part a
program can reach — the modules it names, the ones the language leans on, and
everything those name — is parsed and checked, and a compile that fails
against that part is done again against all of it, so a `bind` written in a
module nothing imports still answers for a program that relied on it. Only
some of what is checked is emitted.

The rule is about **ownership**, not reachability alone:

| Code the artefact… | Emission |
| --- | --- |
| **owns** — the files named on the command line | always emitted, called or not |
| **borrows** — the standard library, an imported library's generics | emitted only where this artefact reaches it |

Owned code is emitted unconditionally because a `.rul` has to carry every
public thing it declares; the library cannot know what an importer will call.
Borrowed code is a copy of somebody else's, and a copy nothing uses can go.

`SemaResult::AncillaryModules` is what says which is which. Sema fills it in
`addModule`, from `Module::IsStdlib` and `Module::FromLibrary`, and CodeGen asks
`isAncillary(decl)` by module path.

## How borrowed code is emitted

Owned bodies are emitted eagerly. Then borrowed ones are emitted only where the
module turns out to refer to them:

```cpp
for (bool more = true; more;) {
  more = false;
  for (FunctionDecl *fn : Sema.Functions) {
    if (!isAncillary(fn) || fn->hasAttr("intrinsic")) continue;
    auto it = Functions.find(fn);
    if (it == Functions.end() || !it->second->isDeclaration() ||
        it->second->use_empty())
      continue;
    if (!offered.insert(fn).second) continue;      // once each, no more
    emitFunctionBody(fn);
    more = true;
  }
}
```

A function still standing as a bodyless declaration *with users* is one
something here reached — through a call, a vtable slot, a global's initialiser,
a thunk, or metadata. Asking the IR rather than walking the AST is the point:
every way one function can name another ends up as a use, so no list of
reference kinds has to be kept correct as the code generator grows.

> **`offered` is load-bearing.** Not everything asked for has a body to give —
> a generic template has none until it is instantiated, a mark's requirement may
> have none at all. A loop that judged by the *result* would offer those again
> forever. Each candidate is offered once.

Emitting a body can reach further, so the loop runs to a fixed point.

## Pruning

`pruneUnreachable()` then runs `GlobalDCE` over the finished module. The
emission loop already declined most of what was not needed, but it decides by
asking whether anything refers to a function, and something may refer to a
function that is itself about to go — a `deinit` named by metadata for a type
nothing constructs, a vtable for a mark object whose last use was in a function
that was not emitted. This settles all of it at once, over a module that is no
longer a moving target.

Only discardable definitions are candidates. Everything owned, exported or
foreign keeps linkage that says "somebody outside may want this", and is a root
of the walk rather than a casualty of it.

Verification runs *after* the prune: the module that goes to the back end is
the one worth checking, and there is a great deal more of it before.

The effect on a hello-world is 918 functions down to 17, and an object of
626 KB down to 12 KB.

## Machine code

`writeMachineCode` asks LLVM for a `TargetMachine` and runs
`addPassesToEmitFile`. Two things to know:

- The back end runs **its own** optimisation pipeline, separate from the IR pipeline in `optimizeModule`, and left to itself it picks the `-O2` one whatever `-O` asked for. `codeGenOptLevel(opts.OptLevel)` is passed so a debug build does not pay for scheduling and register allocation it did not want.
- Target registration (`InitializeAllTargets` and friends) happens once per process, in `initialiseTargets()`. It is idempotent but not free — it runs the constructor of every target LLVM knows, and this build knows all of them.

## The `.rul` format

`Library.cpp`. A little binary container: the magic `RUNELIB\1`, a version, the
object file, and one entry per module holding its name and its **full source
text**. Nothing clever — the interesting decision is that the source is in
there at all, which is what lets an importer instantiate the library's generics
for its own types.

Reading one back extracts the object to a temp file for the linker. That
filename carries the process id, because several compilers may be running at
once and more than one of them may import the same library — a fixed name means
one deleting its extracted object on the way out while another is still linking
against it.

## Linking

`linkExecutable` hands the object to the system C toolchain, which already
knows where libc, the startup files and the linker are. A cross build names its
own driver (`--cc`), and that driver knows its own target's.

The command is built as an **argument list and spawned directly**, not handed to
a shell. `system()` would start `/bin/sh` to re-parse a command line this then
has to quote correctly — two processes and a quoting problem, for a program
whose arguments are already a list. `posix_spawnp` skips both, so a path with a
space, a quote or a dollar in it is simply an argument.

`-lm` is added only where there is a libm to add: Windows keeps the maths
functions in the C runtime and Apple's platforms keep them in libSystem, and on
both, asking sends the driver looking for a library that is not there.
