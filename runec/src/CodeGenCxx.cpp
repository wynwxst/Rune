//===- CodeGenCxx.cpp - Calling C++: the calling convention ---------------===//
//
// A C++ library was compiled by a C++ compiler, which passed every argument
// the way the platform's C++ ABI says: `this` first; a small struct split
// across registers or packed into one; a large one copied to memory and
// passed by address; a result too wide for the return registers written
// through a hidden pointer the caller supplies. LLVM's own lowering of a
// first-class aggregate agrees with none of that in general, so a call into
// C++ is lowered here the way Clang lowers it, target by target:
//
//   x86-64 SysV     eightbyte classification (INTEGER / SSE), registers
//                   counted, `byval` when they run out or the struct is wide
//   AArch64         homogeneous float aggregates as `[N x float]`, anything
//                   else up to 16 bytes as `i64` / `[2 x i64]`, larger ones
//                   by pointer
//   Windows x64     1, 2, 4 and 8 bytes as an integer, anything else by
//                   pointer
//   i386            everything `byval` on the stack; a MinGW build returns
//                   1, 2, 4 and 8 bytes in registers and uses `thiscall`
//
// The rules were checked against Clang's output for each shape, and the
// tests under `tests/cases` and `examples/project/ffi` run them against a
// library built with GCC and Clang on the host and under MinGW.
//
//===----------------------------------------------------------------------===//
#include "rune/CodeGen.h"
#include "rune/CxxInterop.h"

#include <llvm/IR/Attributes.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/TargetParser/Triple.h>

using namespace llvm;

