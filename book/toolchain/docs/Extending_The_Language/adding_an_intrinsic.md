# Adding an intrinsic

An intrinsic is a function the standard library *declares* and the compiler
*answers*. Use one when the answer is something only the compiler knows — a
layout, a type name, whether a conformance holds — or something that has to
lower to a specific instruction.

## 1. Declare it in the standard library

```rune
#intrinsic("align_of")
pub fn alignOf<T>() -> usize
```

No body. The name in the string is what the code generator switches on; the
Rune name is what users write.

## 2. Answer it in the code generator

`CodeGenExpr.cpp`, in the block that runs when the call target has the
attribute:

```cpp
if (target && target->hasAttr("intrinsic")) {
  const Attribute *a = target->findAttr("intrinsic");
  std::string which;
  if (a && !a->Args.empty())
    if (const auto *lit = dyn_cast<StringLitExpr>(a->Args[0].get()))
      which = lit->Value;
  Type *arg = target->TypeArguments.empty() ? nullptr : target->TypeArguments[0];

  if (arg && (which == "size_of" || which == "align_of")) {
    llvm::Type *lowered = lower(arg);
    const llvm::DataLayout &dl = M->getDataLayout();
    uint64_t v = which == "size_of"
                     ? dl.getTypeAllocSize(lowered).getFixedValue()
                     : dl.getABITypeAlign(lowered).value();
    return ConstantInt::get(lower(c->Ty), v);
  }
  ...
  reportUnsupported(c->Range, "this intrinsic");
}
```

`target->TypeArguments[0]` is the `T` the call was instantiated with — that is
how a generic intrinsic gets at its type argument.

The fall-through calls `reportUnsupported`, so an intrinsic you declared but
did not implement gives a diagnostic rather than emitting something wrong.

## Compile-time versus run-time

Most of `std::reflect` is compile-time: `sizeOf<i64>()` is the constant `8` by
the time the program runs, and `kindOf<T>()` has already chosen its branch. A
call costs what its answer costs, which is usually nothing. Prefer that: an
intrinsic that folds to a constant is better than a runtime helper, and better
than a descriptor carried around.

`Any` is the counter-example — it answers a narrower question about a *value*
rather than a type, so it carries a descriptor and is checked at run time.

## A worked example

`std::asm` is an intrinsic that turned out to need no new syntax at all: two
declarations, one case in the code generator, one check in Sema. It is written
up in [Inline assembly](../The_Compiler/inline_assembly.md), and is worth
reading before adding a feature that looks like it needs grammar — the
intrinsic route is often enough, and costs a great deal less.

## Intrinsics that need a runtime function

If the answer needs the runtime rather than the compiler, do not use an
intrinsic at all: declare it `extern "C"` in the standard library and implement
it in `runtime/src/rune_runtime.c`. That is how file I/O, sockets and threads
work. An intrinsic is for things the *compiler* knows.
