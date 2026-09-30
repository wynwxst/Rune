# Inline assembly

`std::asm` is two `@intrinsic` declarations with no bodies, answered in
`CodeGen::emitInlineAsm`. There is no new syntax: the intrinsic mechanism
already exists for exactly this — things the compiler must answer itself — and
using it meant no change to the lexer, the parser, the AST or any of the three
tree helpers.

```rune
@unsafe
@intrinsic("asm")
pub fn run(template: String, constraints: String, ...)

@unsafe
@intrinsic("asm_value")
pub fn value<R>(template: String, constraints: String, ...) -> R
```

Three things come for free from that declaration:

- `@unsafe` means a call needs `unsafe { ... }`, through the same check that guards foreign calls. Nothing had to be taught about assembly.
- `...` makes the operand list variadic, so the operands keep their own types.
- `<R>` gives the value form its result type, reachable in the code generator as `target->TypeArguments[0]`.

## Lowering

`emitInlineAsm` reads the first two arguments as string literals, emits the
rest as values, builds a `FunctionType` from their LLVM types and the result
type, and calls an `llvm::InlineAsm`.

```cpp
auto *fnTy = FunctionType::get(ret, operandTypes, /*isVarArg=*/false);
if (llvm::Error err = llvm::InlineAsm::verify(fnTy, constraints)) { ... }
auto *callee = llvm::InlineAsm::get(fnTy, templateText, constraints,
                                    hasSideEffects);
CallInst *call = B->CreateCall(callee, operands);
```

`InlineAsm::verify` is the part worth keeping. LLVM knows whether the
constraints describe this signature — how many operands, which are results,
whether the target has such registers — and asking first turns what would be an
assertion inside the back end into a diagnostic pointing at the call, carrying
LLVM's own explanation as a note.

Before that, on x86, GCC's x87 letters are rewritten the way Clang rewrites
them: `t` is `{st}` and `u` is `{st(1)}`, outside any braces. LLVM's own
constraint parser does not know them, and code ported from C uses them.

## Errors only the back end finds

`verify` knows the constraints, not the template. An instruction the
assembler does not know, or a register named in the text that does not exist,
surfaces while `writeMachineCode` runs the back end — long after the AST is
gone. Two things bring it back to the source:

- `emitInlineAsm` attaches `!srcloc` to every call. A `SourceLoc` is already a
  single offset into the `SourceManager`'s global space, so the cookie is just
  `c->Range.begin().raw()`; nothing needs a side table.
- `writeMachineCode` installs `BackendDiagnostics` as the `LLVMContext`'s
  handler for the length of the pass pipeline. It takes the cookie from a
  `DiagnosticInfoInlineAsm` (register allocation) or a `DiagnosticInfoSrcMgr`
  (the assembler, which also supplies the offending line), turns it back into
  a `SourceRange`, and reports E0509 there. Any other back-end error is
  reported too, without a location, instead of LLVM's bare `error:` on
  stderr.

If anything was reported as an error, the half-written object or assembly
file is removed and the step fails, so a later link cannot pick up an empty
`.o`.

## `hasSideEffects`

The one semantic decision. It is what tells LLVM whether the call may be
dropped when nothing uses its result, and whether two identical calls may be
merged.

| Call | `hasSideEffects` | Because |
| --- | --- | --- |
| `asm::run` | true | It returns nothing, so its effects are its whole purpose |
| `asm::value` | false | It is a function of its inputs, and should optimise like one |

That split is why there are two functions rather than one with a flag: the
right answer is different often enough that a default would be wrong half the
time, and getting it wrong is a bug that only appears at `-O2`.

## Literals, checked in Sema

`checkReflectionCall` in `SemaExpr.cpp` requires the first two arguments to be
`StringLitExpr`. The template and the constraints are part of what is being
compiled rather than something it computes, so a value that varies has nothing
to hand the assembler. Checking there rather than in the code generator means
the diagnostic arrives during `--check`, and means `emitInlineAsm` can treat a
non-literal as a compiler bug rather than a user error.
