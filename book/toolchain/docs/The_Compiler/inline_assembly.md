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
