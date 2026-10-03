# Bare metal

## The idea

A hosted Rune program links two things it did not write: the C library and
`libruneruntime.a`. A freestanding one — `@runtime(none)` — links neither.
What the generated code still needs of a runtime is Rune source,
`runetime/freestanding.rune`, compiled into the program for the program's own
target, the way Rust builds `core` for whatever it is compiling for. That file
depends on nothing; what only the program can know, it asks the program for.

So the split is:

| Layer | Where it lives | Needs |
| --- | --- | --- |
| The language: checks, the borrow checker, classes, optionals | the compiler, and `std` where it is plain Rune | nothing |
| The freestanding runtime: panics, the heap, `memcpy`, 32-bit division | `runetime/freestanding.rune` | three hooks |
| The hooks: `@panicHandler`, `@allocator`, `@deallocator` | the program | whatever the machine offers |
| The hosted runtime: `String`, files, threads, tasks, counting | `runtime/` and `runetime/core.rune` | an operating system |

## In the compiler

**Options.** `CompilerOptions::Freestanding` and `NoEntry`, from
`--runtime none` / `--entry none`, or from `@runtime(none)` / `@entry(none)`
atop any of the program's files. The directives are read by
`programDirective` in `Compilation.cpp` *before* anything is parsed, because
freestanding decides which files make up the compilation: the runtime file is
added to the inputs then, for every output but a library (the program that
links the library brings one). The parser reads them again in
`parseFileDirectives`, which is where a misspelling is reported.

**Code generation** (`CodeGen.cpp`):

- `emitEntryPoint` generates no `main` under `NoEntry`; it generates
  `rune_init` instead, which runs the global initialisers. Freestanding,
  a `main` it does generate calls no `rune_runtime_init` and no leak report.
- Every defined function gets `no-builtins`, as `-ffreestanding` does for C,
  so LLVM never rewrites the runtime's byte loops into calls to themselves.
- `resolveWeakDefinitions` handles two definitions of one symbol in one
  module where one is `@weak`: the program's `@panicHandler` and the
  runtime's default are both `rune_panic_handler`. The strong one is kept,
  the weak one is never emitted. Across objects the linker does the same.
- The standard library's globals are initialised only when the program reads
  them (`emitBorrowedGlobalInitialisers`, after the bodies it reaches are
  emitted), and never torn down; `std::io`'s would otherwise need the hosted
  runtime just to exist.
- `reportHostedRuntimeUses` runs last: any `rune_*` function still only
  declared is the hosted runtime, and each is reported (E0542) against the
  program's own function that reached it — walking up through standard
  library functions, so `io::println` is blamed on the caller.
- `createTargetMachine` uses static relocation for an OS-less triple.
- A global whose initialiser is all zeros is left to the zeroed storage it
  starts in, hosted or not: nothing to run, and a `[0; 65536]` arena no
  longer becomes a 64 KB aggregate store.
- More generally, `constantValueOf` turns an initialiser made only of
  constants — literals, strings as `CString`, functions as `@cfunction`,
  arrays and structs of those — into an LLVM constant, and the global starts
  as it. Before this, TETRIS-OS's tables were built by `rune.init_globals`
  in a 21 KB stack frame, on a 16 KB stack that sat directly above the
  kernel's read-only data: the overflow zeroed its strings.

**String literals.** `Sema::CStringLiterals` is set for a freestanding or
`--no-stdlib` compile: a string literal in the program's own modules that is
not expected to be a `String` is typed `CString`, so `let s = "..."` needs no
annotation where `String` cannot exist. The standard library's modules are
unaffected, and an expected `String` still gets one — and E0542.

**The standard library, freestanding.** `ConfigSet::forOptions` sets the
`@Config` key `runtime` to `none` or `hosted`, after the `@runtime(none)`
directive has been read, so the library can keep a second definition for a
freestanding build. `process::panic` and `process::assert` do: the hosted ones
take any `Display` and build a `String`, the freestanding ones take a
`CString` and pass it straight to `rune_panic`. That is what lets `Vector`,
`Map` and `Set` — whose only hosted dependency was a literal panic message —
build on bare metal; examples/toyos runs them on its own heap.

**Sema.** `@panicHandler`, `@allocator` and `@deallocator` give a function a
fixed symbol (`langItemSymbol` in `AST.h`) and a fixed C signature, checked by
`checkRuntimeHook` (E0248, and E0249 for a second one). `@weak` is linkage
only.

**Linking.** `linkExecutable` passes `-nostdlib -static` to a compiler
driver — nothing to a linker run directly, and nothing at all under
`--no-default-link-args` — and leaves out the runtime archive, `-lm` and
`-rdynamic`. A linker script and anything else arrive through `--link-arg`.