namespace rune {

namespace {

enum class Arch : uint8_t { X86_64, AArch64, X86, Wasm, Other };

struct AbiTarget {
  Arch arch = Arch::Other;
  bool windows = false;
  bool darwin = false;
};

AbiTarget abiTargetOf(const llvm::Triple &t) {
  AbiTarget a;
  switch (t.getArch()) {
  case llvm::Triple::x86_64: a.arch = Arch::X86_64; break;
  case llvm::Triple::aarch64:
  case llvm::Triple::aarch64_be: a.arch = Arch::AArch64; break;
  case llvm::Triple::x86: a.arch = Arch::X86; break;
  case llvm::Triple::wasm32:
  case llvm::Triple::wasm64: a.arch = Arch::Wasm; break;
  default: break;
  }
  a.windows = t.isOSWindows();
  a.darwin = t.isOSDarwin();
  return a;
}

/// One scalar inside an aggregate, at its byte offset from the start.
struct Leaf {
  uint64_t Offset;
  llvm::Type *Ty;
};

/// The scalars an aggregate is made of, in memory order. Padding leaves
/// nothing behind, which is how a classification sees past it.
void flattenLeaves(llvm::Type *ty, uint64_t base, const DataLayout &dl,
                   std::vector<Leaf> &out) {
  if (auto *st = dyn_cast<StructType>(ty)) {
    const StructLayout *layout = dl.getStructLayout(st);
    for (unsigned i = 0; i < st->getNumElements(); ++i)
      flattenLeaves(st->getElementType(i), base + layout->getElementOffset(i),
                    dl, out);
    return;
  }
  if (auto *at = dyn_cast<ArrayType>(ty)) {
    uint64_t stride = dl.getTypeAllocSize(at->getElementType());
    for (uint64_t i = 0; i < at->getNumElements(); ++i)
      flattenLeaves(at->getElementType(), base + i * stride, dl, out);
    return;
  }
  out.push_back({base, ty});
}

bool isFloatLeaf(llvm::Type *t) { return t->isFloatTy() || t->isDoubleTy(); }

} // namespace

llvm::Type *CodeGen::lowerCxxClass(NominalDecl *nd) {
  // Opaque: Rune sees a run of bytes and never looks inside. Without a size,
  // one byte — enough to make `*var T` a pointer to *something*, and
  // `cxx::alloc` says why that is not enough for it.
  uint64_t size = nd->Cxx && nd->Cxx->Size ? nd->Cxx->Size : 1;
  return ArrayType::get(B->getInt8Ty(), size);
}

CodeGen::CxxArg CodeGen::classifyCxxArgument(Type *t, unsigned &intRegs,
                                             unsigned &sseRegs, bool isReturn,
                                             SourceRange where,
                                             const std::string &role) {
  CxxArg out;
  const AbiTarget tgt = abiTargetOf(llvm::Triple(M->getTargetTriple()));
  const CxxTarget cxx = cxxTargetFor(M->getTargetTriple().str(), M->getDataLayout().getPointerSizeInBits());
  const DataLayout &dl = M->getDataLayout();
  t = t->canonical();

  auto scalar = [&](llvm::Type *ty, bool isFloat) {
    out.K = CxxArg::Direct;
    out.Ty = ty;
    if (isFloat) ++sseRegs; else ++intRegs;
    return out;
  };
  auto reject = [&](const std::string &what, const std::string &hint) {
    auto d = Diags.error(where, "{} cannot cross into C++: {}", role, what);
    if (!hint.empty())
      d.note("{}", hint);
    d.code(526);
    out.K = CxxArg::Direct;
    out.Ty = lower(t);
    return out;
  };

  switch (t->kind()) {
  case TypeKind::Void:
  case TypeKind::Never:
    out.K = CxxArg::Ignore;
    return out;
  case TypeKind::Bool:
    scalar(B->getInt1Ty(), false);
    out.ZeroExt = cxx.BoolZeroExt;
    return out;
  case TypeKind::Int:
    scalar(lower(t), false);
    if (t->intWidth() < 32 && cxx.SmallIntExt) {
      out.SignExt = t->isSigned();
      out.ZeroExt = !t->isSigned();
    }
    return out;
  case TypeKind::Char:
    return scalar(B->getInt32Ty(), false);
  case TypeKind::Float:
    return scalar(lower(t), true);
  case TypeKind::Pointer:
    if (t->isWeakPointer())
      return reject("a `weak` reference has no C++ meaning", "");
    return scalar(PtrTy, false);
  case TypeKind::CString:
  case TypeKind::CFunction:
    return scalar(PtrTy, false);
  case TypeKind::Enum: {
    NominalDecl *nd = t->nominal();
    if (!nd || !nd->Cxx)
      return reject("a Rune enum is laid out Rune's way",
                    "declare the enum inside the `extern \"C++\"` block, "
                    "where it is C++'s `int`");
    // C++ sees an `int`; Rune's layout of a payload-less enum is one `i32`
    // in a struct, so the bytes agree and only the IR type changes.
    scalar(B->getInt32Ty(), false);
    out.K = CxxArg::Coerce;
    return out;
  }
  case TypeKind::Struct:
  case TypeKind::Tuple:
  case TypeKind::Array:
    break;
  case TypeKind::String:
    return reject("`String` is Rune's counted buffer",
                  "hand C++ a `CString` with `.$cstr()`");
  case TypeKind::Function:
    return reject("a closure carries an environment C++ cannot see",
                  "pass a top-level `fn` where a `@cfunction` is wanted");
  case TypeKind::Class:
    return reject("a Rune class is reference counted", "");
  default:
    return reject("'" + t->toString() + "' has no C++ representation", "");
  }

  // An aggregate. A C++ `class` is one Rune must not copy; a `struct`, a
  // tuple or an array has a layout both sides agree on.
  if (t->is(TypeKind::Struct)) {
    NominalDecl *nd = t->nominal();
    if (nd && nd->Cxx && nd->Cxx->IsClass)
      return reject("a C++ class is never passed by value",
                    "pass `*var " + t->toString() + "` or `&var " +
                        t->toString() + "`, which is what C++ does too");
    if (nd && !nd->Cxx)
      return reject("'" + t->toString() + "' is a Rune struct",
                    "declare it inside the `extern \"C++\"` block so both "
                    "sides agree on its layout");
    if (t->isRefCounted())
      return reject("it holds something reference counted", "");
  }
  llvm::Type *lowered = lower(t);
  const uint64_t size = dl.getTypeAllocSize(lowered);
  const unsigned align = dl.getABITypeAlign(lowered).value();
  if (size == 0) {
    out.K = CxxArg::Ignore;
    return out;
  }
  std::vector<Leaf> leaves;
  flattenLeaves(lowered, 0, dl, leaves);

  auto indirect = [&](CxxArg::Kind k, unsigned a) {
    out.K = k;
    out.Ty = lowered;
    out.Align = a;
    return out;
  };
  auto coerceTo = [&](llvm::Type *ty) {
    out.K = CxxArg::Coerce;
    out.Ty = ty;
    return out;
  };

  switch (tgt.arch) {
  case Arch::X86_64: {
    if (tgt.windows) {
      // Win64: a register-sized aggregate travels as that integer; anything
      // else by pointer, and back through a hidden one.
      if (size == 1 || size == 2 || size == 4 || size == 8) {
        ++intRegs;
        return coerceTo(IntegerType::get(*Ctx, static_cast<unsigned>(size * 8)));
      }
      ++intRegs;
      return indirect(CxxArg::Indirect, std::max(align, 8u));
    }
    // SysV: classify each eightbyte, then spend registers on it.
    if (size > 16)
      return indirect(CxxArg::ByVal, std::max(align, 8u));
    enum Cls : uint8_t { None, Integer, Sse };
    const unsigned n = static_cast<unsigned>((size + 7) / 8);
    Cls cls[2] = {None, None};
    for (const Leaf &l : leaves) {
      uint64_t bytes = dl.getTypeStoreSize(l.Ty);
      if (bytes == 0)
        continue;
      unsigned lo = static_cast<unsigned>(l.Offset / 8);
      unsigned hi = static_cast<unsigned>((l.Offset + bytes - 1) / 8);
      for (unsigned e = lo; e <= hi && e < 2; ++e) {
        if (!isFloatLeaf(l.Ty))
          cls[e] = Integer;
        else if (cls[e] == None)
          cls[e] = Sse;
      }
    }
    unsigned needInt = 0, needSse = 0;
    std::vector<llvm::Type *> parts;
    for (unsigned e = 0; e < n; ++e) {
      if (cls[e] == Integer) {
        ++needInt;
        uint64_t bytes = std::min<uint64_t>(8, size - 8 * e);
        parts.push_back(IntegerType::get(*Ctx, static_cast<unsigned>(bytes * 8)));
      } else if (cls[e] == Sse) {
        ++needSse;
        // One float alone is a `float`, two are a `<2 x float>`, and a
        // double — or a float beside something wider — is a `double`.
        unsigned floats = 0, others = 0;
        for (const Leaf &l : leaves)
          if (l.Offset / 8 == e) {
            if (l.Ty->isFloatTy()) ++floats; else ++others;
          }
        if (others == 0 && floats == 1)
          parts.push_back(B->getFloatTy());
        else if (others == 0 && floats == 2)
          parts.push_back(FixedVectorType::get(B->getFloatTy(), 2));
        else
          parts.push_back(B->getDoubleTy());
      } else {
        // Nothing but padding in this eightbyte: it is not passed at all.
      }
    }
    if (parts.empty()) {
      out.K = CxxArg::Ignore;
      return out;
    }
    if (!isReturn && (intRegs + needInt > 6 || sseRegs + needSse > 8))
      return indirect(CxxArg::ByVal, std::max(align, 8u));
    intRegs += needInt;
    sseRegs += needSse;
    if (parts.size() == 1)
      return coerceTo(parts[0]);
    return coerceTo(StructType::get(*Ctx, parts));
  }
  case Arch::AArch64: {
    if (size > 16) {
      ++intRegs;
      return indirect(CxxArg::Indirect, 8);
    }
    // A homogeneous floating-point aggregate rides in the vector registers,
    // one per member, up to four.
    bool hfa = !leaves.empty() && leaves.size() <= 4;
    for (const Leaf &l : leaves)
      hfa = hfa && isFloatLeaf(l.Ty) && l.Ty == leaves[0].Ty;
    if (hfa && size == leaves.size() *
                           dl.getTypeAllocSize(leaves[0].Ty).getFixedValue()) {
      sseRegs += static_cast<unsigned>(leaves.size());
      return coerceTo(ArrayType::get(leaves[0].Ty, leaves.size()));
    }
    if (size <= 8) {
      ++intRegs;
      // A result comes back in exactly its own bits; an argument is
      // rounded up to the register.
      return coerceTo(isReturn ? static_cast<llvm::Type *>(IntegerType::get(
                                     *Ctx, static_cast<unsigned>(size * 8)))
                               : B->getInt64Ty());
    }
    intRegs += 2;
    return coerceTo(ArrayType::get(B->getInt64Ty(), 2));
  }
  case Arch::X86: {
    if (isReturn) {
      // MinGW returns a register-sized aggregate in EAX / EDX:EAX; the SysV
      // i386 ABI returns every aggregate through memory.
      if (tgt.windows && (size == 1 || size == 2 || size == 4 || size == 8))
        return coerceTo(IntegerType::get(*Ctx, static_cast<unsigned>(size * 8)));
      return indirect(CxxArg::Indirect, align);
    }
    // Every aggregate argument is copied onto the stack, 4-byte aligned.
    return indirect(CxxArg::ByVal, 4);
  }
  case Arch::Wasm: {
    // WebAssembly's C ABI: an aggregate that is one scalar travels as that
    // scalar; any other goes as a pointer to a copy, and comes back through
    // a hidden one.
    if (leaves.size() == 1 &&
        size == dl.getTypeAllocSize(leaves[0].Ty).getFixedValue()) {
      if (isFloatLeaf(leaves[0].Ty)) ++sseRegs; else ++intRegs;
      return coerceTo(leaves[0].Ty);
    }
    if (isReturn)
      return indirect(CxxArg::Indirect, align);
    return indirect(CxxArg::ByVal, align);
  }
  case Arch::Other:
    break;
  }
  return reject("this target's C++ calling convention is not known to the "
                "compiler",
                "pass the value through a pointer instead");
}

const CodeGen::CxxSignature &CodeGen::cxxSignatureFor(FunctionDecl *fn) {
  auto it = CxxSignatures.find(fn);
  if (it != CxxSignatures.end())
    return it->second;
  CxxSignature sig;
  const CxxTarget cxx = cxxTargetFor(M->getTargetTriple().str(), M->getDataLayout().getPointerSizeInBits());
  unsigned intRegs = 0, sseRegs = 0;

  // The result first: a hidden pointer, when one is needed, is the very
  // first argument and takes the first register.
  Type *ret = fn->Ty ? fn->Ty->result() : Types.voidType();
  if (fn->Flavour == FunctionFlavour::Initialiser)
    ret = Types.voidType();
  unsigned retInt = 0, retSse = 0;
  sig.Ret = classifyCxxArgument(ret, retInt, retSse, /*isReturn=*/true,
                                fn->ReturnType ? fn->ReturnType->Range
                                               : fn->Range,
                                "the result");
  if (sig.Ret.K == CxxArg::Indirect || sig.Ret.K == CxxArg::ByVal) {
    sig.Sret = true;
    sig.SretTy = sig.Ret.Ty;
    sig.SretAlign = sig.Ret.Align;
    ++intRegs;
  }

  std::vector<llvm::Type *> params;
  if (sig.Sret)
    params.push_back(PtrTy);
  for (const Param &p : fn->Params)
    if (p.IsSelf) {
      sig.HasThis = true;
      sig.This.K = CxxArg::Direct;
      sig.This.Ty = PtrTy;
      ++intRegs;
      params.push_back(PtrTy);
    }
  for (const Param &p : fn->Params) {
    if (p.IsSelf || !p.Ty)
      continue;
    CxxArg a = classifyCxxArgument(
        p.Ty, intRegs, sseRegs, /*isReturn=*/false,
        p.TypeAnnotation ? p.TypeAnnotation->Range : p.Range,
        "parameter '" + p.Name + "'");
    switch (a.K) {
    case CxxArg::Direct:
      params.push_back(a.Ty);
      break;
    case CxxArg::Coerce:
      // Two eightbytes travel as two arguments; an array as one.
      if (auto *st = dyn_cast<StructType>(a.Ty))
        for (llvm::Type *e : st->elements())
          params.push_back(e);
      else
        params.push_back(a.Ty);
      break;
    case CxxArg::Indirect:
    case CxxArg::ByVal:
      params.push_back(PtrTy);
      break;
    case CxxArg::Ignore:
      break;
    }
    sig.Args.push_back(a);
  }

  llvm::Type *retTy = B->getVoidTy();
  if (!sig.Sret && sig.Ret.K != CxxArg::Ignore)
    retTy = sig.Ret.Ty;
  // wasm-ld does not link a call whose type differs from the definition's —
  // it traps instead — so a `this` nobody reads is still declared.
  if (cxx.CtorsReturnThis && sig.HasThis &&
      (isCxxConstructor(fn) || isCxxDestructor(fn)))
    retTy = PtrTy;
  sig.FT = FunctionType::get(retTy, params, fn->IsVariadic);
  if (sig.HasThis && cxx.ThisCall)
    sig.CC = CallingConv::X86_ThisCall;
  return CxxSignatures[fn] = sig;
}

void CodeGen::applyCxxAttributes(llvm::CallBase *call, llvm::Function *fn,
                                 const CxxSignature &sig) {
  // `Attribute` is also a Rune AST node, hence the qualification.
  using LAttr = llvm::Attribute;
  auto addParam = [&](unsigned index, LAttr attr) {
    if (call)
      call->addParamAttr(index, attr);
    if (fn)
      fn->addParamAttr(index, attr);
  };
  auto addRet = [&](LAttr attr) {
    if (call)
      call->addRetAttr(attr);
    if (fn)
      fn->addRetAttr(attr);
  };
  unsigned index = 0;
  if (sig.Sret) {
    addParam(index, LAttr::getWithStructRetType(*Ctx, sig.SretTy));
    addParam(index, LAttr::getWithAlignment(*Ctx, Align(sig.SretAlign)));
    ++index;
  }
  if (sig.HasThis)
    ++index;
  for (const CxxArg &a : sig.Args) {
    switch (a.K) {
    case CxxArg::Direct:
      if (a.ZeroExt)
        addParam(index, LAttr::get(*Ctx, LAttr::ZExt));
      if (a.SignExt)
        addParam(index, LAttr::get(*Ctx, LAttr::SExt));
      ++index;
      break;
    case CxxArg::Coerce:
      if (auto *st = dyn_cast<StructType>(a.Ty))
        index += st->getNumElements();
      else
        ++index;
      break;
    case CxxArg::ByVal:
      addParam(index, LAttr::getWithByValType(*Ctx, a.Ty));
      addParam(index, LAttr::getWithAlignment(*Ctx, Align(a.Align)));
      ++index;
      break;
    case CxxArg::Indirect:
      ++index;
      break;
    case CxxArg::Ignore:
      break;
    }
  }
  if (!sig.Sret && sig.Ret.K == CxxArg::Direct) {
    if (sig.Ret.ZeroExt)
      addRet(LAttr::get(*Ctx, LAttr::ZExt));
    if (sig.Ret.SignExt)
      addRet(LAttr::get(*Ctx, LAttr::SExt));
  }
}

Value *CodeGen::emitCxxCall(CallExpr *c, FunctionDecl *fn, Value *self) {
  const CxxSignature &sig = cxxSignatureFor(fn);
  Function *callee = declareFunction(fn);
  const DataLayout &dl = M->getDataLayout();

  // The arguments as Rune values, already converted to the parameter types.
  std::vector<Value *> ruleArgs =
      buildArguments(c, fn, fn->Ty->params(), fn->IsVariadic);
  const std::vector<Type *> &paramTypes = fn->Ty->params();

  // A slot big enough for both spellings of a value, so it can be stored as
  // one type and read back as the other.
  auto scratch = [&](llvm::Type *a, llvm::Type *b, const char *name) {
    uint64_t size = std::max(dl.getTypeAllocSize(a).getFixedValue(),
                             b ? dl.getTypeAllocSize(b).getFixedValue() : 0);
    unsigned align = std::max(dl.getABITypeAlign(a).value(),
                              b ? dl.getABITypeAlign(b).value() : 1u);
    Value *slot = createEntryAlloca(ArrayType::get(B->getInt8Ty(), size), name);
    cast<AllocaInst>(slot)->setAlignment(Align(align));
    return slot;
  };

  std::vector<Value *> args;
  Value *sretSlot = nullptr;
  if (sig.Sret) {
    sretSlot = createEntryAlloca(sig.SretTy, "cxx.sret");
    cast<AllocaInst>(sretSlot)->setAlignment(Align(sig.SretAlign));
    args.push_back(sretSlot);
  }
  if (sig.HasThis)
    args.push_back(self ? self : ConstantPointerNull::get(PtrTy));

  for (size_t i = 0; i < sig.Args.size() && i < ruleArgs.size(); ++i) {
    const CxxArg &a = sig.Args[i];
    Value *v = ruleArgs[i];
    llvm::Type *runeTy = i < paramTypes.size() ? lower(paramTypes[i]) : v->getType();
    switch (a.K) {
    case CxxArg::Direct:
      args.push_back(v);
      break;
    case CxxArg::Coerce: {
      Value *slot = scratch(runeTy, a.Ty, "cxx.coerce");
      B->CreateStore(v, slot);
      Value *loaded = B->CreateLoad(a.Ty, slot);
      if (auto *st = dyn_cast<StructType>(a.Ty))
        for (unsigned e = 0; e < st->getNumElements(); ++e)
          args.push_back(B->CreateExtractValue(loaded, e));
      else
        args.push_back(loaded);
      break;
    }
    case CxxArg::Indirect:
    case CxxArg::ByVal: {
      Value *slot = createEntryAlloca(runeTy, "cxx.byref");
      cast<AllocaInst>(slot)->setAlignment(Align(std::max(
          a.Align, static_cast<unsigned>(dl.getABITypeAlign(runeTy).value()))));
      B->CreateStore(v, slot);
      args.push_back(slot);
      break;
    }
    case CxxArg::Ignore:
      break;
    }
  }
  // A variadic tail goes as `buildArguments` promoted it.
  for (size_t i = sig.Args.size(); i < ruleArgs.size(); ++i)
    args.push_back(ruleArgs[i]);

  CallInst *call = B->CreateCall(sig.FT, callee, args);
  call->setCallingConv(sig.CC);
  applyCxxAttributes(call, nullptr, sig);

  Type *retTy = c->Ty;
  if (fn->Flavour == FunctionFlavour::Initialiser || !retTy || retTy->isVoid() ||
      retTy->isNever())
    return nullptr;
  llvm::Type *runeRet = lower(retTy);
  Value *result = nullptr;
  if (sig.Sret) {
    result = B->CreateLoad(runeRet, sretSlot, "cxx.result");
  } else if (sig.Ret.K == CxxArg::Coerce) {
    Value *slot = scratch(runeRet, sig.Ret.Ty, "cxx.ret");
    B->CreateStore(call, slot);
    result = B->CreateLoad(runeRet, slot, "cxx.result");
  } else if (sig.Ret.K == CxxArg::Ignore) {
    return nullptr;
  } else {
    result = call;
  }
  return track(result, retTy);
}

Value *CodeGen::emitCxxAlloc(CallExpr *c, Type *arg) {
  const CxxTarget cxx = cxxTargetFor(M->getTargetTriple().str(), M->getDataLayout().getPointerSizeInBits());
  uint64_t size = 0;
  Type *t = arg ? arg->canonical() : nullptr;
  NominalDecl *nd = t && t->isNominal() ? t->nominal() : nullptr;
  if (nd && nd->Cxx && nd->Cxx->IsClass) {
    size = nd->Cxx->Size;
    if (!size) {
      Diags.error(c->Range, "'{}' has no `@size`, so nothing can allocate one",
                  t->toString())
          .note("write `@size(N)` above its declaration, with `N` from "
                "`sizeof` on the C++ side — or take instances from a "
                "function that creates them")
          .code(527);
      return ConstantPointerNull::get(PtrTy);
    }
  } else if (t && !t->isVoid()) {
    size = M->getDataLayout().getTypeAllocSize(lower(t)).getFixedValue();
  }
  llvm::Type *sizeTy = IntegerType::get(*Ctx, cxx.PointerBits);
  FunctionCallee opNew =
      M->getOrInsertFunction(cxx.OperatorNew, PtrTy, sizeTy);
  Value *p = B->CreateCall(opNew, {ConstantInt::get(sizeTy, size)}, "cxx.new");
  return p;
}

void CodeGen::emitCxxFree(CallExpr *c) {
  const CxxTarget cxx = cxxTargetFor(M->getTargetTriple().str(), M->getDataLayout().getPointerSizeInBits());
  if (c->Args.empty())
    return;
  Value *p = emitRValue(c->Args[0].Value.get());
  if (!p)
    return;
  FunctionCallee opDelete =
      M->getOrInsertFunction(cxx.OperatorDelete, B->getVoidTy(), PtrTy);
  B->CreateCall(opDelete, {p});
}

} // namespace rune