**Text.** `runetime/freestanding_text.rune` is compiled in beside it: every
`rune_string_*` function the code generator and the standard library call,
over `rune_alloc` and the raw blocks, with the C runtime's `RuneString`
layout and FNV-1a hash; `rune_print` and friends over `@output`
(`rune_output`, reached through the strong `rune_write_output`); and numbers
as text. A double's exact decimal expansion is computed with big integers
(m·5^k for a negative exponent), the shortest digits whose value lies inside
the round-trip interval are chosen from it, and the hosted runtime's `%g` /
`%.Nf` presentation rules are reproduced — checked against the C runtime on
400,000 random doubles. Parsing divides out the exact fraction for a
correctly rounded result. The scratch space is global, so it is not
reentrant. Freestanding, a `String` literal is emitted as an immortal object
in the image (`.rune.strconst`) instead of a `rune_string_literal` call, and
in `+` a string literal checks after the other side so it can become a
`String`.

**Tiers.** `runec --tiers` compiles the whole standard library freestanding
(`TierReport` forces every function's body, under speculation so the eager
build's own complaints stay quiet) and `CodeGen::reportTiers` walks the IR:
a function is `bare` when nothing it reaches through calls, vtables,
descriptors or function pointers is left only declared, and otherwise names
the first such symbol. A generic template uses an instantiation the library
made, or its own calls resolved by name. `--emit-docs --docs-stdlib` runs the
same analysis in-process and writes a `tier` line per function, which
`rune-doc` shows as a **bare metal** / **hosted** badge; `bare_metal_test.py`
checks that every module builds and that known answers hold.

## In `runetime/freestanding.rune`

Everything the code generator calls, as `@export`ed Rune: the panic entry
points (formatting into a static buffer, since a panic may be the heap
running out), `rune_alloc` / `rune_drop` over the hooks, `$clone`, `is`,
`Any`, hashing, `memcpy` and friends, the raw blocks `std::mem` is built on
(`rune_raw_alloc`, `_realloc`, `_free`, `_is_zero` — with no header: every
caller passes the size back, `std::mem`'s `Allocator` included, and `rune_drop`
reads an object's from its `TypeInfo`, so `@deallocator` is told both), and `__divdi3` and its three siblings
under `@Config(pointer_width == "32")` — written with shifts and subtraction,
since a `/` there would call itself. Everything a program might want to
replace is `@weak`.

Adding something the code generator calls means adding it here too, or
freestanding programs that reach it fail with E0542.

### The minimal subset

`freestanding_type` is a `@Config` key every build knows, `full` unless a
package's `[config]` or `--cfg` says `minimal` (`ConfigSet::forOptions`; any
other value is E0545 in `Compilation.cpp`). The runtime's optional exports —
`rune_clone_object`, `rune_is_kind_of`, `rune_any_is`, `rune_hash_mix`, and in
`freestanding_text.rune` floats as text, text as numbers, the string hashes
and the string searching and slicing — carry
`@Config(!(freestanding_type == "minimal"))`, so under `minimal` they are
never declared. A program that calls one then has an undefined `rune_*`
symbol, which `reportHostedRuntimeUses` already turns into E0542 against the
function responsible; `minimalLeavesOut` in `CodeGen.cpp` names the feature
and says the full runtime has it. That table and the `@Config` marks have to
agree: moving a function in or out of the minimal set means changing both.
`minimal()` in `tests/bare_metal_test.py` checks both directions.

## In `rune`

`[build] runtime = "none"` and `entry = "none"`, or the directives in any
source file, set `Manifest::Freestanding` / `NoEntry`; the root package's
choice applies to the whole build, as `memory` does. A foreign target with
`Freestanding` — `bare-x86`, `bare-x86_64`, `bare-arm64`, `bare-riscv64` —
forces it. A freestanding build never prepares a hosted runtime.
`[build] linker-script` is passed as `-T <path>` to a linker and
`-Wl,-T,<path>` to a driver, and is part of the step's fingerprint, as is
the runtime file.

**No toolchain is assumed.** The bare targets default to `ToolchainKind::Clang`
because this machine's clang builds for every one of them with nothing
installed per target. That default is only a default: `cc`, `linker`,
`linker-kind`, `c-flags`, `link-args` and `default-flags` in the target's
table replace it piece by piece, and clang's own flags leave with clang (see
*Cross compilation*, "Flags belong to tools"). `rune_bare_metal` builds
toyos with stand-ins for `i686-elf-gcc` and `i686-elf-ld` that refuse any
clang or driver flag, and checks that the link line is the objects, `-o`
and `-T kernel.ld`.

What a linker cannot produce — a raw disk image laid out by load address —
is a [build script](build_scripts.md)'s finish phase; TETRIS-OS's makes one
with `objcopy` and names it with `runWith`, and its runner boots it with
`file={}`.

## Testing

`rune_bare_metal` (`tests/bare_metal_test.py`) builds every case in
`tests/freestanding/` as a Linux x86_64 program with no C library — two system
calls by inline assembly — and checks each failed check's message and exit
status, the heap's behaviour, the borrow checker, and E0542. Then it boots
`examples/toyos` under `qemu-system-i386`, normally and with `-append panic`,
debug and release, and `examples/tetris-os` from its own boot sector,
driving it through QEMU's monitor (`sendkey`, `screendump`). Each part is
skipped where the machine cannot run it.
