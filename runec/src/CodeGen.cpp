//===- CodeGen.cpp - Types, ARC, declarations and statements ---*- C++ -*-===//

#include "rune/CodeGen.h"
#include "rune/ASTWalk.h"
#include "rune/CxxInterop.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include <filesystem>
#include <functional>

#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Host.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/IPO/GlobalDCE.h>
#include <llvm/Transforms/Utils/Cloning.h>

#include <cctype>
#include <cmath>
#include <set>

namespace rune {

using namespace llvm;

//===----------------------------------------------------------------------===//
// Construction
//===----------------------------------------------------------------------===//

CodeGen::CodeGen(const SourceManager &sm, DiagnosticEngine &diags,
                 TypeContext &types, const SemaResult &sema,
                 const CompilerOptions &opts)
    : SM(sm), Diags(diags), Types(types), Sema(sema), Opts(opts) {
  Ctx = std::make_unique<LLVMContext>();
  M = std::make_unique<llvm::Module>(opts.ModuleName, *Ctx);
  B = std::make_unique<IRBuilder<>>(*Ctx);
  // The target's layout has to be in place before any code is generated:
  // `size_of` and `align_of` are answered from it, and LLVM's default layout
  // is not the machine's (it aligns i64 to 4).
  applyTargetLayout();
  initRuntimeTypes();
}

CodeGen::~CodeGen() = default;

//===----------------------------------------------------------------------===//
// Debug information
//===----------------------------------------------------------------------===//

void CodeGen::initDebugInfo() {
  if (!Opts.DebugInfo)
    return;
  M->addModuleFlag(llvm::Module::Warning, "Debug Info Version",
                   llvm::DEBUG_METADATA_VERSION);
  M->addModuleFlag(llvm::Module::Warning, "Dwarf Version", 4);
  DI = std::make_unique<llvm::DIBuilder>(*M);
  // The unit's file is the first input; every other file appears through the
  // subprograms that live in it.
  std::string dir = ".", name = Opts.ModuleName + ".rune";
  if (SM.fileCount() > 0) {
    std::filesystem::path p(SM.file(0).Path);
    name = p.filename().string();
    dir = p.has_parent_path() ? p.parent_path().string() : ".";
  }
  llvm::DIFile *file = DI->createFile(name, dir);
  DIFiles[0] = file;
  DICU = DI->createCompileUnit(llvm::dwarf::DW_LANG_C99, file, "runec",
                               Opts.OptLevel > 0, "", 0);
}

void CodeGen::finishDebugInfo() {
  if (DI)
    DI->finalize();
}

llvm::DIFile *CodeGen::debugFileFor(SourceRange r) {
  if (!DI)
    return nullptr;
  const SourceFile *sf = r.isValid() ? SM.fileFor(r.begin()) : nullptr;
  if (!sf)
    return DIFiles.count(0) ? DIFiles[0] : nullptr;
  auto it = DIFiles.find(sf->ID);
  if (it != DIFiles.end())
    return it->second;
  std::filesystem::path p(sf->Path);
  llvm::DIFile *f = DI->createFile(
      p.filename().string(),
      p.has_parent_path() ? p.parent_path().string() : ".");
  DIFiles[sf->ID] = f;
  return f;
}

llvm::DIType *CodeGen::debugTypeFor(Type *t) {
  if (!DI || !t)
    return nullptr;
  auto it = DITypes.find(t);
  if (it != DITypes.end())
    return it->second;
  // Insert a placeholder first: a struct that contains a pointer to itself
  // would otherwise recurse forever.
  DITypes[t] = nullptr;
  llvm::DIType *result = nullptr;

  auto basic = [&](const char *name, unsigned bits, unsigned enc) {
    return DI->createBasicType(name, bits, enc);
  };
  switch (t->kind()) {
  case TypeKind::Void:
    result = nullptr;
    break;
  case TypeKind::Bool:
    result = basic("bool", 8, llvm::dwarf::DW_ATE_boolean);
    break;
  case TypeKind::Char:
    result = basic("Character", 32, llvm::dwarf::DW_ATE_UTF);
    break;
  case TypeKind::Int:
    result = basic(t->toString().c_str(), t->intWidth(),
                   t->isSigned() ? llvm::dwarf::DW_ATE_signed
                                 : llvm::dwarf::DW_ATE_unsigned);
    break;
  case TypeKind::Float:
    result = basic(t->toString().c_str(), t->floatWidth(),
                   llvm::dwarf::DW_ATE_float);
    break;
  case TypeKind::String:
  case TypeKind::CString:
  case TypeKind::Class:
  case TypeKind::Pointer: {
    Type *pointee = t->is(TypeKind::Pointer) ? t->pointee() : nullptr;
    llvm::DIType *inner = pointee ? debugTypeFor(pointee) : nullptr;
    result = DI->createPointerType(inner, 64, 64, std::nullopt,
                                   t->toString());
    break;
  }
  case TypeKind::Array: {
    llvm::DIType *elem = debugTypeFor(t->element());
    if (!elem) break;
    auto count = static_cast<int64_t>(t->arraySize());
    llvm::Metadata *sub[] = {DI->getOrCreateSubrange(0, count)};
    result = DI->createArrayType(
        M->getDataLayout().getTypeAllocSizeInBits(lower(t)), 0, elem,
        DI->getOrCreateArray(sub));
    break;
  }
  case TypeKind::Struct:
  case TypeKind::Enum:
  case TypeKind::Tuple:
  case TypeKind::Slice:
  case TypeKind::Function:
  case TypeKind::Any:
  case TypeKind::DynMark: {
    // A composite whose interior is not worth describing field by field yet;
    // its size and name still let a debugger show the storage.
    llvm::Type *lowered = lower(t);
    uint64_t bits = lowered->isSized()
                        ? M->getDataLayout().getTypeAllocSizeInBits(lowered)
                        : 64;
    result = DI->createStructType(DICU, t->toString(), DIFiles.count(0)
                                                           ? DIFiles[0]
                                                           : nullptr,
                                  0, bits, 0, llvm::DINode::FlagZero, nullptr,
                                  DI->getOrCreateArray({}));
    break;
  }
  default:
    break;
  }
  DITypes[t] = result;
  return result;
}

llvm::DISubprogram *CodeGen::debugSubprogramFor(FunctionDecl *fn,
                                                llvm::Function *f) {
  if (!DI || !fn || !f)
    return nullptr;
  llvm::DIFile *file = debugFileFor(fn->Range);
  if (!file)
    return nullptr;
  unsigned line =
      fn->Range.isValid() ? SM.decode(fn->Range.begin()).Line : 0;

  std::vector<llvm::Metadata *> sig;
  sig.push_back(fn->Ty ? debugTypeFor(fn->Ty->result()) : nullptr);
  for (const Param &p : fn->Params)
    sig.push_back(debugTypeFor(p.Ty));
  llvm::DISubroutineType *type =
      DI->createSubroutineType(DI->getOrCreateTypeArray(sig));

  llvm::DISubprogram::DISPFlags spFlags = llvm::DISubprogram::SPFlagDefinition;
  if (fn->Name == "main")
    spFlags |= llvm::DISubprogram::SPFlagMainSubprogram;
  llvm::DISubprogram *sp =
      DI->createFunction(file, fn->Name, f->getName(), file, line, type, line,
                         llvm::DINode::FlagPrototyped, spFlags);
  f->setSubprogram(sp);
  return sp;
}

void CodeGen::setDebugLocation(SourceRange r) {
  if (!DI || DIScopes.empty() || !DIScopes.back())
    return;
  unsigned line = 0, col = 0;
  if (r.isValid()) {
    PresumedLoc pl = SM.decode(r.begin());
    line = pl.Line;
    col = pl.Column;
  }
  B->SetCurrentDebugLocation(
      llvm::DILocation::get(*Ctx, line, col, DIScopes.back()));
}

void CodeGen::declareDebugVariable(VarDecl *v, llvm::Value *slot,
                                   unsigned argIndex) {
  if (!DI || DIScopes.empty() || !DIScopes.back() || !v || !slot)
    return;
  llvm::DIType *ty = debugTypeFor(v->Ty);
  if (!ty)
    return;
  llvm::DIFile *file = debugFileFor(v->Range);
  unsigned line = v->Range.isValid() ? SM.decode(v->Range.begin()).Line : 0;
  llvm::DILocalVariable *var =
      argIndex ? DI->createParameterVariable(DIScopes.back(), v->Name,
                                             argIndex, file, line, ty, true)
               : DI->createAutoVariable(DIScopes.back(), v->Name, file, line,
                                        ty, true);
  DI->insertDeclare(slot, var, DI->createExpression(),
                    llvm::DILocation::get(*Ctx, line, 0, DIScopes.back()),
                    B->GetInsertBlock());
}

void CodeGen::applyTargetLayout() {
  static bool initialised = false;
  if (!initialised) {
    initialiseTargets();
    initialised = true;
  }
  std::string tripleStr = Opts.TargetTriple.empty()
                              ? llvm::sys::getDefaultTargetTriple()
                              : Opts.TargetTriple;
  // `Triple` no longer normalises what it is given, so an alias like
  // `x86_64-w64-mingw32` would parse as an unknown OS and quietly fall back
  // to ELF. Normalising first turns it into `x86_64-w64-windows-gnu`.
  llvm::Triple triple(llvm::Triple::normalize(tripleStr));
  std::string err;
  if (std::unique_ptr<llvm::TargetMachine> tm = createTargetMachine(triple, err)) {
    M->setDataLayout(tm->createDataLayout());
    M->setTargetTriple(triple);
  }
}

void CodeGen::initRuntimeTypes() {
  PtrTy = PointerType::get(*Ctx, 0);
  // struct RuneObject { int64_t refcount; const RuneTypeInfo *type; }
  ObjectHeaderTy = StructType::create(*Ctx, {B->getInt64Ty(), PtrTy},
                                      "rune.object");
  // struct RuneTypeInfo { name, size, deinit, super, vtable, vtableCount,
  //                       clone }
  TypeInfoTy = StructType::create(
      *Ctx,
      {PtrTy, B->getInt64Ty(), PtrTy, PtrTy, PtrTy, B->getInt32Ty(), PtrTy},
      "rune.typeinfo");
}

FunctionCallee CodeGen::runtimeFn(const char *name, llvm::Type *ret,
                                  std::vector<llvm::Type *> params,
                                  bool variadic) {
  return M->getOrInsertFunction(name, FunctionType::get(ret, params, variadic));
}

//===----------------------------------------------------------------------===//
// Type lowering
//===----------------------------------------------------------------------===//

std::vector<FieldDecl *> CodeGen::allFieldsOf(NominalDecl *nd) {
  std::vector<FieldDecl *> out;
  if (auto *c = dyn_cast<ClassDecl>(static_cast<Decl *>(nd))) {
    // Base class fields come first so Sema's flattened indices line up.
    std::vector<ClassDecl *> chain;
    for (ClassDecl *k = c; k; k = k->Super)
      chain.push_back(k);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
      for (const auto &f : (*it)->Fields)
        out.push_back(f.get());
    return out;
  }
  for (const auto &f : nd->Fields)
    out.push_back(f.get());
  return out;
}

namespace {
/// Tracks which nominal types are mid-layout so a self-referential value type
/// can be reported instead of hanging.
std::set<NominalDecl *> LayoutInProgress;
} // namespace

llvm::Type *CodeGen::variantPayloadType(EnumDecl *e, unsigned variantIndex) {
  const auto &v = e->Variants[variantIndex];
  std::vector<llvm::Type *> fields;
  if (v->Shape == VariantShape::Tuple) {
    for (const auto &tt : v->TupleTypes)
      fields.push_back(lower(tt->Resolved));
  } else if (v->Shape == VariantShape::Struct) {
    for (const auto &f : v->Fields)
      fields.push_back(lower(f->Ty));
  }
  return StructType::get(*Ctx, fields);
}

/// `double` when every scalar in every variant's payload is a `double` and it
/// fills its alignment exactly; `float` likewise; otherwise null.
llvm::Type *CodeGen::enumPayloadFloat(EnumDecl *e, uint64_t align) {
  llvm::Type *only = nullptr;
  std::function<bool(llvm::Type *)> walk = [&](llvm::Type *t) {
    if (auto *st = dyn_cast<StructType>(t)) {
      for (llvm::Type *el : st->elements())
        if (!walk(el))
          return false;
      return true;
    }
    if (auto *at = dyn_cast<ArrayType>(t))
      return walk(at->getElementType());
    if (!t->isFloatTy() && !t->isDoubleTy())
      return false;
    if (only && only != t)
      return false;
    only = t;
    return true;
  };
  for (unsigned i = 0; i < e->Variants.size(); ++i) {
    llvm::Type *pt = variantPayloadType(e, i);
    if (!pt->isSized() || !walk(pt))
      return nullptr;
  }
  if (!only || M->getDataLayout().getABITypeAlign(only).value() != align)
    return nullptr;
  return only;
}

/// The strictest alignment any variant's payload asks for: 1, 2, 4, 8 or 16.
uint64_t CodeGen::enumPayloadAlign(EnumDecl *e) {
  const DataLayout &DL = M->getDataLayout();
  uint64_t align = 1;
  for (unsigned i = 0; i < e->Variants.size(); ++i) {
    llvm::Type *pt = variantPayloadType(e, i);
    if (pt->isSized())
      align = std::max<uint64_t>(align, DL.getABITypeAlign(pt).value());
  }
  return std::min<uint64_t>(align, 16);
}

uint64_t CodeGen::enumPayloadSize(EnumDecl *e) {
  const DataLayout &DL = M->getDataLayout();
  uint64_t maxSize = 0;
  for (unsigned i = 0; i < e->Variants.size(); ++i) {
    llvm::Type *pt = variantPayloadType(e, i);
    if (!pt->isSized()) {
      // A payload whose layout is still being computed means the variant
      // contains the enum itself, directly or through another value type.
      const auto &variant = e->Variants[i];
      auto d = Diags.error(
          variant->NameRange.isValid() ? variant->NameRange : variant->Range,
          "variant '{}' makes '{}' contain itself, so its size is unbounded",
          variant->Name, e->Name);
      d.note("an enum is a value, so a variant cannot hold the enum directly; "
             "put the recursive part behind a class")
          .code(502);
      if (e->NameRange.isValid())
        d.related(e->NameRange, fmt("'{}' declared here", e->Name),
                  "every variant's payload has to have a known size");
      continue;
    }
    maxSize = std::max(maxSize, DL.getTypeAllocSize(pt).getFixedValue());
  }
  return maxSize;
}

llvm::StructType *CodeGen::layoutOf(NominalDecl *nd, Type *t) {
  auto it = NominalLayouts.find(nd);
  if (it != NominalLayouts.end())
    return it->second;

  std::string name = static_cast<Decl *>(nd)->Name;
  if (!t->typeArguments().empty())
    name += "." + std::to_string(NominalLayouts.size());
  auto *st = StructType::create(*Ctx, "rune." + name);
  NominalLayouts[nd] = st;
  // Seed the lowering cache so a type that refers to itself terminates. A
  // class *value* is a pointer to its instance, not the instance layout, so
  // only value types map onto the struct itself.
  LoweredTypes[t] = isa<ClassDecl>(static_cast<Decl *>(nd))
                        ? static_cast<llvm::Type *>(PtrTy)
                        : static_cast<llvm::Type *>(st);

  if (LayoutInProgress.count(nd)) {
    // Reached while already laying this type out: it contains itself.
    Diags.error(static_cast<Decl *>(nd)->NameRange,
                "'{}' contains itself, so its size is unbounded",
                static_cast<Decl *>(nd)->Name)
        .note("store the nested value behind a class or a pointer to break "
              "the cycle")
        .code(500);
    st->setBody({B->getInt8Ty()});
    return st;
  }
  LayoutInProgress.insert(nd);

  std::vector<llvm::Type *> body;
  if (auto *e = dyn_cast<EnumDecl>(static_cast<Decl *>(nd))) {
    body.push_back(B->getInt32Ty()); // discriminant
    // The payload is a run of the widest-aligned integer any variant needs,
    // so it starts where C's tagged union would put it: an `i64` after the
    // tag at 8, not 4. Bytes would leave it at 4, which is a misaligned load
    // on a processor that minds — and a layout C does not share.
    uint64_t payload = enumPayloadSize(e);
    if (payload) {
      uint64_t align = enumPayloadAlign(e);
      // All `double`s (or all `float`s) in every variant: a run of that, so
      // C's classification — floating-point registers — is what the IR says.
      llvm::Type *unit = enumPayloadFloat(e, align);
      if (!unit)
        unit = IntegerType::get(*Ctx, static_cast<unsigned>(align * 8));
      uint64_t unitSize = M->getDataLayout().getTypeAllocSize(unit);
      body.push_back(ArrayType::get(unit, (payload + unitSize - 1) / unitSize));
    }
  } else {
    if (isa<ClassDecl>(static_cast<Decl *>(nd)))
      body.push_back(ObjectHeaderTy);
    for (FieldDecl *f : allFieldsOf(nd)) {
      // A weak field holds a bare pointer that the runtime nulls when the
      // target dies; the Option it reads as is built on access.
      llvm::Type *ft = f->IsWeak ? static_cast<llvm::Type *>(PtrTy)
                                 : lower(f->Ty);
      auto *asStruct = llvm::dyn_cast<StructType>(ft);
      if (asStruct && asStruct->isOpaque()) {
        // The field's own layout is still in progress, so it contains this
        // type by value.
        auto d = Diags.error(
            f->NameRange.isValid() ? f->NameRange : f->Range,
            "field '{}' makes '{}' contain itself, so its size is unbounded",
            f->Name, static_cast<Decl *>(nd)->Name);
        d.note("store it behind a class or a pointer to break the cycle")
            .code(502);
        ft = B->getInt8Ty();
      }
      body.push_back(ft);
    }
  }
  st->setBody(body);
  LayoutInProgress.erase(nd);
  return st;
}

llvm::Type *CodeGen::lower(Type *t) {
  if (!t)
    return B->getVoidTy();
  // A `some Mark` is laid out exactly as the type behind it.
  t = t->canonical();
  auto it = LoweredTypes.find(t);
  if (it != LoweredTypes.end())
    return it->second;

  llvm::Type *r = nullptr;
  switch (t->kind()) {
  case TypeKind::Void:
  case TypeKind::Never:
  case TypeKind::Error:
  case TypeKind::Opaque: // a `some` nothing ever fixed; Sema has reported it
    r = StructType::get(*Ctx, {}); // zero-sized placeholder
    break;
  case TypeKind::Bool:
    r = B->getInt1Ty();
    break;
  case TypeKind::Int:
    r = IntegerType::get(*Ctx, t->intWidth());
    break;
  case TypeKind::Float:
    r = t->floatWidth() == 32 ? B->getFloatTy() : B->getDoubleTy();
    break;
  case TypeKind::Char:
    r = B->getInt32Ty();
    break;
  case TypeKind::CString:
  case TypeKind::String:
  case TypeKind::Pointer:
  case TypeKind::Class:
  case TypeKind::Mark:
  // An `Any` is one pointer: the boxed value, whose object header already
  // names the type. Nothing has to travel beside it.
  case TypeKind::Any:
    r = PtrTy;
    break;
  case TypeKind::Array:
    r = ArrayType::get(lower(t->element()),
                       t->arraySize() ? t->arraySize() : 0);
    break;
  case TypeKind::Slice:
    r = StructType::get(*Ctx, {PtrTy, B->getInt64Ty()});
    break;
  case TypeKind::Tuple: {
    std::vector<llvm::Type *> elems;
    for (Type *e : t->tupleElements())
      elems.push_back(lower(e));
    r = StructType::get(*Ctx, elems);
    break;
  }
  case TypeKind::CFunction:
    // A bare pointer, exactly like C: nothing captured, nothing counted.
    r = PtrTy;
    break;
  case TypeKind::Function:
  case TypeKind::DynMark:
    // { code pointer, environment } and { instance, vtable } share a shape.
    r = StructType::get(*Ctx, {PtrTy, PtrTy});
    break;
  case TypeKind::Struct:
  case TypeKind::Enum:
    // An opaque C++ class has no fields Rune knows; it is `@size` bytes.
    if (t->nominal() && t->nominal()->Cxx && t->nominal()->Cxx->IsClass)
      r = lowerCxxClass(t->nominal());
    else
      r = layoutOf(t->nominal(), t);
    break;
  case TypeKind::Generic:
    // Only reachable if an uninstantiated template leaked through.
    r = B->getInt8Ty();
    break;
  }
  LoweredTypes[t] = r;
  return r;
}

llvm::Type *CodeGen::lowerReturn(Type *t) {
  if (!t || t->isVoid() || t->isNever() || t->isError())
    return B->getVoidTy();
  return lower(t);
}

llvm::FunctionType *CodeGen::functionTypeFor(FunctionDecl *fn) {
  // A C++ function's type is whatever the target's C++ ABI makes of its
  // signature: see CodeGenCxx.cpp.
  if (isCxxExtern(fn))
    return cxxSignatureFor(fn).FT;
  if (const CxxSignature *sig = cSignatureFor(fn))
    return sig->FT;
  std::vector<llvm::Type *> params;
  // Closures take their environment first.
  if (fn->Flavour == FunctionFlavour::Closure)
    params.push_back(PtrTy);
  for (const Param &p : fn->Params) {
    if (!p.Ty)
      continue;
    // `self` is just the first parameter; Sema already gave it the right
    // shape (the class pointer, `&Struct`, or the struct by value).
    params.push_back(lower(p.Ty));
  }
  Type *ret = fn->Ty ? fn->Ty->result() : Types.voidType();
  // An initialiser writes into the freshly allocated instance and returns it.
  if (fn->Flavour == FunctionFlavour::Initialiser)
    return FunctionType::get(B->getVoidTy(), params, fn->IsVariadic);
  return FunctionType::get(lowerReturn(ret), params, fn->IsVariadic);
}

//===----------------------------------------------------------------------===//
// Reference counting
//===----------------------------------------------------------------------===//

/// Whether a value of `t` can be reached from more than one thread, and so
/// needs its reference count changed indivisibly.
///
/// The compiler answers this from the static type, which is the whole point:
/// a class that cannot be shared pays an ordinary add, and only what really
/// is shared pays for an atomic. Measured on a reference-counting loop, that
/// is the difference between one pass and two.
///
/// Three things qualify. `String` is `Send`, so a thread can be handed one.
/// Anything marked `@sync` says outright that it is reached from several
/// threads — `Arc` and `Mutex` are the two. And nothing else is either,
/// because nothing else is `Send`.
bool CodeGen::isSharedRefType(Type *t) {
  if (!t)
    return false;
  if (t->is(TypeKind::String))
    return true;
  if (NominalDecl *nd = t->nominal())
    return nd->hasAttr("sync");
  return false;
}

const char *CodeGen::retainFnFor(Type *t) {
  return isSharedRefType(t) ? "rune_retain_shared" : "rune_retain";
}

const char *CodeGen::releaseFnFor(Type *t) {
  return isSharedRefType(t) ? "rune_release_shared" : "rune_release";
}

void CodeGen::emitRetain(Value *v, Type *t) {
  if (!v || !t || !t->isRefCounted())
    return;
  // Under Zombie nothing is ever counted: what would have been a second
  // reference is a move, and the consumer says so (`takeOwnership`).
  if (zombie())
    return;
  switch (t->kind()) {
  case TypeKind::Class:
  case TypeKind::String:
  case TypeKind::Any:
    B->CreateCall(runtimeFn(retainFnFor(t), PtrTy, {PtrTy}), {v});
    break;
  case TypeKind::Function: {
    Value *env = B->CreateExtractValue(v, 1, "env");
    // A closure is not `Send`, but `task::offload` hands one to a worker
    // thread once its captures have been checked — and the thread that made
    // it may still be letting go of its own reference while the worker takes
    // the closure up. So the environment is counted atomically, like a
    // `String`: the one kind of object whose last two owners can be on
    // different threads without either being told.
    B->CreateCall(runtimeFn("rune_retain_shared", PtrTy, {PtrTy}), {env});
    break;
  }
  case TypeKind::DynMark: {
    // `{ object, vtable }`: the object is the reference-counted half.
    Value *object = B->CreateExtractValue(v, 0, "dyn.object");
    B->CreateCall(runtimeFn(retainFnFor(t), PtrTy, {PtrTy}), {object});
    break;
  }
  case TypeKind::Tuple: {
    const auto &elems = t->tupleElements();
    for (unsigned i = 0; i < elems.size(); ++i) {
      if (elems[i]->isRefCounted())
        emitRetain(B->CreateExtractValue(v, i), elems[i]);
      else if (elems[i]->isSharedHeapBorrow())
        emitRetain(heldObject(B->CreateExtractValue(v, i), elems[i]), elems[i]->pointee());
    }
    break;
  }
  case TypeKind::Struct: {
    auto fields = allFieldsOf(t->nominal());
    for (unsigned i = 0; i < fields.size(); ++i) {
      if (!fields[i]->Ty) continue;
      if (fields[i]->Ty->isRefCounted())
        emitRetain(B->CreateExtractValue(v, i), fields[i]->Ty);
      else if (fields[i]->Ty->isSharedHeapBorrow())
        // A stored borrow is a strong reference under counting.
        emitRetain(heldObject(B->CreateExtractValue(v, i), fields[i]->Ty),
                   fields[i]->Ty->pointee());
    }
    break;
  }
  case TypeKind::Array: {
    for (uint64_t i = 0; i < t->arraySize(); ++i)
      emitRetain(B->CreateExtractValue(v, static_cast<unsigned>(i)),
                 t->element());
    break;
  }
  case TypeKind::Enum:
    emitEnumRefCount(v, t, /*retain=*/true);
    break;
  default:
    break;
  }
}

void CodeGen::emitRelease(Value *v, Type *t) {
  if (!v || !t || !t->isRefCounted())
    return;
  // Under Zombie a release is a drop: the one owner is letting go, so the
  // object is torn down without a count being consulted. At `--safety full`
  // the runtime still checks the count is 1, which catches anything that
  // retained behind the checker's back.
  auto dropObject = [&](Value *obj) {
    if (zombie()) {
      B->CreateCall(runtimeFn("rune_drop", B->getVoidTy(),
                              {PtrTy, B->getInt1Ty()}),
                    {obj, B->getInt1(Opts.Safety == SafetyLevel::Full)});
    } else {
      B->CreateCall(runtimeFn(releaseFnFor(t), B->getVoidTy(), {PtrTy}),
                    {obj});
    }
  };
  switch (t->kind()) {
  case TypeKind::Class:
  case TypeKind::String:
  case TypeKind::Any:
    dropObject(v);
    break;
  case TypeKind::Function: {
    Value *env = B->CreateExtractValue(v, 1, "env");
    if (zombie())
      B->CreateCall(runtimeFn("rune_drop", B->getVoidTy(),
                              {PtrTy, B->getInt1Ty()}),
                    {env, B->getInt1(Opts.Safety == SafetyLevel::Full)});
    else
      B->CreateCall(runtimeFn("rune_release_shared", B->getVoidTy(), {PtrTy}),
                    {env});
    break;
  }
  case TypeKind::DynMark: {
    Value *object = B->CreateExtractValue(v, 0, "dyn.object");
    dropObject(object);
    break;
  }
  case TypeKind::Tuple: {
    const auto &elems = t->tupleElements();
    for (unsigned i = 0; i < elems.size(); ++i) {
      if (elems[i]->isRefCounted())
        emitRelease(B->CreateExtractValue(v, i), elems[i]);
      else if (!zombie() && elems[i]->isSharedHeapBorrow())
        emitRelease(heldObject(B->CreateExtractValue(v, i), elems[i]), elems[i]->pointee());
    }
    break;
  }
  case TypeKind::Struct: {
    auto fields = allFieldsOf(t->nominal());
    for (unsigned i = 0; i < fields.size(); ++i) {
      if (!fields[i]->Ty) continue;
      if (fields[i]->Ty->isRefCounted())
        emitRelease(B->CreateExtractValue(v, i), fields[i]->Ty);
      else if (!zombie() && fields[i]->Ty->isSharedHeapBorrow())
        // A stored borrow held a strong reference under counting; let it go.
        // Under single ownership it is a plain borrow and is never dropped.
        emitRelease(heldObject(B->CreateExtractValue(v, i), fields[i]->Ty),
                    fields[i]->Ty->pointee());
    }
    break;
  }
  case TypeKind::Array: {
    for (uint64_t i = 0; i < t->arraySize(); ++i)
      emitRelease(B->CreateExtractValue(v, static_cast<unsigned>(i)),
                  t->element());
    break;
  }
  case TypeKind::Enum:
    emitEnumRefCount(v, t, /*retain=*/false);
    break;
  default:
    break;
  }
}

/// A structural hash of `v`, consistent with `emitEquals`.
///
/// The compiler knows the layout, so this needs nothing from the type: no
/// `Hashable` mark to bind and no `Display` to stand in for one. A String
/// hashes its contents; a class hashes its address, because two distinct
/// objects are two distinct keys even when their fields agree.
//===----------------------------------------------------------------------===//
// Reflection
//===----------------------------------------------------------------------===//

/// The `reflect::Kind` case for `t`, as the enum's own tag value.
Value *CodeGen::emitKindOf(Type *t, Type *kindType) {
  // The order here is the order the cases are written in `std::reflect`; the
  // enum has no explicit values, so a case's index is its tag.
  unsigned index = 17; // Unknown
  switch (t->kind()) {
  case TypeKind::Void: index = 0; break;
  case TypeKind::Bool: index = 1; break;
  case TypeKind::Int: index = 2; break;
  case TypeKind::Float: index = 3; break;
  case TypeKind::Char: index = 4; break;
  case TypeKind::String: index = 5; break;
  case TypeKind::CString: index = 6; break;
  case TypeKind::Pointer: index = 7; break;
  case TypeKind::Array: index = 8; break;
  case TypeKind::Slice: index = 9; break;
  case TypeKind::Tuple: index = 10; break;
  case TypeKind::Function:
  case TypeKind::CFunction: index = 11; break;
  case TypeKind::Struct: index = 12; break;
  case TypeKind::Enum: index = 13; break;
  case TypeKind::Class: index = 14; break;
  case TypeKind::Mark:
  case TypeKind::DynMark: index = 15; break;
  case TypeKind::Any: index = 16; break;
  default: index = 17; break;
  }
  // A payload-free enum is its tag, so the constant is the whole value.
  llvm::Type *lowered = lower(kindType);
  if (auto *st = dyn_cast<StructType>(lowered)) {
    Value *slot = createEntryAlloca(st, "kind");
    B->CreateStore(Constant::getNullValue(st), slot);
    B->CreateStore(B->getInt32(index), B->CreateStructGEP(st, slot, 0));
    return B->CreateLoad(st, slot);
  }
  return ConstantInt::get(lowered, index);
}

/// How many parts `t` has, in the sense `reflect::fieldCount` means.
uint64_t CodeGen::reflectFieldCount(Type *t) {
  switch (t->kind()) {
  case TypeKind::Struct:
    return allFieldsOf(t->nominal()).size();
  case TypeKind::Class:
    // Already flattened, base class fields first.
    return allFieldsOf(t->nominal()).size();
  case TypeKind::Tuple:
    return t->tupleElements().size();
  case TypeKind::Array:
    return t->arraySize();
  case TypeKind::Enum:
    if (auto *e = dyn_cast<EnumDecl>(static_cast<Decl *>(t->nominal())))
      return e->Variants.size();
    return 0;
  default:
    return 0;
  }
}

/// The name of part `index` of `t`, or of its type when `wantType`.
std::string CodeGen::reflectFieldText(Type *t, int64_t index, bool wantType) {
  if (index < 0)
    return "";
  const uint64_t at = static_cast<uint64_t>(index);
  switch (t->kind()) {
  case TypeKind::Struct:
  case TypeKind::Class: {
    auto fields = allFieldsOf(t->nominal());
    if (at >= fields.size())
      return "";
    FieldDecl *f = fields[at];
    if (!wantType)
      return f->Name;
    return f->Ty ? f->Ty->toString() : "";
  }
  case TypeKind::Tuple: {
    const auto &elems = t->tupleElements();
    if (at >= elems.size())
      return "";
    return wantType ? elems[at]->toString() : std::to_string(at);
  }
  case TypeKind::Array:
    if (at >= t->arraySize())
      return "";
    return wantType ? t->element()->toString() : std::to_string(at);
  case TypeKind::Enum: {
    auto *e = dyn_cast<EnumDecl>(static_cast<Decl *>(t->nominal()));
    if (!e || at >= e->Variants.size())
      return "";
    const auto &v = e->Variants[at];
    if (!wantType)
      return v->Name;
    // A variant's "type" is the shape of its payload.
    std::string out;
    for (const auto &tt : v->TupleTypes)
      out += (out.empty() ? "" : ", ") +
             (tt->Resolved ? tt->Resolved->toString() : std::string("?"));
    for (const auto &fd : v->Fields)
      out += (out.empty() ? "" : ", ") + fd->Name + ": " +
             (fd->Ty ? fd->Ty->toString() : std::string("?"));
    return out;
  }
  default:
    return "";
  }
}

/// `offset_of!(T, field)`. The offset comes from the same layout the code
/// generator uses, so it is the offset the program will actually see.
Value *CodeGen::emitOffsetOf(CallExpr *c, Type *t, const std::string &field) {
  llvm::Type *resultTy = lower(c->Ty);
  if (!t->is(TypeKind::Struct) && !t->is(TypeKind::Class)) {
    Diags.error(c->Range, "'{}' has no named fields to take an offset in",
                t->toString())
        .note("`offset_of!` applies to a struct or a class")
        .code(506);
    return ConstantInt::get(resultTy, 0);
  }
  auto fields = allFieldsOf(t->nominal());
  for (unsigned i = 0; i < fields.size(); ++i) {
    if (fields[i]->Name != field)
      continue;
    auto *layout = layoutOf(t->nominal(), t);
    const llvm::StructLayout *sl = M->getDataLayout().getStructLayout(layout);
    // A class stores its fields after the object header, and the header is
    // part of the same struct, so the index is already right.
    return ConstantInt::get(resultTy, sl->getElementOffset(i));
  }
  auto d = Diags.error(c->Range, "'{}' has no field named '{}'", t->toString(),
                       field);
  std::string names;
  for (FieldDecl *f : fields)
    names += (names.empty() ? "" : ", ") + f->Name;
  if (!names.empty())
    d.note("its fields are: {}", names);
  d.code(506);
  return ConstantInt::get(resultTy, 0);
}

/// Whether `t` binds the mark `markType`.
bool CodeGen::reflectConforms(Type *t, Type *markType, SourceRange where) {
  if (!markType || (!markType->is(TypeKind::Mark) &&
                    !markType->is(TypeKind::DynMark))) {
    Diags.error(where, "the second type argument of `conforms` has to be a mark")
        .note("write `reflect::conforms<T, io::Display>()`")
        .code(506);
    return false;
  }
  NominalDecl *want = markType->nominal();
  if (!want)
    return false;
  // A class inherits what its bases bound, so walk up as well as across.
  for (NominalDecl *nd = t->nominal(); nd;) {
    for (BindDecl *b : nd->Bindings)
      if (b->ResolvedMark == static_cast<void *>(want))
        return true;
    auto *cd = dyn_cast<ClassDecl>(static_cast<Decl *>(nd));
    nd = cd ? cd->Super : nullptr;
  }
  return false;
}

/// The `display` a type has bound, or null. `describe` defers to it, so a
/// value that decided how it looks keeps that decision wherever it appears.
FunctionDecl *CodeGen::displayMethodFor(Type *t) {
  NominalDecl *nd = t->nominal();
  if (!nd)
    return nullptr;
  for (BindDecl *b : nd->Bindings) {
    if (!b->ResolvedMark || b->ResolvedMark->Name != "Display")
      continue;
    for (auto &m : b->Methods)
      if (m->Name == "display" && m->Generics.empty())
        return m.get();
  }
  return nullptr;
}

/// A type's name without its module path: `Point`, not `app::Point`. The
/// module is usually noise in a rendered value, and `typeName` is there when
/// it is not.
std::string CodeGen::shortNameOf(Type *t) {
  std::string full = t->toString();
  size_t generic = full.find('<');
  size_t search = generic == std::string::npos ? full.size() : generic;
  size_t sep = full.rfind("::", search);
  return sep == std::string::npos ? full : full.substr(sep + 2);
}

/// A structural rendering of `v`, worked out from `t`.
///
/// Matched to `emitHash` and `emitEquals`: the same walk over the same layout,
/// producing something to read rather than a number or a verdict. A type that
/// binds `io::Display` is asked instead, so a value that has decided how it
/// looks keeps that decision even when it appears inside something else.
Value *CodeGen::emitDescribe(Value *v, Type *t) {
  auto text = [&](const std::string &s) { return emitStringLiteral(s, false); };
  // Each concatenation makes a fresh string and finishes with its operands.
  // Releasing them here keeps a rendering from leaking every piece it was
  // built out of; a literal is immortal, so releasing one does nothing.
  auto cat = [&](Value *a, Value *b) {
    Value *joined = B->CreateCall(
        runtimeFn("rune_string_concat", PtrTy, {PtrTy, PtrTy}), {a, b});
    auto release = runtimeFn("rune_release_shared", B->getVoidTy(), {PtrTy});
    B->CreateCall(release, {a});
    B->CreateCall(release, {b});
    return joined;
  };
  auto join = [&](std::initializer_list<Value *> parts) {
    Value *out = nullptr;
    for (Value *p : parts)
      out = out ? cat(out, p) : p;
    return out ? out : text("");
  };
  if (!v || !t)
    return text("");

  // A type that says how it prints is asked, whatever shape it is underneath.
  if (FunctionDecl *display = displayMethodFor(t)) {
    Function *f = declareFunction(display);
    Value *self = v;
    if (!t->is(TypeKind::Class)) {
      // `display` takes `&self`, so an aggregate needs somewhere to be.
      Value *slot = createEntryAlloca(lower(t), "describe.self");
      B->CreateStore(v, slot);
      self = slot;
    }
    return B->CreateCall(f, {self});
  }

  switch (t->kind()) {
  case TypeKind::Bool: {
    Value *sel = B->CreateSelect(B->CreateTrunc(v, B->getInt1Ty()),
                                 text("true"), text("false"));
    // Both arms are literals, which are immortal; nothing to retain.
    return sel;
  }
  case TypeKind::Int: {
    const char *fn = t->isSigned() ? "rune_string_from_i64"
                                      : "rune_string_from_u64";
    Value *wide = t->isSigned()
                      ? B->CreateSExtOrTrunc(v, B->getInt64Ty())
                      : B->CreateZExtOrTrunc(v, B->getInt64Ty());
    return B->CreateCall(runtimeFn(fn, PtrTy, {B->getInt64Ty()}), {wide});
  }
  case TypeKind::Float: {
    Value *wide = t->floatWidth() == 32
                      ? B->CreateFPExt(v, B->getDoubleTy())
                      : v;
    return B->CreateCall(
        runtimeFn("rune_string_from_f64", PtrTy, {B->getDoubleTy()}), {wide});
  }
  case TypeKind::Char:
    return B->CreateCall(
        runtimeFn("rune_string_from_char", PtrTy, {B->getInt32Ty()}),
        {B->CreateZExtOrTrunc(v, B->getInt32Ty())});
  case TypeKind::String:
    // Quoted, so an empty string and a missing one look different.
    return join({text("\""), B->CreateCall(runtimeFn("rune_retain_shared",
                                                     PtrTy, {PtrTy}), {v}),
                 text("\"")});
  case TypeKind::CString:
    return B->CreateCall(runtimeFn("rune_string_from_cstr", PtrTy, {PtrTy}),
                         {v});
  case TypeKind::Void:
    return text("()");
  case TypeKind::Tuple: {
    const auto &elems = t->tupleElements();
    Value *out = text("(");
    for (unsigned i = 0; i < elems.size(); ++i) {
      if (i)
        out = cat(out, text(", "));
      out = cat(out, emitDescribe(B->CreateExtractValue(v, i), elems[i]));
    }
    return cat(out, text(")"));
  }
  case TypeKind::Array: {
    Value *out = text("[");
    for (uint64_t i = 0; i < t->arraySize(); ++i) {
      if (i)
        out = cat(out, text(", "));
      out = cat(out, emitDescribe(
                         B->CreateExtractValue(v, static_cast<unsigned>(i)),
                         t->element()));
    }
    return cat(out, text("]"));
  }
  case TypeKind::Struct: {
    auto fields = allFieldsOf(t->nominal());
    Value *out = text(shortNameOf(t) + " { ");
    for (unsigned i = 0; i < fields.size(); ++i) {
      if (i)
        out = cat(out, text(", "));
      out = cat(out, text(fields[i]->Name + ": "));
      if (fields[i]->Ty)
        out = cat(out, emitDescribe(B->CreateExtractValue(v, i),
                                    fields[i]->Ty));
    }
    return cat(out, text(fields.empty() ? "}" : " }"));
  }
  case TypeKind::Class: {
    // A class is a reference: what it *is* matters more than what it holds,
    // and following it could run forever around a cycle.
    return join({text(shortNameOf(t) + "@"),
                 B->CreateCall(runtimeFn("rune_string_from_ptr", PtrTy,
                                         {PtrTy}), {v})});
  }
  case TypeKind::Enum: {
    auto *e = dyn_cast<EnumDecl>(static_cast<Decl *>(t->nominal()));
    StructType *layout = layoutOf(t->nominal(), t);
    Value *tmp = createEntryAlloca(layout, "describe.enum");
    B->CreateStore(v, tmp);
    Value *tag =
        B->CreateLoad(B->getInt32Ty(), B->CreateStructGEP(layout, tmp, 0));
    if (!e)
      return text("?");

    Value *slot = createEntryAlloca(PtrTy, "describe.enum.out");
    B->CreateStore(text("?"), slot);
    Function *fn = fs().Fn;
    auto *doneBB = BasicBlock::Create(*Ctx, "describe.done", fn);
    // The payload address has to be taken before the switch: the switch is
    // this block's terminator, and nothing may follow it.
    Value *payload = layout->getNumElements() >= 2
                         ? B->CreateStructGEP(layout, tmp, 1)
                         : nullptr;
    SwitchInst *sw = B->CreateSwitch(tag, doneBB,
                                     static_cast<unsigned>(e->Variants.size()));
    for (unsigned i = 0; i < e->Variants.size(); ++i) {
      const auto &variant = e->Variants[i];
      auto *caseBB = BasicBlock::Create(*Ctx, "describe.variant", fn);
      sw->addCase(B->getInt32(static_cast<uint32_t>(variant->Value)), caseBB);
      B->SetInsertPoint(caseBB);

      std::vector<Type *> payloadTypes;
      std::vector<std::string> payloadNames;
      for (const auto &tt : variant->TupleTypes) {
        payloadTypes.push_back(tt->Resolved);
        payloadNames.push_back("");
      }
      for (const auto &fd : variant->Fields) {
        payloadTypes.push_back(fd->Ty);
        payloadNames.push_back(fd->Name);
      }
      Value *out = text(variant->Name);
      if (!payloadTypes.empty() && payload) {
        llvm::Type *pt = variantPayloadType(e, i);
        const bool named = !variant->Fields.empty();
        out = cat(out, text(named ? " { " : "("));
        for (unsigned j = 0; j < payloadTypes.size(); ++j) {
          if (j)
            out = cat(out, text(", "));
          if (named)
            out = cat(out, text(payloadNames[j] + ": "));
          Type *ft = payloadTypes[j];
          if (!ft)
            continue;
          Value *fieldPtr = B->CreateStructGEP(pt, payload, j);
          out = cat(out, emitDescribe(B->CreateLoad(lower(ft), fieldPtr), ft));
        }
        out = cat(out, text(named ? " }" : ")"));
      }
      B->CreateStore(out, slot);
      B->CreateBr(doneBB);
    }
    B->SetInsertPoint(doneBB);
    return B->CreateLoad(PtrTy, slot);
  }
  case TypeKind::Pointer:
    return join({text("*"), B->CreateCall(runtimeFn("rune_string_from_ptr",
                                                    PtrTy, {PtrTy}), {v})});
  case TypeKind::Any:
    return join({text("Any("),
                 B->CreateCall(runtimeFn("rune_string_from_cstr", PtrTy,
                                         {PtrTy}),
                               {B->CreateCall(runtimeFn("rune_any_type_cstr",
                                                        PtrTy, {PtrTy}), {v})}),
                 text(")")});
  default:
    // A function, a mark object: nothing to look inside, so name the type.
    return text("<" + t->toString() + ">");
  }
}

Value *CodeGen::emitHash(Value *v, Type *t, Value *acc) {
  auto mix = [&](Value *bits) {
    return B->CreateCall(
        runtimeFn("rune_hash_mix", B->getInt64Ty(),
                  {B->getInt64Ty(), B->getInt64Ty()}),
        {acc, bits});
  };
  if (!v || !t)
    return acc;
  switch (t->kind()) {
  case TypeKind::Bool:
  case TypeKind::Char:
  case TypeKind::Int:
    return mix(B->CreateZExtOrTrunc(v, B->getInt64Ty()));
  case TypeKind::Float: {
    llvm::Type *asInt = t->floatWidth() == 32 ? B->getInt32Ty() : B->getInt64Ty();
    return mix(B->CreateZExtOrTrunc(B->CreateBitCast(v, asInt),
                                    B->getInt64Ty()));
  }
  case TypeKind::String:
    return mix(B->CreateCall(
        runtimeFn("rune_string_hash", B->getInt64Ty(), {PtrTy}), {v}));
  case TypeKind::CString:
    return mix(B->CreateCall(
        runtimeFn("rune_cstring_hash", B->getInt64Ty(), {PtrTy}), {v}));
  case TypeKind::Class:
  case TypeKind::Pointer:
  case TypeKind::Any:
    return mix(B->CreatePtrToInt(v, B->getInt64Ty()));
  case TypeKind::DynMark:
    return mix(B->CreatePtrToInt(B->CreateExtractValue(v, 0),
                                 B->getInt64Ty()));
  case TypeKind::Function:
    return mix(B->CreatePtrToInt(B->CreateExtractValue(v, 0),
                                 B->getInt64Ty()));
  case TypeKind::Tuple: {
    const auto &elems = t->tupleElements();
    for (unsigned i = 0; i < elems.size(); ++i)
      acc = emitHash(B->CreateExtractValue(v, i), elems[i], acc);
    return acc;
  }
  case TypeKind::Struct: {
    auto fields = allFieldsOf(t->nominal());
    for (unsigned i = 0; i < fields.size(); ++i)
      if (fields[i]->Ty)
        acc = emitHash(B->CreateExtractValue(v, i), fields[i]->Ty, acc);
    return acc;
  }
  case TypeKind::Array: {
    for (uint64_t i = 0; i < t->arraySize(); ++i)
      acc = emitHash(B->CreateExtractValue(v, static_cast<unsigned>(i)),
                     t->element(), acc);
    return acc;
  }
  case TypeKind::Enum: {
    // The tag, then the payload of whichever variant is live. Hashing the raw
    // payload bytes instead would make `Some("a")` and `Some("a")` differ,
    // since it would hash two String pointers rather than their contents.
    auto *e = dyn_cast<EnumDecl>(static_cast<Decl *>(t->nominal()));
    StructType *layout = layoutOf(t->nominal(), t);
    Value *tmp = createEntryAlloca(layout, "enum.hash");
    B->CreateStore(v, tmp);
    Value *tag =
        B->CreateLoad(B->getInt32Ty(), B->CreateStructGEP(layout, tmp, 0));
    acc = mix(B->CreateZExtOrTrunc(tag, B->getInt64Ty()));
    if (!e || layout->getNumElements() < 2)
      return acc;

    Value *payload = B->CreateStructGEP(layout, tmp, 1);
    Value *slot = createEntryAlloca(B->getInt64Ty(), "enum.hash.acc");
    B->CreateStore(acc, slot);
    Function *f = fs().Fn;
    auto *doneBB = BasicBlock::Create(*Ctx, "enum.hash.done", f);
    SwitchInst *sw = B->CreateSwitch(tag, doneBB,
                                     static_cast<unsigned>(e->Variants.size()));
    for (unsigned i = 0; i < e->Variants.size(); ++i) {
      const auto &variant = e->Variants[i];
      std::vector<Type *> payloadTypes;
      for (const auto &tt : variant->TupleTypes)
        payloadTypes.push_back(tt->Resolved);
      for (const auto &fd : variant->Fields)
        payloadTypes.push_back(fd->Ty);
      if (payloadTypes.empty())
        continue;
      auto *caseBB = BasicBlock::Create(*Ctx, "enum.hash.variant", f);
      sw->addCase(B->getInt32(static_cast<uint32_t>(variant->Value)), caseBB);
      B->SetInsertPoint(caseBB);
      llvm::Type *pt = variantPayloadType(e, i);
      Value *inner = B->CreateLoad(B->getInt64Ty(), slot);
      for (unsigned j = 0; j < payloadTypes.size(); ++j) {
        Type *ft = payloadTypes[j];
        if (!ft)
          continue;
        Value *fieldPtr = B->CreateStructGEP(pt, payload, j);
        Value *saved = acc;
        acc = inner;
        inner = emitHash(B->CreateLoad(lower(ft), fieldPtr), ft, inner);
        acc = saved;
      }
      B->CreateStore(inner, slot);
      B->CreateBr(doneBB);
    }
    B->SetInsertPoint(doneBB);
    return B->CreateLoad(B->getInt64Ty(), slot);
  }
  default:
    return acc;
  }
}

/// Structural equality, matched to `emitHash`: contents for a String, address
/// for a class, field by field for everything aggregate.
Value *CodeGen::emitEquals(Value *a, Value *b, Type *t) {
  if (!a || !b || !t)
    return B->getInt1(true);
  switch (t->kind()) {
  case TypeKind::Bool:
  case TypeKind::Char:
  case TypeKind::Int:
    return B->CreateICmpEQ(a, b);
  case TypeKind::Float:
    return B->CreateFCmpOEQ(a, b);
  case TypeKind::String:
    return B->CreateICmpEQ(
        B->CreateCall(runtimeFn("rune_string_compare", B->getInt32Ty(),
                                {PtrTy, PtrTy}),
                      {a, b}),
        B->getInt32(0));
  case TypeKind::CString:
    return B->CreateICmpEQ(
        B->CreateCall(runtimeFn("rune_cstring_hash", B->getInt64Ty(), {PtrTy}),
                      {a}),
        B->CreateCall(runtimeFn("rune_cstring_hash", B->getInt64Ty(), {PtrTy}),
                      {b}));
  case TypeKind::Class:
  case TypeKind::Pointer:
  case TypeKind::Any:
    return B->CreateICmpEQ(B->CreatePtrToInt(a, B->getInt64Ty()),
                           B->CreatePtrToInt(b, B->getInt64Ty()));
  case TypeKind::DynMark:
  case TypeKind::Function:
    return B->CreateICmpEQ(
        B->CreatePtrToInt(B->CreateExtractValue(a, 0), B->getInt64Ty()),
        B->CreatePtrToInt(B->CreateExtractValue(b, 0), B->getInt64Ty()));
  case TypeKind::Tuple: {
    Value *same = B->getInt1(true);
    const auto &elems = t->tupleElements();
    for (unsigned i = 0; i < elems.size(); ++i)
      same = B->CreateAnd(same, emitEquals(B->CreateExtractValue(a, i),
                                           B->CreateExtractValue(b, i),
                                           elems[i]));
    return same;
  }
  case TypeKind::Struct: {
    Value *same = B->getInt1(true);
    auto fields = allFieldsOf(t->nominal());
    for (unsigned i = 0; i < fields.size(); ++i)
      if (fields[i]->Ty)
        same = B->CreateAnd(same, emitEquals(B->CreateExtractValue(a, i),
                                             B->CreateExtractValue(b, i),
                                             fields[i]->Ty));
    return same;
  }
  case TypeKind::Array: {
    Value *same = B->getInt1(true);
    for (uint64_t i = 0; i < t->arraySize(); ++i) {
      unsigned idx = static_cast<unsigned>(i);
      same = B->CreateAnd(same, emitEquals(B->CreateExtractValue(a, idx),
                                           B->CreateExtractValue(b, idx),
                                           t->element()));
    }
    return same;
  }
  case TypeKind::Enum: {
    // Equal when the tags agree and, where the live variant carries anything,
    // the payloads agree too.
    auto *e = dyn_cast<EnumDecl>(static_cast<Decl *>(t->nominal()));
    StructType *layout = layoutOf(t->nominal(), t);
    Value *ta = createEntryAlloca(layout, "enum.eq.a");
    Value *tb = createEntryAlloca(layout, "enum.eq.b");
    B->CreateStore(a, ta);
    B->CreateStore(b, tb);
    Value *tagA =
        B->CreateLoad(B->getInt32Ty(), B->CreateStructGEP(layout, ta, 0));
    Value *tagB =
        B->CreateLoad(B->getInt32Ty(), B->CreateStructGEP(layout, tb, 0));
    Value *sameTag = B->CreateICmpEQ(tagA, tagB);
    if (!e || layout->getNumElements() < 2)
      return sameTag;

    Value *slot = createEntryAlloca(B->getInt1Ty(), "enum.eq");
    B->CreateStore(sameTag, slot);
    Function *f = fs().Fn;
    auto *cmpBB = BasicBlock::Create(*Ctx, "enum.eq.payload", f);
    auto *doneBB = BasicBlock::Create(*Ctx, "enum.eq.done", f);
    B->CreateCondBr(sameTag, cmpBB, doneBB);

    B->SetInsertPoint(cmpBB);
    Value *pa = B->CreateStructGEP(layout, ta, 1);
    Value *pb = B->CreateStructGEP(layout, tb, 1);
    SwitchInst *sw = B->CreateSwitch(tagA, doneBB,
                                     static_cast<unsigned>(e->Variants.size()));
    for (unsigned i = 0; i < e->Variants.size(); ++i) {
      const auto &variant = e->Variants[i];
      std::vector<Type *> payloadTypes;
      for (const auto &tt : variant->TupleTypes)
        payloadTypes.push_back(tt->Resolved);
      for (const auto &fd : variant->Fields)
        payloadTypes.push_back(fd->Ty);
      if (payloadTypes.empty())
        continue;
      auto *caseBB = BasicBlock::Create(*Ctx, "enum.eq.variant", f);
      sw->addCase(B->getInt32(static_cast<uint32_t>(variant->Value)), caseBB);
      B->SetInsertPoint(caseBB);
      llvm::Type *pt = variantPayloadType(e, i);
      Value *same = B->getInt1(true);
      for (unsigned j = 0; j < payloadTypes.size(); ++j) {
        Type *ft = payloadTypes[j];
        if (!ft)
          continue;
        Value *fa = B->CreateLoad(lower(ft), B->CreateStructGEP(pt, pa, j));
        Value *fb = B->CreateLoad(lower(ft), B->CreateStructGEP(pt, pb, j));
        same = B->CreateAnd(same, emitEquals(fa, fb, ft));
      }
      B->CreateStore(same, slot);
      B->CreateBr(doneBB);
    }
    B->SetInsertPoint(doneBB);
    return B->CreateLoad(B->getInt1Ty(), slot);
  }
  default:
    return B->getInt1(true);
  }
}

void CodeGen::emitEnumRefCount(Value *v, Type *t, bool retain) {
  auto *e = dyn_cast<EnumDecl>(static_cast<Decl *>(t->nominal()));
  if (!e || !v)
    return;
  StructType *layout = layoutOf(t->nominal(), t);
  if (layout->getNumElements() < 2)
    return; // no payload to manage

  // Work from memory: the active variant is only known at run time, so the
  // payload has to be reinterpreted through its address.
  Value *tmp = createEntryAlloca(layout, "enum.rc");
  B->CreateStore(v, tmp);
  Value *tag = B->CreateLoad(B->getInt32Ty(), B->CreateStructGEP(layout, tmp, 0));
  Value *payload = B->CreateStructGEP(layout, tmp, 1);

  Function *f = fs().Fn;
  auto *doneBB = BasicBlock::Create(*Ctx, "enum.rc.done", f);
  SwitchInst *sw = B->CreateSwitch(tag, doneBB,
                                   static_cast<unsigned>(e->Variants.size()));

  for (unsigned i = 0; i < e->Variants.size(); ++i) {
    const auto &variant = e->Variants[i];
    std::vector<Type *> payloadTypes;
    for (const auto &tt : variant->TupleTypes)
      payloadTypes.push_back(tt->Resolved);
    for (const auto &fd : variant->Fields)
      payloadTypes.push_back(fd->Ty);

    bool any = false;
    for (Type *pt : payloadTypes)
      if (pt && pt->isRefCounted())
        any = true;
    if (!any)
      continue;

    auto *caseBB = BasicBlock::Create(*Ctx, "enum.rc.variant", f);
    sw->addCase(B->getInt32(static_cast<uint32_t>(variant->Value)), caseBB);
    B->SetInsertPoint(caseBB);
    llvm::Type *pt = variantPayloadType(e, i);
    for (unsigned j = 0; j < payloadTypes.size(); ++j) {
      Type *ft = payloadTypes[j];
      if (!ft || !ft->isRefCounted())
        continue;
      Value *slot = B->CreateStructGEP(pt, payload, j);
      Value *field = B->CreateLoad(lower(ft), slot);
      if (retain)
        emitRetain(field, ft);
      else
        emitRelease(field, ft);
    }
    B->CreateBr(doneBB);
  }
  B->SetInsertPoint(doneBB);
}

void CodeGen::emitReleaseFields(Value *addr, Type *t) {
  if (!t || !t->isRefCounted())
    return;
  Value *v = B->CreateLoad(lower(t), addr);
  emitRelease(v, t);
}

Value *CodeGen::track(Value *v, Type *t) {
  // Only reference-counted temporaries are tracked. A value with a `deinit`
  // is destroyed by whoever ends up holding it — the local it is bound to, or
  // the parameter it is passed into — so releasing it here as well would
  // destroy it while its new owner still has it.
  if (!v || !t || !t->isRefCounted())
    return v;
  // A temporary may be produced inside a conditional block (the right-hand
  // side of `??`, an arm of a `match`) while the release runs after the paths
  // rejoin. Parking it in a stack slot that starts null makes the release
  // valid on every path, including the ones that never created it.
  Function *f = fs().Fn;
  IRBuilder<> entry(&f->getEntryBlock(), f->getEntryBlock().begin());
  llvm::Type *ty = lower(t);
  auto *slot = entry.CreateAlloca(ty, nullptr, "temp");
  entry.CreateStore(Constant::getNullValue(ty), slot);
  B->CreateStore(v, slot);
  fs().Temps.push_back({slot, t});
  // Remembering which slot a temporary was parked in is what lets anything
  // downstream work on *that* storage rather than on a copy of it — the
  // `adopt` under single ownership, and the array a slice is a view of.
  fs().TempOf[v] = slot;
  return v;
}

//===----------------------------------------------------------------------===//
// Cloning
//===----------------------------------------------------------------------===//

/// True when `t` holds, somewhere inside it, a type that writes its own
/// `clone`. Such a part has to be cloned by calling it even under reference
/// counting, where a clone is otherwise a share: a `Vector` shared rather
/// than copied leaves two of them holding one block, and the second to go
/// frees it again.
///
/// A class is not looked into: under counting the share *is* the clone, and
/// under single ownership the class's own entry below handles it.
bool CodeGen::containsUserClone(Type *t, std::set<Type *> &seen) {
  if (!t)
    return false;
  t = t->canonical();
  if (!seen.insert(t).second)
    return false;
  if (userCloneOf(t))
    return true;
  switch (t->kind()) {
  case TypeKind::Tuple:
    for (Type *e : t->tupleElements())
      if (containsUserClone(e, seen))
        return true;
    return false;
  case TypeKind::Array:
  case TypeKind::Slice:
    return containsUserClone(t->element(), seen);
  case TypeKind::Struct:
  case TypeKind::Enum: {
    NominalDecl *nd = t->nominal();
    if (!nd)
      return false;
    for (FieldDecl *f : allFieldsOf(nd))
      if (containsUserClone(f->Ty, seen))
        return true;
    if (auto *e = dyn_cast<EnumDecl>(static_cast<Decl *>(nd)))
      for (const auto &var : e->Variants) {
        for (const auto &tt : var->TupleTypes)
          if (tt->Resolved && containsUserClone(tt->Resolved, seen))
            return true;
        for (const auto &fd : var->Fields)
          if (containsUserClone(fd->Ty, seen))
            return true;
      }
    return false;
  }
  default:
    return false;
  }
}

/// True when copying `t` part by part would call a `clone` this program
/// never has: one whose `where` the instantiation fails — `Buffer<T>::clone`
/// needs `T: Clone`, and a closure is not — or one nothing asked Sema for.
/// The walk is `containsUserClone`'s, stopping at a class, whose copy is
/// asked of the object at run time instead.
bool CodeGen::cloneUnavailable(Type *t, std::set<Type *> &seen) {
  if (!t)
    return false;
  t = t->canonical();
  if (!seen.insert(t).second)
    return false;
  if (FunctionDecl *user = userCloneOf(t)) {
    if (KnownFunctions.empty())
      KnownFunctions.insert(Sema.Functions.begin(), Sema.Functions.end());
    return user->WhereUnmet || !user->Body || !KnownFunctions.count(user);
  }
  switch (t->kind()) {
  case TypeKind::Tuple:
    for (Type *e : t->tupleElements())
      if (cloneUnavailable(e, seen))
        return true;
    return false;
  case TypeKind::Array:
  case TypeKind::Slice:
    return cloneUnavailable(t->element(), seen);
  case TypeKind::Struct:
  case TypeKind::Enum: {
    NominalDecl *nd = t->nominal();
    if (!nd)
      return false;
    for (FieldDecl *f : allFieldsOf(nd))
      if (cloneUnavailable(f->Ty, seen))
        return true;
    if (auto *e = dyn_cast<EnumDecl>(static_cast<Decl *>(nd)))
      for (const auto &var : e->Variants) {
        for (const auto &tt : var->TupleTypes)
          if (tt->Resolved && cloneUnavailable(tt->Resolved, seen))
            return true;
        for (const auto &fd : var->Fields)
          if (cloneUnavailable(fd->Ty, seen))
            return true;
      }
    return false;
  }
  default:
    return false;
  }
}

Value *CodeGen::emitClone(Value *v, Type *t) {
  if (!v || !t)
    return v;
  if (t->isOpaque() && t->canonical() != t)
    return emitClone(v, t->canonical());
  // A type that writes its own `clone` is cloned by calling it, whatever the
  // memory model: only the type knows what a copy of it means.
  if (FunctionDecl *user = userCloneOf(t))
    return emitUserClone(user, v, t);
  // Under reference counting a clone is a *share*: claim a reference to every
  // heap part and hand back the same bits. That is exactly what returning a
  // value has always meant, so a container read written `slot[i].$clone()`
  // keeps the identity the plain `slot[i]` gave it — two lookups of one key
  // are still the one object. Only single ownership, which has no count to
  // share, makes `$clone` the deep, independent copy below.
  //
  // The exception is a value holding something that writes its own `clone`:
  // that one has to run, so the parts are walked here too and the sharing
  // happens at the leaves, where it is still what a clone means.
  if (!zombie()) {
    std::set<Type *> seen;
    if (!containsUserClone(t, seen)) {
      emitRetain(v, t);
      return v;
    }
  }
  switch (t->kind()) {
  case TypeKind::Void:
    return v;
  case TypeKind::Any:
    // The object says what it is, and its descriptor how to copy it.
    return B->CreateCall(runtimeFn("rune_clone_object", PtrTy, {PtrTy}), {v},
                         "clone");
  case TypeKind::DynMark: {
    // The same object copy; the mark table is the concrete type's still.
    Value *obj = B->CreateExtractValue(v, 0);
    Value *copy = B->CreateCall(
        runtimeFn("rune_clone_object", PtrTy, {PtrTy}), {obj}, "clone");
    return B->CreateInsertValue(v, copy, 0);
  }
  case TypeKind::Function:
    // No deep copy exists for a closure's captures; a "clone" shares them.
    // Under counting that is a retain; under single ownership `emitRetain` is
    // a no-op, so a program that reaches this holds two owners of one
    // closure — which the drop check catches at run time. Cloning one is
    // rare and the borrow checker steers away from it.
    emitRetain(v, t);
    return v;
  case TypeKind::String:
    return B->CreateCall(runtimeFn("rune_string_copy", PtrTy, {PtrTy}), {v},
                         "clone");
  case TypeKind::Tuple: {
    Value *out = UndefValue::get(lower(t));
    const auto &elems = t->tupleElements();
    for (unsigned i = 0; i < elems.size(); ++i)
      out = B->CreateInsertValue(
          out, emitClone(B->CreateExtractValue(v, i), elems[i]), i);
    return out;
  }
  case TypeKind::Struct: {
    Value *out = UndefValue::get(lower(t));
    auto fields = allFieldsOf(t->nominal());
    for (unsigned i = 0; i < fields.size(); ++i)
      out = B->CreateInsertValue(
          out, emitClone(B->CreateExtractValue(v, i), fields[i]->Ty), i);
    return out;
  }
  case TypeKind::Array: {
    Value *out = UndefValue::get(lower(t));
    for (uint64_t i = 0; i < t->arraySize(); ++i)
      out = B->CreateInsertValue(
          out,
          emitClone(B->CreateExtractValue(v, static_cast<unsigned>(i)),
                    t->element()),
          static_cast<unsigned>(i));
    return out;
  }
  case TypeKind::Class:
    // Under counting, a class's clone is the share above — its identity is
    // what a second reference to it means. Only single ownership, with no
    // count to share, copies the object.
    if (!zombie()) {
      emitRetain(v, t);
      return v;
    }
    // Through the object's own descriptor: a `Dog` named as an `Animal` is
    // copied as a `Dog`.
    return B->CreateCall(runtimeFn("rune_clone_object", PtrTy, {PtrTy}), {v},
                         "clone");
  case TypeKind::Enum:
    return B->CreateCall(cloneFnFor(t), {v}, "clone");
  default:
    return v; // trivially copyable
  }
}

/// Calls a type's own `clone`, giving it the receiver in the shape its
/// `self` parameter asks for.
Value *CodeGen::emitUserClone(FunctionDecl *user, Value *v, Type *t) {
  Function *f = declareFunction(user);
  const Param *selfP = user->Params.empty() ? nullptr : &user->Params[0];
  Value *self = v;
  if (selfP && selfP->IsSelf && selfP->SelfByRef && !t->isPointerLike()) {
    Value *slot = createEntryAlloca(lower(t), "clone.self");
    B->CreateStore(v, slot);
    self = slot;
  }
  return B->CreateCall(f, {self}, "clone");
}

/// A type's own `clone(&self) -> Self`, or null when it has none.
FunctionDecl *CodeGen::userCloneOf(Type *t) {
  if (!t || !t->isNominal())
    return nullptr;
  FunctionDecl *m = Sema::userClone(t);
  return m;
}

/// `void *(const void *)`: a copy of an object of exactly class `t`, for its
/// descriptor's `clone` slot. A class that writes its own `clone` is copied
/// by it; otherwise field by field.
Function *CodeGen::objectCloneFor(Type *t) {
  FunctionDecl *user = userCloneOf(t);
  if (!user) {
    // Field by field — unless a field's own `clone` does not exist here,
    // when the object cannot be copied at all and the slot stays empty.
    std::set<Type *> seen;
    if (NominalDecl *nd = t->nominal())
      for (FieldDecl *f : allFieldsOf(nd))
        if (cloneUnavailable(f->Ty, seen))
          return nullptr;
    return cloneFnFor(t);
  }
  // A generic's own `clone` exists for this instantiation only if something
  // asked Sema for it. When nothing did there is no body to point at, and a
  // field-by-field copy would be wrong for exactly the types that write
  // their own — so the slot stays empty and a copy made at run time says so.
  if (KnownFunctions.empty())
    KnownFunctions.insert(Sema.Functions.begin(), Sema.Functions.end());
  // Nor does one whose `where` this instantiation fails: `Vector<dyn Node>`
  // has no `clone`, since a `dyn Node` cannot be copied.
  if (!user->Body || !KnownFunctions.count(user) || user->WhereUnmet)
    return nullptr;
  std::string name = "rune.oclone." + typeSymbolFor(t->nominal());
  if (Function *f = M->getFunction(name))
    return f;
  auto *f = Function::Create(FunctionType::get(PtrTy, {PtrTy}, false),
                             GlobalValue::LinkOnceODRLinkage, name, *M);
  auto *saveBB = B->GetInsertBlock();
  auto saveIt = saveBB ? B->GetInsertPoint() : BasicBlock::iterator();
  FunctionState st;
  st.Fn = f;
  st.ReturnType = t;
  FnStack.push_back(st);
  fs().Scopes.push_back(LexicalScope{});
  B->SetInsertPoint(BasicBlock::Create(*Ctx, "entry", f));
  B->CreateRet(emitUserClone(user, f->getArg(0), t));
  fs().Scopes.pop_back();
  FnStack.pop_back();
  if (saveBB)
    B->SetInsertPoint(saveBB, saveIt);
  return f;
}

Function *CodeGen::cloneFnFor(Type *t) {
  auto it = CloneFns.find(t);
  if (it != CloneFns.end())
    return it->second;
  NominalDecl *nd = t->nominal();
  std::string symbol = typeSymbolFor(nd);
  llvm::Type *ty = lower(t);
  auto *ft = FunctionType::get(ty, {ty}, false);
  auto *f = Function::Create(ft, GlobalValue::LinkOnceODRLinkage,
                             "rune.clone." + symbol, *M);
  CloneFns[t] = f;

  auto *saveBB = B->GetInsertBlock();
  auto saveIt = saveBB ? B->GetInsertPoint() : BasicBlock::iterator();
  FunctionState st;
  st.Fn = f;
  st.ReturnType = t;
  FnStack.push_back(st);
  fs().Scopes.push_back(LexicalScope{});
  auto *entry = BasicBlock::Create(*Ctx, "entry", f);
  B->SetInsertPoint(entry);
  Value *src = f->getArg(0);

  if (auto *c = dyn_cast<ClassDecl>(static_cast<Decl *>(nd))) {
    // A fresh object of the same class, every field cloned in turn.
    StructType *layout = layoutOf(nd, t);
    GlobalVariable *info = emitTypeInfo(nd);
    uint64_t size = M->getDataLayout().getTypeAllocSize(layout).getFixedValue();
    Value *obj = B->CreateCall(
        runtimeFn("rune_alloc", PtrTy, {B->getInt64Ty(), PtrTy}),
        {ConstantInt::get(B->getInt64Ty(), size), info});
    (void)c;
    auto fields = allFieldsOf(nd);
    for (unsigned i = 0; i < fields.size(); ++i) {
      Type *ft2 = fields[i]->Ty;
      Value *from = B->CreateLoad(lower(ft2),
                                  B->CreateStructGEP(layout, src, 1 + i));
      B->CreateStore(emitClone(from, ft2),
                     B->CreateStructGEP(layout, obj, 1 + i));
    }
    B->CreateRet(obj);
  } else {
    // An enum: copy the tag, then clone the active variant's payload.
    auto *e = cast<EnumDecl>(static_cast<Decl *>(nd));
    StructType *layout = layoutOf(nd, t);
    Value *tmp = createEntryAlloca(layout, "enum.src");
    B->CreateStore(src, tmp);
    Value *out = createEntryAlloca(layout, "enum.clone");
    B->CreateStore(src, out); // tag and bits; payload fields are redone below
    if (layout->getNumElements() >= 2) {
      Value *tag = B->CreateLoad(B->getInt32Ty(),
                                 B->CreateStructGEP(layout, tmp, 0));
      auto *doneBB = BasicBlock::Create(*Ctx, "clone.done", f);
      SwitchInst *sw = B->CreateSwitch(
          tag, doneBB, static_cast<unsigned>(e->Variants.size()));
      for (unsigned i = 0; i < e->Variants.size(); ++i) {
        const auto &variant = e->Variants[i];
        std::vector<Type *> payloadTypes;
        for (const auto &tt : variant->TupleTypes)
          payloadTypes.push_back(tt->Resolved);
        for (const auto &fd : variant->Fields)
          payloadTypes.push_back(fd->Ty);
        bool any = false;
        for (Type *pt : payloadTypes)
          if (pt && pt->isRefCounted())
            any = true;
        if (!any)
          continue;
        auto *bb = BasicBlock::Create(*Ctx, "clone." + variant->Name, f);
        sw->addCase(B->getInt32(static_cast<uint32_t>(variant->Value)), bb);
        B->SetInsertPoint(bb);
        llvm::Type *pt = variantPayloadType(e, i);
        Value *srcPayload = B->CreateStructGEP(layout, tmp, 1);
        Value *dstPayload = B->CreateStructGEP(layout, out, 1);
        for (unsigned j = 0; j < payloadTypes.size(); ++j) {
          Type *ft2 = payloadTypes[j];
          if (!ft2 || !ft2->isRefCounted())
            continue;
          Value *from = B->CreateLoad(lower(ft2),
                                      B->CreateStructGEP(pt, srcPayload, j));
          B->CreateStore(emitClone(from, ft2),
                         B->CreateStructGEP(pt, dstPayload, j));
        }
        B->CreateBr(doneBB);
      }
      B->SetInsertPoint(doneBB);
    }
    B->CreateRet(B->CreateLoad(layout, out));
  }
  fs().Scopes.pop_back();
  FnStack.pop_back();
  if (saveBB)
    B->SetInsertPoint(saveBB, saveIt);
  return f;
}

//===----------------------------------------------------------------------===//
// Ownership under Zombie
//===----------------------------------------------------------------------===//

void CodeGen::adopt(Value *v) {
  auto it = fs().TempOf.find(v);
  if (it == fs().TempOf.end())
    return;
  // The statement's cleanup will find nothing in the slot.
  Value *slot = it->second;
  llvm::Type *ty = cast<AllocaInst>(slot)->getAllocatedType();
  B->CreateStore(Constant::getNullValue(ty), slot);
  fs().TempOf.erase(it);
}

Expr *CodeGen::movedPlaceOf(Expr *e) {
  while (e) {
    switch (e->Kind) {
    case NodeKind::DeclRef:
    case NodeKind::SelfRef:
    case NodeKind::Member:
    case NodeKind::Index:
    case NodeKind::Deref:
      // A field, an element or a name: a place, unless it is really a call
      // (an overloaded `[]` or `*`, a method used as a value).
      if (auto *i = dyn_cast<IndexExpr>(e))
        if (i->OverloadResolved || isa<RangeExpr>(i->Index.get()))
          return nullptr;
      if (auto *d = dyn_cast<DerefExpr>(e))
        if (d->OverloadResolved)
          return nullptr;
      if (auto *m = dyn_cast<MemberExpr>(e))
        if (m->FieldIndex < 0 && !m->IsTupleIndex)
          return nullptr;
      if (auto *r = dyn_cast<DeclRefExpr>(e))
        if (!r->Resolved || (!isa<VarDecl>(r->Resolved) &&
                             !isa<GlobalVarDecl>(r->Resolved)))
          return nullptr;
      return e;
    case NodeKind::Cast: {
      auto *c = cast<CastExpr>(e);
      // A class upcast reads the same handle; a numeric cast is a fresh
      // value and a raw-pointer cast owns nothing.
      if (c->Ty && c->Ty->isRefCounted() && c->Operand->Ty &&
          c->Operand->Ty->isRefCounted()) {
        e = c->Operand.get();
        continue;
      }
      return nullptr;
    }
    case NodeKind::Move:
      e = cast<MoveExpr>(e)->Operand.get();
      continue;
    case NodeKind::UnsafeBlock: {
      auto *u = cast<UnsafeBlockExpr>(e);
      if (u->Body && u->Body->Stmts.empty() && u->Body->Tail) {
        e = u->Body->Tail.get();
        continue;
      }
      return nullptr;
    }
    default:
      return nullptr;
    }
  }
  return nullptr;
}

/// True when a place expression is rooted in storage of its own rather than
/// in a value some call just produced.
bool CodeGen::placeRootIsStable(Expr *e) {
  while (e) {
    switch (e->Kind) {
    case NodeKind::DeclRef: {
      auto *r = cast<DeclRefExpr>(e);
      return r->Resolved && (isa<VarDecl>(r->Resolved) ||
                             isa<GlobalVarDecl>(r->Resolved));
    }
    case NodeKind::SelfRef:
      return true;
    case NodeKind::Member: {
      auto *m = cast<MemberExpr>(e);
      if (m->FieldIndex < 0 && !m->IsTupleIndex)
        return false;
      e = m->Base.get();
      continue;
    }
    case NodeKind::Index: {
      auto *i = cast<IndexExpr>(e);
      if (i->OverloadResolved || isa<RangeExpr>(i->Index.get()))
        return false;
      e = i->Base.get();
      continue;
    }
    case NodeKind::Deref: {
      auto *d = cast<DerefExpr>(e);
      if (d->OverloadResolved)
        return false;
      // Through a pointer: the address comes from the pointer's value, and
      // reading that again is free of consequences.
      return true;
    }
    case NodeKind::Cast:
      e = cast<CastExpr>(e)->Operand.get();
      continue;
    case NodeKind::Move:
      e = cast<MoveExpr>(e)->Operand.get();
      continue;
    case NodeKind::UnsafeBlock: {
      auto *u = cast<UnsafeBlockExpr>(e);
      if (u->Body && u->Body->Stmts.empty() && u->Body->Tail) {
        e = u->Body->Tail.get();
        continue;
      }
      return false;
    }
    default:
      return false;
    }
  }
  return false;
}

/// True when a place lives behind a pointer or a class handle.
///
/// `match self.head { ... }` on a `&var List` receiver names storage the
/// caller owns: the arms look at it where it is, and the borrow checker
/// checked them on that basis — their bindings alias the field rather than
/// taking it. Copying the subject here instead, and emptying the field to
/// pay for the copy, would leave the caller's list without its head.
bool CodeGen::placeBehindBorrow(Expr *e) {
  while (e) {
    switch (e->Kind) {
    case NodeKind::Member: {
      auto *m = cast<MemberExpr>(e);
      if (m->FieldIndex < 0 && !m->IsTupleIndex)
        return false;
      Type *bt = m->Base->Ty;
      if (bt && (bt->is(TypeKind::Pointer) || bt->is(TypeKind::Class)))
        return true;
      e = m->Base.get();
      continue;
    }
    case NodeKind::Index: {
      auto *i = cast<IndexExpr>(e);
      if (i->OverloadResolved || i->StringChar ||
          isa<RangeExpr>(i->Index.get()) || i->ThroughRawPointer)
        return false;
      Type *bt = i->Base->Ty;
      if (bt && bt->is(TypeKind::Pointer))
        return true;
      e = i->Base.get();
      continue;
    }
    case NodeKind::Deref: {
      auto *d = cast<DerefExpr>(e);
      return !d->OverloadResolved;
    }
    case NodeKind::SelfRef: {
      auto *sr = cast<SelfExpr>(e);
      return sr->Ty && (sr->Ty->is(TypeKind::Pointer) ||
                        sr->Ty->is(TypeKind::Class));
    }
    case NodeKind::Cast:
      e = cast<CastExpr>(e)->Operand.get();
      continue;
    case NodeKind::UnsafeBlock: {
      auto *u = cast<UnsafeBlockExpr>(e);
      if (u->Body && u->Body->Stmts.empty() && u->Body->Tail) {
        e = u->Body->Tail.get();
        continue;
      }
      return false;
    }
    default:
      return false;
    }
  }
  return false;
}

void CodeGen::emptyPlace(Expr *e, Type *t) {
  if (!e || !t)
    return;
  // A binding that only aliases borrowed content owns nothing to give up.
  if (auto *r = dyn_cast<DeclRefExpr>(e))
    if (auto *v = dyn_cast<VarDecl>(r->Resolved)) {
      if (v->ZombieAlias)
        return;
      // A value with a `deinit` has no empty state: its slot carries a flag
      // saying whether it still owns anything.
      if (Value *flag = liveFlagFor(v)) {
        B->CreateStore(B->getFalse(), flag);
        if (!t->isRefCounted())
          return;
      }
    }
  if (auto *sr = dyn_cast<SelfExpr>(e))
    if (sr->Binding && sr->Binding->ZombieAlias)
      return;
  if (!t->isRefCounted())
    return;
  // A field of a value a call produced — `(*handle).next`, `list.head()[0]` —
  // is a place all the same: the temporary owns it until something takes it.
  // Its address is the one the read just used, which is inside the very
  // temporary the statement will destroy; working the expression out again
  // would call whatever produced it a second time and empty a copy nothing
  // is going to look at.
  Value *addr = nullptr;
  auto known = fs().PlaceAddr.find(e);
  if (known != fs().PlaceAddr.end())
    addr = known->second;
  else if (placeRootIsStable(e))
    addr = emitLValue(e);
  if (!addr)
    return;
  B->CreateStore(Constant::getNullValue(lower(t)), addr);
}

/// True when `e`'s value is read out of raw memory — a `*var T` index or a
/// deref of a raw pointer, seen through the wrappers `movedPlaceOf` looks
/// past. Such a read aliases memory the compiler does not track: the slot goes
/// on owning what it holds (only `mem::store`/`take`/`drop_at` hand ownership
/// across that boundary), so a Zombie consumer must neither empty the slot nor
/// adopt the value for a drop — it is a plain borrow. The checker guarantees a
/// genuine move out of raw memory never reaches here (E0274).
bool CodeGen::readsUntrackedMemory(Expr *e) {
  while (e) {
    switch (e->Kind) {
    case NodeKind::Index:
      return cast<IndexExpr>(e)->ThroughRawPointer;
    case NodeKind::Deref: {
      auto *d = cast<DerefExpr>(e);
      return !d->OverloadResolved && d->Operand->Ty &&
             d->Operand->Ty->isRawPointer();
    }
    case NodeKind::Cast:
      e = cast<CastExpr>(e)->Operand.get();
      continue;
    case NodeKind::Move:
      e = cast<MoveExpr>(e)->Operand.get();
      continue;
    case NodeKind::UnsafeBlock: {
      auto *u = cast<UnsafeBlockExpr>(e);
      if (u->Body && u->Body->Stmts.empty() && u->Body->Tail) {
        e = u->Body->Tail.get();
        continue;
      }
      return false;
    }
    default:
      return false;
    }
  }
  return false;
}

void CodeGen::takeOwnership(Expr *e, Value *v, Type *t) {
  if (!zombie()) {
    emitRetain(v, t);
    return;
  }
  if (!v || !t)
    return;
  if (readsUntrackedMemory(e))
    return;
  if (Expr *place = movedPlaceOf(e)) {
    // Only a place the operand names directly outlives the operand: one
    // reached through a block may be that block's own local, which its
    // scope drops before the construction is done.
    if (PendingEmpties && place == e && PendingEmpties->here())
      PendingEmpties->List.push_back({place, t});
    else
      emptyPlace(place, t);
    return;
  }
  adopt(v);
}

/// The object a stored shared borrow keeps alive under counting, read out of
/// the borrow: the borrow itself for a one-word handle, which *is* the
/// object, or what it points at for a closure or a mark object, whose borrow
/// points at the pair.
Value *CodeGen::heldObject(Value *borrow, Type *ptrTy) {
  if (handleBorrow(ptrTy))
    return borrow;
  return B->CreateLoad(lower(ptrTy->pointee()), borrow, "held");
}

bool CodeGen::handleBorrow(Type *t) const {
  // In both memory models: a shared borrow of an object is the object.
  // Only a `&var` names the slot, since it may put a new object there.
  //
  // "Is the object" only works while the object fits where the borrow does,
  // which is one pointer. A mark object is two — an instance beside the
  // table of methods its type supplies — so a borrow of one points *at* the
  // pair and is read through like any other borrow.
  if (!t || !t->is(TypeKind::Pointer) || t->isRawPointer() ||
      t->isMutablePointer() || t->isWeakPointer() || !t->pointee())
    return false;
  Type *inner = t->pointee();
  // A closure is two words as well — code beside its environment — and a
  // borrow of one is lowered as one pointer, so it points *at* the pair.
  return inner->isHeapHandle() && !inner->is(TypeKind::DynMark) &&
         !inner->is(TypeKind::Function);
}

void CodeGen::emitStatementCleanup(bool consume) {
  if (blockIsTerminated()) {
    if (consume)
      fs().Temps.clear();
    return;
  }
  for (auto it = fs().Temps.rbegin(); it != fs().Temps.rend(); ++it) {
    llvm::Type *ty = lower(it->second);
    // References only. A temporary is tracked to balance a `+1`, not because
    // this statement owns the value: the slot it was stored into does, and
    // running a destructor here would destroy what that slot still holds.
    emitRelease(B->CreateLoad(ty, it->first), it->second);
    // Blank the slot so a second pass over the same statement (a loop body,
    // or an early exit that also runs cleanups) cannot release twice.
    B->CreateStore(Constant::getNullValue(ty), it->first);
  }
  if (consume)
    fs().Temps.clear();
}

void CodeGen::emitScopeCleanup(size_t scopeIndex, bool runDeferred) {
  if (scopeIndex >= fs().Scopes.size())
    return;
  // Control never reaches the end of a block that already diverged (a
  // `return`, a `break`, or a call that does not come back), so there is
  // nothing left to clean up and nowhere to put the instructions.
  if (blockIsTerminated())
    return;
  LexicalScope &scope = fs().Scopes[scopeIndex];
  if (runDeferred && !scope.Deferred.empty()) {
    // A deferred expression runs as its own statement, so whatever it creates
    // is released right after it, not left for a statement that already ended.
    auto pending = fs().Temps;
    fs().Temps.clear();
    for (auto it = scope.Deferred.rbegin(); it != scope.Deferred.rend(); ++it) {
      emitRValue(*it);
      emitStatementCleanup();
    }
    fs().Temps = pending;
  }
  for (auto it = scope.Locals.rbegin(); it != scope.Locals.rend(); ++it) {
    Type *ty = it->Ty;
    if (!needsDestruction(ty))
      continue;
    // The slot is blanked on the way out: a scope inside a loop is cleaned
    // once per iteration, and the allocas are reused, so a stale pointer must
    // not survive. `emitDestroy` does that for a reference-counted slot.
    emitDestroy(it->Addr, ty, it->LiveFlag);
  }
}

//===----------------------------------------------------------------------===//
// Value destructors
//===----------------------------------------------------------------------===//

FunctionDecl *CodeGen::valueDeinit(Type *t) {
  if (!t)
    return nullptr;
  while (t->is(TypeKind::Pointer))
    t = t->pointee();
  if (!t->is(TypeKind::Struct) && !t->is(TypeKind::Enum))
    return nullptr;
  NominalDecl *nd = t->nominal();
  return nd ? nd->Deinit : nullptr;
}

bool CodeGen::hasValueDeinit(Type *t) {
  if (!t)
    return false;
  auto cached = DeinitCache.find(t);
  if (cached != DeinitCache.end())
    return cached->second;
  // Insert `false` first: a type reachable from itself must not send this
  // walk round for ever, and a cycle contributes nothing on its own.
  DeinitCache[t] = false;
  bool answer = false;
  switch (t->kind()) {
  case TypeKind::Struct:
  case TypeKind::Enum: {
    NominalDecl *nd = t->nominal();
    if (nd && nd->Deinit) {
      answer = true;
      break;
    }
    if (!nd)
      break;
    for (auto &f : nd->Fields)
      if (hasValueDeinit(f->Ty)) { answer = true; break; }
    if (!answer)
      if (auto *e = dyn_cast<EnumDecl>(static_cast<Decl *>(nd)))
        for (auto &v : e->Variants) {
          for (auto &f : v->Fields)
            if (hasValueDeinit(f->Ty)) { answer = true; break; }
          if (answer) break;
          for (auto &tt : v->TupleTypes)
            if (tt && hasValueDeinit(tt->Resolved)) { answer = true; break; }
          if (answer) break;
        }
    break;
  }
  case TypeKind::Array:
    answer = hasValueDeinit(t->element());
    break;
  case TypeKind::Tuple:
    for (Type *e : t->tupleElements())
      if (hasValueDeinit(e)) { answer = true; break; }
    break;
  default:
    break;
  }
  DeinitCache[t] = answer;
  return answer;
}

void CodeGen::emitRunDeinits(Value *addr, Type *t) {
  if (!addr || !hasValueDeinit(t) || blockIsTerminated())
    return;

  if (FunctionDecl *d = valueDeinit(t)) {
    // `deinit` borrows `self`, so the slot's address is what it wants. A
    // class's is not reached this way at all — the runtime calls it when the
    // count hits zero — so this is only ever a struct's or an enum's.
    B->CreateCall(declareFunction(d), {addr});
  }

  // Then whatever the value contains. A struct, an array or a tuple is walked
  // in place, so a part with a `deinit` of its own is reached by address.
  switch (t->kind()) {
  case TypeKind::Struct: {
    auto *layout = llvm::dyn_cast<llvm::StructType>(lower(t));
    auto fields = allFieldsOf(t->nominal());
    if (layout)
      for (unsigned i = 0; i < fields.size(); ++i)
        if (hasValueDeinit(fields[i]->Ty))
          emitRunDeinits(B->CreateStructGEP(layout, addr, i), fields[i]->Ty);
    break;
  }
  case TypeKind::Array: {
    if (!hasValueDeinit(t->element()))
      break;
    auto *arrTy = llvm::ArrayType::get(lower(t->element()), t->arraySize());
    for (uint64_t i = 0; i < t->arraySize(); ++i)
      emitRunDeinits(B->CreateConstInBoundsGEP2_64(arrTy, addr, 0, i),
                     t->element());
    break;
  }
  case TypeKind::Tuple: {
    auto *layout = llvm::dyn_cast<llvm::StructType>(lower(t));
    const auto &elems = t->tupleElements();
    if (layout)
      for (unsigned i = 0; i < elems.size(); ++i)
        if (hasValueDeinit(elems[i]))
          emitRunDeinits(B->CreateStructGEP(layout, addr, i), elems[i]);
    break;
  }
  default:
    // An enum's payload would need a switch on the tag to reach; its own
    // `deinit` ran above, which is what a resource-owning enum declares.
    break;
  }
}

void CodeGen::emitDestroy(Value *addr, Type *t, Value *live) {
  if (!addr || !needsDestruction(t) || blockIsTerminated())
    return;

  // The flag guards the *destructor* and nothing else. A value handed on may
  // still hold references this slot claimed — a struct owning a descriptor
  // may also hold a String — and those are balanced by the count, which the
  // destination retained for itself. So the resource is released once, where
  // it is still owned, and the references are released here either way.
  BasicBlock *join = nullptr;
  if (live) {
    Function *f = fs().Fn;
    BasicBlock *doIt = BasicBlock::Create(*Ctx, "drop.live", f);
    join = BasicBlock::Create(*Ctx, "drop.done", f);
    B->CreateCondBr(B->CreateLoad(B->getInt1Ty(), live, "owns"), doIt, join);
    B->SetInsertPoint(doIt);
    B->CreateStore(B->getFalse(), live);
  }
  emitRunDeinits(addr, t);
  if (join) {
    if (!blockIsTerminated())
      B->CreateBr(join);
    B->SetInsertPoint(join);
  }

  if (t->isRefCounted()) {
    llvm::Type *lowered = lower(t);
    emitRelease(B->CreateLoad(lowered, addr), t);
    B->CreateStore(Constant::getNullValue(lowered), addr);
  }
}

void CodeGen::emitDestroyValue(Value *v, Type *t) {
  // Only the destructors: a value in a register that is about to be thrown
  // away is a tracked temporary too, and the statement's cleanup is what
  // releases the references it holds.
  if (!v || !hasValueDeinit(t))
    return;
  Function *f = fs().Fn;
  IRBuilder<> entry(&f->getEntryBlock(), f->getEntryBlock().begin());
  auto *slot = entry.CreateAlloca(lower(t), nullptr, "drop.tmp");
  B->CreateStore(v, slot);
  emitRunDeinits(slot, t);
}

void CodeGen::emitAllScopeCleanups(size_t downTo) {
  for (size_t i = fs().Scopes.size(); i > downTo; --i) {
    if (blockIsTerminated())
      return;
    emitScopeCleanup(i - 1);
  }
}

//===----------------------------------------------------------------------===//
// Helpers
//===----------------------------------------------------------------------===//

bool CodeGen::blockIsTerminated() const {
  BasicBlock *bb = B->GetInsertBlock();
  return !bb || bb->getTerminator() != nullptr;
}

void CodeGen::ensureTerminated(BasicBlock *target) {
  if (!blockIsTerminated())
    B->CreateBr(target);
}

Value *CodeGen::createEntryAlloca(llvm::Type *ty, const std::string &name) {
  Function *f = fs().Fn;
  IRBuilder<> entry(&f->getEntryBlock(), f->getEntryBlock().begin());
  return entry.CreateAlloca(ty, nullptr, name);
}

Value *CodeGen::createEntryAllocaZeroed(llvm::Type *ty,
                                        const std::string &name) {
  // One builder for both, so the store lands immediately after the alloca it
  // writes to. Two builders would each insert at the top of the block, and
  // the store would come out *before* what it refers to.
  Function *f = fs().Fn;
  IRBuilder<> entry(&f->getEntryBlock(), f->getEntryBlock().begin());
  Value *slot = entry.CreateAlloca(ty, nullptr, name);
  entry.CreateStore(Constant::getNullValue(ty), slot);
  return slot;
}

bool CodeGen::adoptsFreshConstruction(VarDecl *v, Expr *init) {
  if (!v || !init || !v->NoEscape || v->IsCaptured)
    return false;
  if (!v->Ty || !v->Ty->is(TypeKind::Class) || v->Ty->isUniq())
    return false;
  // Only a construction. Anything else — a call that returns a reference, a
  // read of a field — hands back something someone else may still hold, and
  // the count has to say so.
  auto *call = dyn_cast<CallExpr>(init);
  return call && call->ConstructsClass;
}

llvm::Value *CodeGen::liveFlagFor(VarDecl *v) {
  if (!v || FnStack.empty())
    return nullptr;
  auto it = fs().LiveFlags.find(v);
  return it == fs().LiveFlags.end() ? nullptr : it->second;
}

Value *CodeGen::declareLocalSlot(VarDecl *v, const std::string &name) {
  // Alternatives of a `|` pattern share one variable, so the slot may already
  // exist; reuse it rather than shadowing it with a second allocation.
  auto existing = fs().Slots.find(v);
  if (existing != fs().Slots.end())
    return existing->second;

  llvm::Type *ty = lower(v->Ty);
  // A reference-counted slot starts empty so the first store can release what
  // was there safely. The emptying belongs in the *entry* block: a binding may
  // be introduced on one path and cleaned up on another — `while x is Some(v)`
  // binds `v` only when the pattern matches, and gives the scope back when it
  // does not — and a slot zeroed only where it is bound is undefined on the
  // path that skipped it, which the release at the end of that scope reads.
  const bool counted = v->Ty && v->Ty->isRefCounted();
  Value *slot = counted ? createEntryAllocaZeroed(ty, name)
                        : createEntryAlloca(ty, name);
  fs().Slots[v] = slot;
  declareDebugVariable(v, slot, /*argIndex=*/0);
  // Somewhere in this function the binding hands its value on, so whether it
  // still owns one is a run-time question: an early `return` may already have
  // given it away by the time the block ends. A flag answers it. Only a value
  // with a destructor needs one — everything else is either copied, in which
  // case nothing is lost, or reference counted, where the count is the answer.
  Value *liveFlag = nullptr;
  if ((v->MovedSomewhere || (zombie() && v->ZombieMoved)) &&
      hasValueDeinit(v->Ty)) {
    // Zeroed in the entry block for the same reason: the flag is read
    // wherever the scope ends, including on a path that never reached the
    // declaration. False there means "owns nothing yet", which is right.
    liveFlag = createEntryAllocaZeroed(B->getInt1Ty(), name + ".owns");
    fs().LiveFlags[v] = liveFlag;
  }
  // An alias of borrowed content owns nothing: the scope has nothing to
  // destroy for it.
  if (!fs().Scopes.empty() && !(zombie() && v->ZombieAlias))
    fs().Scopes.back().Locals.push_back({slot, v->Ty, liveFlag});
  return slot;
}

Value *CodeGen::locationString(SourceRange range) {
  PresumedLoc pl = SM.decode(range.begin());
  std::string text = pl.isValid() ? (pl.File->Name + ":" +
                                     std::to_string(pl.Line) + ":" +
                                     std::to_string(pl.Column))
                                  : std::string("<unknown>");
  auto it = StringLiterals.find("loc:" + text);
  if (it != StringLiterals.end())
    return it->second;
  Constant *c = ConstantDataArray::getString(*Ctx, text, true);
  auto *gv = new GlobalVariable(*M, c->getType(), true,
                                GlobalValue::PrivateLinkage, c, ".rune.loc");
  gv->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
  StringLiterals["loc:" + text] = gv;
  return gv;
}

Value *CodeGen::coerce(Value *v, Type *from, Type *to) {
  // Into or out of a `some`: the representation is the concrete type's.
  if (from)
    from = from->canonical();
  if (to)
    to = to->canonical();
  if (!v || !from || !to || from == to)
    return v;
  if (from->isError() || to->isError() || to->isVoid())
    return v;

  if (from->isInt() && to->isInt()) {
    llvm::Type *dst = lower(to);
    if (from->intWidth() == to->intWidth())
      return v;
    return from->isSigned() ? B->CreateSExtOrTrunc(v, dst)
                            : B->CreateZExtOrTrunc(v, dst);
  }
  if (from->isInt() && to->isFloat())
    return from->isSigned() ? B->CreateSIToFP(v, lower(to))
                            : B->CreateUIToFP(v, lower(to));
  if (from->isFloat() && to->isInt())
    return to->isSigned() ? B->CreateFPToSI(v, lower(to))
                          : B->CreateFPToUI(v, lower(to));
  if (from->isFloat() && to->isFloat()) {
    if (from->floatWidth() < to->floatWidth())
      return B->CreateFPExt(v, lower(to));
    if (from->floatWidth() > to->floatWidth())
      return B->CreateFPTrunc(v, lower(to));
    return v;
  }
  if (from->is(TypeKind::Char) && to->isInt())
    return B->CreateZExtOrTrunc(v, lower(to));
  if (from->isInt() && to->is(TypeKind::Char))
    return B->CreateZExtOrTrunc(v, B->getInt32Ty());
  if (from->isBool() && to->isInt())
    return B->CreateZExt(v, lower(to));
  if (from->isInt() && to->isBool())
    return B->CreateICmpNE(v, Constant::getNullValue(lower(from)));
  if (from->is(TypeKind::Enum) && to->isNumeric() && from->nominal())
    if (auto *en = dyn_cast<EnumDecl>(static_cast<Decl *>(from->nominal())))
      if (en->RawFloat) {
        // The values sit in a constant table indexed by tag, which for a
        // float-valued enum counts up from zero.
        std::vector<double> raw;
        for (auto &var : en->Variants)
          raw.push_back(var->FloatValue);
        Constant *table = ConstantDataArray::get(*Ctx, raw);
        auto *global = new GlobalVariable(*M, table->getType(), true,
                                          GlobalValue::PrivateLinkage, table,
                                          "enum.values");
        Value *tag = B->CreateSExtOrTrunc(B->CreateExtractValue(v, 0),
                                          B->getInt64Ty());
        Value *at = B->CreateInBoundsGEP(table->getType(), global,
                                         {B->getInt64(0), tag});
        Value *value = B->CreateLoad(B->getDoubleTy(), at);
        return coerce(value, Types.f64(), to);
      }
  if (from->is(TypeKind::Enum) && to->isInt()) {
    Value *tag = B->CreateExtractValue(v, 0);
    return B->CreateSExtOrTrunc(tag, lower(to));
  }

  // T -> Option<T>
  if (isOptionType(to) && !isOptionType(from))
    return emitOptionSome(v, from, to);

  // T -> Result<T, E>, or E -> Result<T, E>. Which channel is decided by the
  // type, the same way Sema decided the conversion was allowed at all.
  if (isResultType(to) && !isResultType(from))
    return emitResultOf(v, from, to);

  // `Derived` -> `Base`: the parent's fields are the child's first fields, so
  // the value it wants is that prefix, read out field by field. (The two
  // lowered types differ in length, which is why this is a copy rather than
  // a reinterpretation.)
  if (from->is(TypeKind::Struct) && to->is(TypeKind::Struct) &&
      from->nominal() && to->nominal()) {
    bool inherits = false;
    for (NominalDecl *p = from->nominal()->InheritsDecl; p && !inherits;
         p = p->InheritsDecl)
      inherits = p->DeclaredType == to;
    if (inherits) {
      auto *parentTy = cast<StructType>(lower(to));
      const unsigned kept = parentTy->getNumElements();
      Value *out = UndefValue::get(parentTy);
      for (unsigned i = 0; i < kept; ++i)
        out = B->CreateInsertValue(out, B->CreateExtractValue(v, i), i);
      // Under single ownership the value is *moved* into the conversion, so
      // the fields the parent does not have are this conversion's to destroy
      // — nothing else will ever see them again. Under counting the callee
      // retains what it keeps and the source is released whole, so the books
      // already balance and touching them here would unbalance them.
      if (zombie()) {
        auto fields = allFieldsOf(from->nominal());
        for (unsigned i = kept; i < fields.size(); ++i) {
          Type *ft = fields[i]->Ty;
          if (!ft || !needsDestruction(ft))
            continue;
          Value *fv = B->CreateExtractValue(v, i);
          emitDestroyValue(fv, ft);
          emitRelease(fv, ft);
        }
      }
      return out;
    }
  }

  // `Base` -> `Derived` for an enum: the child's variants start with the
  // parent's and keep their numbers, so the tag carries over unchanged and
  // the payload is copied into the child's — larger, or the same — storage.
  if (from->is(TypeKind::Enum) && to->is(TypeKind::Enum) && from->nominal() &&
      to->nominal()) {
    bool inherits = false;
    for (NominalDecl *p = to->nominal()->InheritsDecl; p && !inherits;
         p = p->InheritsDecl)
      inherits = p->DeclaredType == from;
    if (inherits) {
      llvm::Type *wideTy = lower(to);
      llvm::Type *narrowTy = lower(from);
      Value *slot = createEntryAlloca(wideTy, "widened");
      B->CreateStore(Constant::getNullValue(wideTy), slot);
      Value *src = createEntryAlloca(narrowTy, "narrow");
      B->CreateStore(v, src);
      const llvm::DataLayout &dl = M->getDataLayout();
      uint64_t bytes = dl.getTypeStoreSize(narrowTy).getFixedValue();
      B->CreateMemCpy(slot, llvm::MaybeAlign(), src, llvm::MaybeAlign(),
                      ConstantInt::get(B->getInt64Ty(), bytes));
      return B->CreateLoad(wideTy, slot);
    }
  }

  // Under Zombie the box takes the value over — a class is its own box, and
  // anything else is copied into one — so the temporary it came from must
  // not also give it back at the end of the statement.
  // T -> dyn Mark
  if (to->is(TypeKind::DynMark) && !from->is(TypeKind::DynMark)) {
    Value *r = emitDynCoerce(v, from, to);
    if (zombie())
      adopt(v);
    return track(r, to);
  }

  // T -> Any. The box is the value; its header names the type.
  if (to->isAny() && !from->isAny()) {
    Value *r = emitAnyBox(v, from);
    if (zombie() && r == v)
      return r;                        // the same object, owned where it was
    if (zombie())
      adopt(v);
    return track(r, to);
  }

  // Tuples are coerced element by element, which is where a `String` in one
  // becomes the `Any` the destination asked for.
  if (from->is(TypeKind::Tuple) && to->is(TypeKind::Tuple) && from != to) {
    const auto &fe = from->tupleElements();
    const auto &te = to->tupleElements();
    if (fe.size() == te.size()) {
      Value *out = UndefValue::get(lower(to));
      for (unsigned i = 0; i < fe.size(); ++i) {
        Value *e = coerce(B->CreateExtractValue(v, i), fe[i], te[i]);
        // Under Zombie the new tuple owns what the conversion made — a box,
        // say — so that is handed on with the tuple, not given back here.
        if (zombie())
          adopt(e);
        out = B->CreateInsertValue(out, e, i);
      }
      return zombie() ? track(out, to) : out;
    }
  }

  // [N:T] -> [T]
  if (from->is(TypeKind::Array) && to->is(TypeKind::Slice)) {
    // A slice is an address and a count, so the array needs one — and when
    // the array is a temporary the statement already owns, the address to
    // use is the slot that temporary was parked in. Viewing a *copy* of it
    // would put the two out of step: a value taken out through the slice
    // would be taken out of the copy, while the cleanup went on releasing
    // the original, and both would then own it.
    Value *tmp = nullptr;
    auto parked = fs().TempOf.find(v);
    if (parked != fs().TempOf.end())
      tmp = parked->second;
    if (!tmp) {
      tmp = createEntryAlloca(lower(from), "arr.tmp");
      B->CreateStore(v, tmp);
    }
    Value *slice = UndefValue::get(lower(to));
    slice = B->CreateInsertValue(slice, tmp, 0);
    slice = B->CreateInsertValue(
        slice, ConstantInt::get(B->getInt64Ty(), from->arraySize()), 1);
    return slice;
  }

  // A borrow of something that copies freely, where the value is wanted:
  // read through it.
  if (from->is(TypeKind::Pointer) && !from->isRawPointer() && from->pointee() &&
      !to->is(TypeKind::Pointer) && !from->pointee()->isRefCounted() &&
      !handleBorrow(from)) {
    Value *loaded = B->CreateLoad(lower(from->pointee()), v, "deref");
    return coerce(loaded, from->pointee(), to);
  }
  // Under Zombie a shared `&Class` is the handle while a `&var Class` names
  // the slot holding it: one becomes the other through a load.
  if (handleBorrow(to) && from->is(TypeKind::Pointer) && !handleBorrow(from) &&
      !from->isRawPointer())
    return B->CreateLoad(PtrTy, v, "borrowed.obj");
  // Pointers, classes and strings are all machine pointers already.
  if (from->isPointerLike() && to->isPointerLike())
    return v;
  if (from->isPointerLike() && to->isInt())
    return B->CreatePtrToInt(v, lower(to));
  if (from->isInt() && to->isPointerLike())
    return B->CreateIntToPtr(v, PtrTy);
  return v;
}

Value *CodeGen::emitEnumVariant(Type *enumType, int variantIndex,
                                const std::vector<Value *> &payload) {
  auto *e = dyn_cast<EnumDecl>(static_cast<Decl *>(enumType->nominal()));
  if (!e || variantIndex < 0)
    return Constant::getNullValue(lower(enumType));
  StructType *layout = layoutOf(enumType->nominal(), enumType);
  Value *slot = createEntryAlloca(layout, "variant");
  B->CreateStore(Constant::getNullValue(layout), slot);
  const auto &variant = e->Variants[static_cast<size_t>(variantIndex)];
  B->CreateStore(B->getInt32(static_cast<uint32_t>(variant->Value)),
                 B->CreateStructGEP(layout, slot, 0));
  if (!payload.empty() && layout->getNumElements() > 1) {
    Value *payloadPtr = B->CreateStructGEP(layout, slot, 1);
    llvm::Type *pt = variantPayloadType(e, static_cast<unsigned>(variantIndex));
    for (unsigned i = 0; i < payload.size(); ++i)
      B->CreateStore(payload[i],
                     B->CreateStructGEP(pt, payloadPtr, i));
  }
  return B->CreateLoad(layout, slot);
}

Value *CodeGen::emitOptionSome(Value *v, Type *valueType, Type *optType) {
  Type *elem = optionPayload(optType);
  Value *coerced = coerce(v, valueType, elem);
  int idx = variantIndexNamed(optType, "Some");
  // The wrapped Option is a borrowed view of the same payload: whoever stores
  // it retains the whole enum, which walks into the payload. Retaining here as
  // well would leave the payload one reference too high.
  return emitEnumVariant(optType, idx, {coerced});
}

/// Wraps a bare value into the `Result` channel its type belongs to.
Value *CodeGen::emitResultOf(Value *v, Type *valueType, Type *resultType) {
  Type *okTy = resultValue(resultType);
  Type *errTy = resultError(resultType);
  const bool ok = okTy && isImplicitlyConvertible(valueType, okTy);
  Type *target = ok ? okTy : errTy;
  Value *coerced = coerce(v, valueType, target);
  int idx = variantIndexNamed(resultType, ok ? "Ok" : "Err");
  // Borrowed, like the Option wrap above: whoever stores the enum retains it,
  // and that walks into the payload.
  return emitEnumVariant(resultType, idx, {coerced});
}

Value *CodeGen::emitEnumTag(Value *enumValue, Type *enumType) {
  StructType *layout = layoutOf(enumType->nominal(), enumType);
  Value *slot = createEntryAlloca(layout, "enum.tag");
  B->CreateStore(enumValue, slot);
  return B->CreateLoad(B->getInt32Ty(), B->CreateStructGEP(layout, slot, 0));
}

Value *CodeGen::emitVariantPayload(Value *enumValue, Type *enumType,
                                   int variantIndex, unsigned field,
                                   Type *fieldType) {
  auto *e = dyn_cast<EnumDecl>(static_cast<Decl *>(enumType->nominal()));
  if (!e)
    return Constant::getNullValue(lower(fieldType));
  StructType *layout = layoutOf(enumType->nominal(), enumType);
  Value *slot = createEntryAlloca(layout, "enum.payload");
  B->CreateStore(enumValue, slot);
  Value *payloadPtr = B->CreateStructGEP(layout, slot, 1);
  llvm::Type *pt = variantPayloadType(e, static_cast<unsigned>(variantIndex));
  return B->CreateLoad(lower(fieldType),
                       B->CreateStructGEP(pt, payloadPtr, field));
}

/// True when `enumValue` holds the variant named `name`.
Value *CodeGen::emitVariantTest(Value *enumValue, Type *enumType,
                                const char *name) {
  auto *e = dyn_cast<EnumDecl>(static_cast<Decl *>(enumType->nominal()));
  int idx = variantIndexNamed(enumType, name);
  if (!e || idx < 0)
    return B->getInt1(false);
  int64_t want = e->Variants[static_cast<size_t>(idx)]->Value;
  return B->CreateICmpEQ(emitEnumTag(enumValue, enumType),
                         B->getInt32(static_cast<uint32_t>(want)));
}

Value *CodeGen::emitBoundsCheck(Value *index, Value *length, SourceRange range) {
  if (Opts.Safety != SafetyLevel::Full || !length)
    return index;
  Function *f = fs().Fn;
  auto *okBB = BasicBlock::Create(*Ctx, "bounds.ok", f);
  auto *failBB = BasicBlock::Create(*Ctx, "bounds.fail", f);
  Value *tooLow = B->CreateICmpSLT(index, ConstantInt::get(B->getInt64Ty(), 0));
  Value *tooHigh = B->CreateICmpSGE(index, length);
  B->CreateCondBr(B->CreateOr(tooLow, tooHigh), failBB, okBB);
  B->SetInsertPoint(failBB);
  B->CreateCall(
      runtimeFn("rune_panic_bounds", B->getVoidTy(),
                {B->getInt64Ty(), B->getInt64Ty(), PtrTy}),
      {index, length, locationString(range)});
  B->CreateUnreachable();
  B->SetInsertPoint(okBB);
  return index;
}

Value *CodeGen::emitLengthOf(Value *addr, Type *t) {
  if (t->is(TypeKind::Array))
    return ConstantInt::get(B->getInt64Ty(), t->arraySize());
  if (t->is(TypeKind::Slice)) {
    Value *lenPtr = B->CreateStructGEP(lower(t), addr, 1);
    return B->CreateLoad(B->getInt64Ty(), lenPtr);
  }
  return nullptr;
}

void CodeGen::reportUnsupported(SourceRange range, const std::string &what) {
  Diags.error(range, "{} is not supported by this code generator yet", what)
      .note("this construct type-checks, but has no lowering; please open an "
            "issue with a reproducer")
      .code(599);
}

//===----------------------------------------------------------------------===//
// Mark objects
//===----------------------------------------------------------------------===//

namespace {
/// A run-time type name turned into something a linker will accept, without
/// letting two different names collapse into one: every character that is not
/// already safe becomes `$` followed by its hex code.
std::string symbolise(const std::string &name) {
  std::string out;
  out.reserve(name.size());
  for (unsigned char c : name) {
    if (std::isalnum(c) || c == '_')
      out += static_cast<char>(c);
    else {
      static const char *hex = "0123456789abcdef";
      out += '$';
      out += hex[c >> 4];
      out += hex[c & 0xF];
    }
  }
  return out;
}
} // namespace

std::string CodeGen::typeDisplayName(NominalDecl *nd) {
  if (!nd)
    return "?";
  // The instantiation's own name, arguments included. This is what `Any`
  // reports, what `is` compares and what a traceback prints, so
  // `Vector<i64>` has to be able to say it is not `Vector<String>`.
  if (nd->DeclaredType)
    return runtimeTypeName(nd->DeclaredType);
  const auto *d = static_cast<const Decl *>(nd);
  return d->ModulePath.empty() ? d->Name : d->ModulePath + "::" + d->Name;
}

std::string CodeGen::typeSymbolFor(NominalDecl *nd) {
  // The linker symbol for a type's per-type globals: its destructor, its
  // descriptor, its vtable. All three use one-definition linkage so that a
  // type reached through a `.rul` resolves to a single object, which means
  // the name has to identify the type *exactly*.
  //
  // A generic's arguments are part of that identity. Leaving them out gave
  // every instantiation of `Vector` the same name; inside one object file
  // LLVM quietly numbered them apart, and across two the linker folded a
  // `Vector<i64>` destructor onto a `Vector<String>` one — whichever
  // happened to be emitted first. The result was a program that destroyed
  // the wrong thing, and only when it was split into a library and a binary.
  return symbolise(typeDisplayName(nd));
}

GlobalVariable *CodeGen::boxInfoFor(Type *concrete) {
  auto it = BoxInfos.find(concrete);
  if (it != BoxInfos.end())
    return it->second;

  llvm::Type *payload = lower(concrete);
  auto *boxTy = StructType::get(*Ctx, {ObjectHeaderTy, payload});
  uint64_t size = M->getDataLayout().getTypeAllocSize(boxTy).getFixedValue();

  // The name is the type's run-time identity, and the symbol is derived from
  // it, so every object file that boxes an `i64` names the same descriptor and
  // the linker folds them into one. `Any` then answers "is this an i64?" with
  // a pointer comparison rather than a string compare.
  const std::string name = runtimeTypeName(concrete);
  const std::string symbol = symbolise(name);

  if (GlobalVariable *existing = M->getNamedGlobal("rune.typeinfo.box." + symbol)) {
    BoxInfos[concrete] = existing;
    return existing;
  }

  Function *deinit = nullptr;
  if (concrete->isRefCounted()) {
    auto *ft = FunctionType::get(B->getVoidTy(), {PtrTy}, false);
    deinit = Function::Create(ft, GlobalValue::LinkOnceODRLinkage,
                              "rune.box.deinit." + symbol, *M);
    auto *saveBB = B->GetInsertBlock();
    auto saveIt = saveBB ? B->GetInsertPoint() : BasicBlock::iterator();
    FunctionState st;
    st.Fn = deinit;
    st.ReturnType = Types.voidType();
    FnStack.push_back(st);
    fs().Scopes.push_back(LexicalScope{});
    B->SetInsertPoint(BasicBlock::Create(*Ctx, "entry", deinit));
    Value *slot = B->CreateStructGEP(boxTy, deinit->getArg(0), 1);
    emitRelease(B->CreateLoad(payload, slot), concrete);
    B->CreateRetVoid();
    fs().Scopes.pop_back();
    FnStack.pop_back();
    if (saveBB)
      B->SetInsertPoint(saveBB, saveIt);
  }

  // Under single ownership a box can be copied — out of an `Any`, or a
  // `dyn` — into a new box of its own, the value inside cloned. Not when the
  // value holds a part with no `clone` here — a `Vector<dyn Node>`, say:
  // the slot stays empty, and a copy asked for at run time says so.
  Function *clone = nullptr;
  std::set<Type *> cloneSeen;
  if (zombie() && !cloneUnavailable(concrete, cloneSeen)) {
    auto *ft = FunctionType::get(PtrTy, {PtrTy}, false);
    clone = Function::Create(ft, GlobalValue::LinkOnceODRLinkage,
                             "rune.box.clone." + symbol, *M);
  }

  Constant *nameConst = ConstantDataArray::getString(*Ctx, name, true);
  auto *nameGV = new GlobalVariable(*M, nameConst->getType(), true,
                                    GlobalValue::PrivateLinkage, nameConst,
                                    ".rune.boxname");
  nameGV->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
  auto *gv = new GlobalVariable(
      *M, TypeInfoTy, true, GlobalValue::LinkOnceODRLinkage,
      ConstantStruct::get(
          TypeInfoTy,
          {nameGV, ConstantInt::get(B->getInt64Ty(), size),
           deinit ? static_cast<Constant *>(deinit)
                  : ConstantPointerNull::get(PtrTy),
           ConstantPointerNull::get(PtrTy), ConstantPointerNull::get(PtrTy),
           B->getInt32(0),
           clone ? static_cast<Constant *>(clone)
                 : ConstantPointerNull::get(PtrTy)}),
      "rune.typeinfo.box." + symbol);
  BoxInfos[concrete] = gv;

  if (clone) {
    auto *saveBB = B->GetInsertBlock();
    auto saveIt = saveBB ? B->GetInsertPoint() : BasicBlock::iterator();
    FunctionState st;
    st.Fn = clone;
    st.ReturnType = Types.voidType();
    FnStack.push_back(st);
    fs().Scopes.push_back(LexicalScope{});
    B->SetInsertPoint(BasicBlock::Create(*Ctx, "entry", clone));
    Value *box = B->CreateCall(
        runtimeFn("rune_alloc", PtrTy, {B->getInt64Ty(), PtrTy}),
        {ConstantInt::get(B->getInt64Ty(), size), gv});
    Value *from = B->CreateLoad(
        payload, B->CreateStructGEP(boxTy, clone->getArg(0), 1));
    B->CreateStore(emitClone(from, concrete),
                   B->CreateStructGEP(boxTy, box, 1));
    B->CreateRet(box);
    fs().Scopes.pop_back();
    FnStack.pop_back();
    if (saveBB)
      B->SetInsertPoint(saveBB, saveIt);
  }
  return gv;
}

Value *CodeGen::emitDynBox(Value *v, Type *concrete) {
  // A class instance is already a reference-counted object, so it can serve as
  // the mark object's payload directly — but the mark object is a separate
  // owner and needs a reference of its own.
  if (concrete->is(TypeKind::Class)) {
    emitRetain(v, concrete);
    return v;
  }

  llvm::Type *payload = lower(concrete);
  auto *boxTy = StructType::get(*Ctx, {ObjectHeaderTy, payload});
  uint64_t size = M->getDataLayout().getTypeAllocSize(boxTy).getFixedValue();
  Value *box = B->CreateCall(
      runtimeFn("rune_alloc", PtrTy, {B->getInt64Ty(), PtrTy}),
      {ConstantInt::get(B->getInt64Ty(), size), boxInfoFor(concrete)});
  emitRetain(v, concrete);
  B->CreateStore(v, B->CreateStructGEP(boxTy, box, 1));
  return box;
}

Function *CodeGen::markThunkFor(FunctionDecl *impl, Type *concrete) {
  auto key = std::make_pair(impl, concrete);
  auto it = MarkThunks.find(key);
  if (it != MarkThunks.end())
    return it->second;

  Function *target = declareFunction(impl);
  // Uniform shape: the receiver always arrives as a pointer to the object.
  std::vector<llvm::Type *> params{PtrTy};
  FunctionType *targetTy = target->getFunctionType();
  for (unsigned i = 1; i < targetTy->getNumParams(); ++i)
    params.push_back(targetTy->getParamType(i));
  auto *ft = FunctionType::get(targetTy->getReturnType(), params, false);
  auto *thunk = Function::Create(ft, GlobalValue::InternalLinkage,
                                 target->getName() + ".dyn", *M);
  MarkThunks[key] = thunk;

  auto *saveBB = B->GetInsertBlock();
  auto saveIt = saveBB ? B->GetInsertPoint() : BasicBlock::iterator();
  FunctionState st;
  st.Fn = thunk;
  st.ReturnType = impl->Ty ? impl->Ty->result() : Types.voidType();
  FnStack.push_back(st);
  fs().Scopes.push_back(LexicalScope{});
  B->SetInsertPoint(BasicBlock::Create(*Ctx, "entry", thunk));

  Value *object = thunk->getArg(0);
  Type *selfParam = nullptr;
  for (const Param &p : impl->Params)
    if (p.IsSelf)
      selfParam = p.Ty;

  Value *self = object;
  if (!concrete->is(TypeKind::Class)) {
    // The value lives just past the object header inside the box.
    auto *boxTy = StructType::get(*Ctx, {ObjectHeaderTy, lower(concrete)});
    Value *payload = B->CreateStructGEP(boxTy, object, 1);
    // A borrowing method wants that address; a by-value method wants the
    // value. "Borrowing" is not the same question as "is the self parameter a
    // pointer": the concrete type may itself be one (`bind M to *var u8`), and
    // a shared borrow of an object *is* the object. So compare the parameter
    // with the type it is a borrow of.
    const bool borrowsSelf =
        selfParam && selfParam->is(TypeKind::Pointer) && selfParam->pointee() &&
        selfParam->pointee()->canonical() == concrete->canonical() &&
        !handleBorrow(selfParam);
    self = borrowsSelf ? payload : B->CreateLoad(lower(concrete), payload);
  }

  std::vector<Value *> args{self};
  for (unsigned i = 1; i < thunk->arg_size(); ++i)
    args.push_back(thunk->getArg(i));
  Value *r = B->CreateCall(target, args);
  if (thunk->getReturnType()->isVoidTy())
    B->CreateRetVoid();
  else
    B->CreateRet(r);

  fs().Scopes.pop_back();
  FnStack.pop_back();
  if (saveBB)
    B->SetInsertPoint(saveBB, saveIt);
  return thunk;
}

GlobalVariable *CodeGen::markVTableFor(MarkDecl *mark, Type *concrete) {
  auto key = std::make_pair(mark, concrete);
  auto it = MarkVTables.find(key);
  if (it != MarkVTables.end())
    return it->second;

  std::vector<Constant *> entries;
  for (const auto &requirement : mark->Methods) {
    // A static requirement keeps its slot so the indices stay aligned with the
    // mark's method list, but there is nothing to dispatch to: Sema refuses to
    // call one through a `dyn` value.
    bool hasSelf = false;
    for (const Param &p : requirement->Params)
      hasSelf = hasSelf || p.IsSelf;
    if (!hasSelf) {
      entries.push_back(ConstantPointerNull::get(PtrTy));
      continue;
    }
    FunctionDecl *impl =
        Sema.markMethodFor(mark, concrete, requirement->Name);
    if (!impl || (!impl->Body && !impl->IsImported)) {
      // Sema has already reported the missing binding; keep going so the rest
      // of the module still compiles.
      entries.push_back(ConstantPointerNull::get(PtrTy));
      continue;
    }
    entries.push_back(markThunkFor(impl, concrete));
  }

  auto *arrTy = ArrayType::get(PtrTy, entries.size());
  auto *gv = new GlobalVariable(*M, arrTy, true, GlobalValue::InternalLinkage,
                                ConstantArray::get(arrTy, entries),
                                "rune.markvtable." + mark->Name);
  MarkVTables[key] = gv;
  return gv;
}

Value *CodeGen::emitDynCoerce(Value *v, Type *from, Type *dynType) {
  MarkDecl *mark = dynType->mark();
  if (!mark)
    return Constant::getNullValue(lower(dynType));
  Value *object = emitDynBox(v, from);
  Value *pair = UndefValue::get(lower(dynType));
  pair = B->CreateInsertValue(pair, object, 0);
  pair = B->CreateInsertValue(pair, markVTableFor(mark, from), 1);
  return pair;
}

//===----------------------------------------------------------------------===//
// `Any`
//
// An `Any` and a mark object hold their value the same way, and for the same
// reason: a class is already an object with a descriptor in its header, and
// everything else is copied into a box that gets one. The difference is what
// travels alongside. A mark object carries a vtable, because it knows in
// advance which methods will be asked for. An `Any` carries nothing, because
// it does not — the header is the whole answer, and every question about the
// value goes back to it.
//===----------------------------------------------------------------------===//

GlobalVariable *CodeGen::typeDescriptorFor(Type *t) {
  t = TypeContext::stripUniq(t);
  // A class already has a descriptor, and it is the one that knows about the
  // superclass chain, so `any is Animal` still finds a `Dog`.
  if (t->is(TypeKind::Class))
    return emitTypeInfo(t->nominal());
  return boxInfoFor(t);
}

Value *CodeGen::emitAnyBox(Value *v, Type *concrete) {
  return emitDynBox(v, TypeContext::stripUniq(concrete));
}

Value *CodeGen::emitAnyIs(Value *any, Type *target) {
  if (!any || !target || !isStorableInAny(target))
    return B->getInt1(false);
  Value *r = B->CreateCall(
      runtimeFn("rune_any_is", B->getInt32Ty(), {PtrTy, PtrTy}),
      {any, typeDescriptorFor(target)});
  return B->CreateICmpNE(r, B->getInt32(0), "any.is");
}

Value *CodeGen::emitAnyUnbox(Value *any, Type *target) {
  target = TypeContext::stripUniq(target);
  // The `Any` keeps what it holds, so what comes out is a second owner: a
  // share under counting, a copy under Zombie, which has no count to share.
  // A class was never boxed: the `Any` *is* the object.
  if (target->is(TypeKind::Class)) {
    if (zombie())
      return emitClone(any, target);
    emitRetain(any, target);
    return any;
  }
  llvm::Type *payload = lower(target);
  auto *boxTy = StructType::get(*Ctx, {ObjectHeaderTy, payload});
  Value *v = B->CreateLoad(payload, B->CreateStructGEP(boxTy, any, 1),
                           "any.value");
  if (zombie())
    return emitClone(v, target);
  emitRetain(v, target);
  return v;
}

Value *CodeGen::emitAnyTypeName(Value *any) {
  Value *name = B->CreateCall(
      runtimeFn("rune_any_type_cstr", PtrTy, {PtrTy}), {any});
  return B->CreateCall(runtimeFn("rune_string_from_cstr", PtrTy, {PtrTy}),
                       {name});
}

Value *CodeGen::emitAnyIntrinsic(CallExpr *c, const std::string &which,
                                 Type *typeArg) {
  auto *member = dyn_cast<MemberExpr>(c->Callee.get());
  if (!member)
    return nullptr;
  Value *any = emitRValue(member->Base.get());
  if (!any)
    return Constant::getNullValue(lower(c->Ty));

  if (which == "any_type_name")
    return track(emitAnyTypeName(any), Types.stringType());

  if (which == "any_holds")
    return emitAnyIs(any, typeArg);

  if (which == "any_get") {
    // `Some(value)` or `None`, decided by the descriptor in the header.
    Value *slot = createEntryAlloca(lower(c->Ty), "any.get");
    B->CreateStore(Constant::getNullValue(lower(c->Ty)), slot);
    Function *f = fs().Fn;
    auto *hitBB = BasicBlock::Create(*Ctx, "any.get.hit", f);
    auto *missBB = BasicBlock::Create(*Ctx, "any.get.miss", f);
    auto *doneBB = BasicBlock::Create(*Ctx, "any.get.done", f);
    B->CreateCondBr(emitAnyIs(any, typeArg), hitBB, missBB);

    B->SetInsertPoint(hitBB);
    B->CreateStore(emitOptionSome(emitAnyUnbox(any, typeArg), typeArg, c->Ty),
                   slot);
    B->CreateBr(doneBB);

    B->SetInsertPoint(missBB);
    B->CreateStore(emitEnumVariant(c->Ty, variantIndexNamed(c->Ty, "None"), {}),
                   slot);
    B->CreateBr(doneBB);

    B->SetInsertPoint(doneBB);
    return track(B->CreateLoad(lower(c->Ty), slot), c->Ty);
  }

  if (which == "any_expect") {
    Function *f = fs().Fn;
    auto *okBB = BasicBlock::Create(*Ctx, "any.expect.ok", f);
    auto *badBB = BasicBlock::Create(*Ctx, "any.expect.bad", f);
    B->CreateCondBr(emitAnyIs(any, typeArg), okBB, badBB);

    // The message names both types, so the report says what was there as well
    // as what was wanted.
    B->SetInsertPoint(badBB);
    std::string wanted = runtimeTypeName(typeArg);
    Constant *msg = ConstantDataArray::getString(*Ctx, wanted, true);
    auto *msgGV = new GlobalVariable(*M, msg->getType(), true,
                                     GlobalValue::PrivateLinkage, msg,
                                     ".rune.anyname");
    msgGV->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
    B->CreateCall(runtimeFn("rune_panic_any", B->getVoidTy(),
                            {PtrTy, PtrTy, PtrTy}),
                  {msgGV, any, locationString(c->Range)});
    B->CreateUnreachable();

    B->SetInsertPoint(okBB);
    return track(emitAnyUnbox(any, typeArg), typeArg);
  }
  return nullptr;
}

//===----------------------------------------------------------------------===//
// Declarations
//===----------------------------------------------------------------------===//

/// Gives `gv` the linkage for a definition that may appear in more than one
/// object and should merge rather than collide.
///
/// `weak_odr` says that everywhere but COFF, where it becomes a *weak
/// external* — a reference with a fallback, which another object's reference
/// does not resolve against. A COMDAT with strong linkage says the same thing
/// in the form COFF understands: keep one, discard the rest.
void CodeGen::setMergeableLinkage(llvm::GlobalObject *gv,
                                  const std::string &name) {
  if (llvm::Triple(M->getTargetTriple()).isOSBinFormatCOFF()) {
    gv->setLinkage(GlobalValue::ExternalLinkage);
    gv->setComdat(M->getOrInsertComdat(name));
    return;
  }
  gv->setLinkage(GlobalValue::WeakODRLinkage);
}

/// Mergeable, and droppable when nothing here reaches it.
///
/// The standard library is compiled from source into every artefact that uses
/// it, so a hello-world otherwise carries every function in it — nine hundred
/// of them, for the seventeen it calls. `linkonce_odr` says what is true of
/// such a copy: identical wherever it appears, so keep one; and belonging to
/// nobody in particular, so a copy nothing calls can go. That last part is
/// what `weak_odr` cannot say, and why a library's own exports keep it.
///
/// COFF needs the same COMDAT `setMergeableLinkage` uses — its linkage alone
/// carries neither half of the meaning.
void CodeGen::setDiscardableLinkage(llvm::GlobalObject *gv,
                                    const std::string &name) {
  gv->setLinkage(GlobalValue::LinkOnceODRLinkage);
  if (llvm::Triple(M->getTargetTriple()).isOSBinFormatCOFF())
    gv->setComdat(M->getOrInsertComdat(name));
}

bool CodeGen::isAncillary(const Decl *d) const {
  return d && Sema.isAncillary(d->ModulePath);
}

Function *CodeGen::declareFunction(FunctionDecl *fn) {
  auto it = Functions.find(fn);
  if (it != Functions.end())
    return it->second;

  FunctionType *ft = functionTypeFor(fn);
  std::string name = fn->MangledName.empty() ? fn->Name : fn->MangledName;

  // The same foreign function may be declared in several modules. Reuse the
  // symbol rather than letting LLVM invent `name.1`, and report a genuine
  // disagreement about its signature.
  if (Function *existing = M->getFunction(name)) {
    if (existing->getFunctionType() != ft) {
      Diags.error(fn->NameRange.isValid() ? fn->NameRange : fn->Range,
                  "'{}' is declared with two different signatures", name)
          .note("every declaration of a foreign function must agree on its "
                "parameter and result types")
          .code(501);
    }
    Functions[fn] = existing;
    fn->CodeGenFn = existing;
    return existing;
  }

  auto *f = Function::Create(ft, GlobalValue::ExternalLinkage, name, *M);
  if (isCxxExtern(fn)) {
    const CxxSignature &sig = cxxSignatureFor(fn);
    f->setCallingConv(sig.CC);
    applyCxxAttributes(nullptr, f, sig);
  } else if (const CxxSignature *sig = cSignatureFor(fn)) {
    applyCxxAttributes(nullptr, f, *sig);
  }
  // A traceback walks the frame-pointer chain, so a debug build has to keep
  // one in every function or the walk stops at the first omission.
  if (Opts.DebugInfo)
    f->addFnAttr("frame-pointer", "all");
  // Only definitions get restricted linkage: a bodyless declaration has to
  // stay external so the linker can resolve it.
  bool hasDefinition = fn->Body || fn->SourceClosure;
  if (fn->IsImported)
    hasDefinition = false; // the definition lives in the imported library
  if (hasDefinition && !fn->IsExtern && fn->Name != "main") {
    // A `pub` function can end up compiled into more than one artefact — the
    // standard library lands in every object that uses it. Mangled names
    // already encode the module, so identically named definitions really are
    // the same function and may be merged. `weak_odr` rather than
    // `linkonce_odr`: a library's exports must survive optimisation even
    // when nothing inside that library calls them.
    //
    // Methods follow the type rather than the module. Whether a type conforms
    // to a mark, or what methods it has, is visible wherever the type is, so a
    // method cannot be private to the object that happened to compile it.
    bool isMethod = fn->OwnerType != nullptr ||
                    (fn->Parent && isa<NominalDecl>(fn->Parent));
    // A debug build keeps every function nameable so a traceback can say what
    // it was in, rather than showing a bare address. `weak_odr` rather than
    // `external`: two objects that both carry the standard library have to
    // merge those definitions, not collide over them.
    // `@export` is different in kind: it says this function is *the* one
    // that answers to that name from outside. A weak definition would not
    // pull its object out of an archive on COFF, which is how the runtime is
    // linked, and merging two different `@export("rune_alloc")`s was never
    // wanted anyway. Give it strong external linkage.
    if (isExportedFunction(fn))
      f->setLinkage(GlobalValue::ExternalLinkage);
    else if (isAncillary(fn))
      // Not this artefact's API: a copy of somebody else's code, kept only
      // where this one reaches it.
      setDiscardableLinkage(f, name);
    else if (fn->IsPublic || isMethod || Opts.DebugInfo)
      setMergeableLinkage(f, name);
    else
      f->setLinkage(GlobalValue::InternalLinkage);
  }
  // `@weak`: a definition another object may replace — a default handler a
  // program overrides by defining its own under the same name.
  if (hasDefinition && fn->hasAttr("weak"))
    f->setLinkage(GlobalValue::WeakAnyLinkage);
  // A freestanding program is its own C library: LLVM must not turn a loop
  // that copies bytes into a call to `memcpy`, least of all inside `memcpy`.
  // This is what `-ffreestanding` does for C.
  if (hasDefinition && Opts.Freestanding)
    f->addFnAttr("no-builtins");
  if (fn->hasAttr("inline"))
    f->addFnAttr(llvm::Attribute::AlwaysInline);
  if (fn->hasAttr("noinline"))
    f->addFnAttr(llvm::Attribute::NoInline);
  Functions[fn] = f;
  fn->CodeGenFn = f;
  return f;
}

/// The value an immutable global is worth, when that is decided already.
///
/// `pub let bits: i64 = 64` is a *number*, and a program that reads it should
/// pay nothing for the reading. Anything whose value needs the program to be
/// running — a string, a call, an object — is left to the initialiser.
Constant *CodeGen::constantInitialiserFor(GlobalVarDecl *g) {
  if (!g || !g->Init || !g->Ty)
    return nullptr;
  auto it = FoldedGlobals.find(g);
  if (it != FoldedGlobals.end())
    return it->second;
  Constant *c = constantValueOf(g->Init.get(), g->Ty);
  FoldedGlobals[g] = c;
  return c;
}

/// `e` as a `t`, when that is known without running anything: a number, a
/// `bool`, a string as a `CString`, a top-level function as a `@cfunction`,
/// and arrays and structs made of those. Null for anything else, which is
/// then left to the generated initialiser.
///
/// This is what puts a table in the image rather than in code that builds it
/// at start-up: a font, a score table, an IDT's handlers. Built by code, each
/// is an aggregate made on the stack and stored whole — tens of kilobytes of
/// stack for a program whose stack may be a few, and time spent before
/// anything runs.
Constant *CodeGen::constantValueOf(Expr *e, Type *t) {
  if (!e || !t)
    return nullptr;
  t = t->canonical();
  bool negate = false;
  if (auto *u = dyn_cast<UnaryExpr>(e))
    if (u->Op == UnaryOp::Neg) {
      negate = true;
      e = u->Operand.get();
    }
  if (auto *lit = dyn_cast<IntLitExpr>(e)) {
    if (!t->isInt())
      return nullptr;
    int64_t v = static_cast<int64_t>(lit->Value);
    if (negate || lit->IsNegated)
      v = -v;
    return ConstantInt::get(lower(t), static_cast<uint64_t>(v), t->isSigned());
  }
  if (auto *lit = dyn_cast<FloatLitExpr>(e)) {
    if (!t->isFloat())
      return nullptr;
    return ConstantFP::get(lower(t), negate ? -lit->Value : lit->Value);
  }
  if (negate)
    return nullptr;
  if (auto *lit = dyn_cast<BoolLitExpr>(e)) {
    if (!t->isBool())
      return nullptr;
    return ConstantInt::get(lower(t), lit->Value ? 1 : 0);
  }
  if (auto *lit = dyn_cast<StringLitExpr>(e)) {
    if (!t->is(TypeKind::CString))
      return nullptr;
    return dyn_cast<Constant>(emitStringLiteral(lit->Value, /*asCString=*/true));
  }
  if (auto *ref = dyn_cast<DeclRefExpr>(e)) {
    // A top-level function where a C function pointer is wanted is its
    // address, which the linker fills in.
    auto *fn = ref->Resolved ? dyn_cast<FunctionDecl>(ref->Resolved) : nullptr;
    if (!fn || !t->is(TypeKind::CFunction) || !fn->Generics.empty() ||
        fn->Parent || fn->Flavour == FunctionFlavour::Closure)
      return nullptr;
    return cAdapterFor(fn, declareFunction(fn));
  }
  if (auto *arr = dyn_cast<ArrayLitExpr>(e)) {
    if (!t->is(TypeKind::Array) || !t->element())
      return nullptr;
    auto *arrTy = dyn_cast<ArrayType>(lower(t));
    if (!arrTy)
      return nullptr;
    std::vector<Constant *> elems;
    if (arr->RepeatCount) {
      if (arr->Elements.size() != 1)
        return nullptr;
      Constant *one = constantValueOf(arr->Elements[0].get(), t->element());
      if (!one)
        return nullptr;
      if (one->isNullValue())
        return ConstantAggregateZero::get(arrTy);
      elems.assign(arrTy->getNumElements(), one);
    } else {
      if (arr->Elements.size() != arrTy->getNumElements())
        return nullptr;
      for (const ExprPtr &el : arr->Elements) {
        Constant *c = constantValueOf(el.get(), t->element());
        if (!c)
          return nullptr;
        elems.push_back(c);
      }
    }
    return ConstantArray::get(arrTy, elems);
  }
  if (auto *lit = dyn_cast<StructLitExpr>(e)) {
    if (!t->is(TypeKind::Struct) || lit->Base || !t->nominal())
      return nullptr;
    auto *stTy = dyn_cast<StructType>(lower(t));
    std::vector<FieldDecl *> fields = allFieldsOf(t->nominal());
    if (!stTy || stTy->getNumElements() != fields.size() ||
        lit->Fields.size() != fields.size())
      return nullptr;
    std::vector<Constant *> elems(fields.size(), nullptr);
    for (const StructLitField &f : lit->Fields) {
      if (!f.Value || f.FieldIndex >= fields.size())
        return nullptr;
      Constant *c = constantValueOf(f.Value.get(), fields[f.FieldIndex]->Ty);
      if (!c)
        return nullptr;
      elems[f.FieldIndex] = c;
    }
    for (Constant *c : elems)
      if (!c)
        return nullptr;
    return ConstantStruct::get(stTy, elems);
  }
  return nullptr;
}

/// True when `e` is worth all zero bytes as a `t`: `0`, `0.0`, `false`, or an
/// array of them, written out or as `[v; n]`.
bool CodeGen::isZeroInitialiser(Expr *e, Type *t) {
  if (!e || !t)
    return false;
  t = t->canonical();
  if (auto *lit = dyn_cast<IntLitExpr>(e))
    return t->isInt() && lit->Value == 0;
  if (auto *lit = dyn_cast<FloatLitExpr>(e))
    // Negative zero has its sign bit set, so only a plain `0.0` counts.
    return t->isFloat() && lit->Value == 0.0 && !std::signbit(lit->Value);
  if (auto *lit = dyn_cast<BoolLitExpr>(e))
    return t->isBool() && !lit->Value;
  if (auto *arr = dyn_cast<ArrayLitExpr>(e)) {
    if (!t->is(TypeKind::Array) || !t->element())
      return false;
    for (const ExprPtr &el : arr->Elements)
      if (!isZeroInitialiser(el.get(), t->element()))
        return false;
    return true;
  }
  return false;
}

GlobalVariable *CodeGen::declareGlobal(GlobalVarDecl *g) {
  auto it = Globals.find(g);
  if (it != Globals.end())
    return it->second;
  llvm::Type *ty = lower(g->Ty);

  // Foreign globals keep the name the C side gave them. Rune globals are
  // mangled with their module so two modules may each have a `counter`.
  bool isForeign = g->Parent && isa<ExternDecl>(g->Parent);
  // `@as` renamed the Rune-side name only; the symbol stays what C exports.
  std::string name = g->LinkName.empty() ? g->Name : g->LinkName;
  if (!isForeign && !g->ModulePath.empty()) {
    std::string mod = g->ModulePath;
    for (char &ch : mod)
      if (!std::isalnum(static_cast<unsigned char>(ch)))
        ch = '_';
    name = "_RG" + mod + "V" + g->Name;
  }

  GlobalValue::LinkageTypes linkage =
      isForeign ? GlobalValue::ExternalLinkage
                : (g->IsPublic ? GlobalValue::WeakODRLinkage
                               : GlobalValue::InternalLinkage);
  Constant *folded = isForeign ? nullptr : constantInitialiserFor(g);
  // A `global var` with a known value starts as that value and stays
  // writable; a `let` is a constant.
  auto *gv = new GlobalVariable(*M, ty,
                                /*isConstant=*/folded != nullptr && !g->IsMutable,
                                linkage, nullptr, name);
  if (!isForeign && g->IsPublic)
    isAncillary(g) ? setDiscardableLinkage(gv, name)
                   : setMergeableLinkage(gv, name);
  // A global that is already worth something is emitted worth it, so reading
  // one costs a load of a constant rather than a load of whatever the
  // initialiser got round to storing. The rest start zeroed and are filled in
  // by the generated initialiser.
  if (folded)
    gv->setInitializer(folded);
  else if (!isForeign)
    gv->setInitializer(Constant::getNullValue(ty));
  Globals[g] = gv;
  g->CodeGenGlobal = gv;
  return gv;
}

Function *CodeGen::emitClassDeinit(ClassDecl *c) {
  auto it = ClassDeinits.find(static_cast<NominalDecl *>(c));
  if (it != ClassDeinits.end())
    return it->second;

  // Deinit, type info and vtables use one-definition linkage with a fully
  // qualified name so a class shared through a .rul resolves to a single
  // object at link time; otherwise `is` checks would disagree across the
  // library boundary.
  std::string symbol = typeSymbolFor(static_cast<NominalDecl *>(c));
  auto *ft = FunctionType::get(B->getVoidTy(), {PtrTy}, false);
  auto *f = Function::Create(ft, GlobalValue::LinkOnceODRLinkage,
                             "rune.deinit." + symbol, *M);
  ClassDeinits[static_cast<NominalDecl *>(c)] = f;

  // Build the body after the class layout exists; it releases every
  // reference-counted field and then chains to the user's `deinit`.
  auto *saveBB = B->GetInsertBlock();
  auto saveIt = saveBB ? B->GetInsertPoint() : BasicBlock::iterator();

  // A generated function still needs a FunctionState: releasing an enum field
  // allocates a scratch slot and creates blocks, both of which look it up.
  FunctionState st;
  st.Fn = f;
  st.ReturnType = Types.voidType();
  FnStack.push_back(st);
  fs().Scopes.push_back(LexicalScope{});

  auto *entry = BasicBlock::Create(*Ctx, "entry", f);
  B->SetInsertPoint(entry);
  Value *self = f->getArg(0);

  // Destruction runs from the most-derived class inwards: this class's own
  // `deinit`, then its own fields, then the base class's deinit does the same
  // for its half of the instance.
  if (c->Deinit) {
    Function *userDeinit = declareFunction(c->Deinit);
    B->CreateCall(userDeinit, {self});
  }
  StructType *layout = layoutOf(static_cast<NominalDecl *>(c),
                                c->DeclaredType);
  for (const auto &field : c->Fields) {
    Value *fieldPtr = B->CreateStructGEP(layout, self, 1 + field->Index);
    if (field->IsWeak) {
      // Forget the slot so the runtime never writes into freed memory.
      B->CreateCall(runtimeFn("rune_weak_clear", B->getVoidTy(), {PtrTy}),
                    {fieldPtr});
      continue;
    }
    Type *ft2 = field->Ty;
    if (!needsDestruction(ft2))
      continue;
    // A field that is itself a value with a `deinit` is destroyed in place:
    // its destructor wants the address of the field, not a copy of it.
    emitDestroy(fieldPtr, ft2);
  }
  if (c->Super)
    B->CreateCall(emitClassDeinit(c->Super), {self});
  B->CreateRetVoid();

  fs().Scopes.pop_back();
  FnStack.pop_back();
  if (saveBB)
    B->SetInsertPoint(saveBB, saveIt);
  return f;
}

GlobalVariable *CodeGen::emitTypeInfo(NominalDecl *nd) {
  auto it = TypeInfos.find(nd);
  if (it != TypeInfos.end())
    return it->second;

  std::string symbol = typeSymbolFor(nd);
  std::string qualified = typeDisplayName(nd);

  // Reserve the slot first so a class that refers to itself terminates.
  auto *gv = new GlobalVariable(*M, TypeInfoTy, /*isConstant=*/true,
                                GlobalValue::LinkOnceODRLinkage, nullptr,
                                "rune.typeinfo." + symbol);
  TypeInfos[nd] = gv;

  Constant *nameConst = ConstantDataArray::getString(*Ctx, qualified, true);
  auto *nameGV = new GlobalVariable(*M, nameConst->getType(), true,
                                    GlobalValue::PrivateLinkage, nameConst,
                                    ".rune.typename");
  nameGV->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);

  auto *cls = dyn_cast<ClassDecl>(static_cast<Decl *>(nd));
  StructType *layout = layoutOf(nd, nd->DeclaredType);
  uint64_t size = layout->isSized()
                      ? M->getDataLayout().getTypeAllocSize(layout).getFixedValue()
                      : 0;

  Constant *deinitFn = ConstantPointerNull::get(PtrTy);
  Constant *cloneFn = ConstantPointerNull::get(PtrTy);
  Constant *superInfo = ConstantPointerNull::get(PtrTy);
  Constant *vtable = ConstantPointerNull::get(PtrTy);
  unsigned vtableCount = 0;

  if (cls) {
    deinitFn = emitClassDeinit(cls);
    // Single ownership copies an object through whatever names it — a base
    // class, a `dyn`, an `Any` — so the copy is found from the object.
    if (zombie() && !cls->Generics.size())
      if (Function *f = objectCloneFor(nd->DeclaredType))
        cloneFn = f;
    if (cls->Super)
      superInfo = emitTypeInfo(static_cast<NominalDecl *>(cls->Super));
    if (!cls->VTable.empty()) {
      std::vector<Constant *> entries;
      for (FunctionDecl *m : cls->VTable)
        // A method this instantiation's arguments do not qualify for has no
        // body, and Sema refuses every call that would reach the slot.
        entries.push_back(m->WhereUnmet ? static_cast<Constant *>(ConstantPointerNull::get(PtrTy))
                                        : declareFunction(m));
      auto *arrTy = ArrayType::get(PtrTy, entries.size());
      auto *vtGV = new GlobalVariable(*M, arrTy, true,
                                      GlobalValue::LinkOnceODRLinkage,
                                      ConstantArray::get(arrTy, entries),
                                      "rune.vtable." + symbol);
      vtable = vtGV;
      vtableCount = static_cast<unsigned>(entries.size());
    }
  }

  gv->setInitializer(ConstantStruct::get(
      TypeInfoTy, {nameGV, ConstantInt::get(B->getInt64Ty(), size), deinitFn,
                   superInfo, vtable,
                   ConstantInt::get(B->getInt32Ty(), vtableCount), cloneFn}));
  return gv;
}

Function *CodeGen::thunkFor(Function *target, Type *fnType) {
  auto it = ValueThunks.find(target);
  if (it != ValueThunks.end())
    return it->second;

  // A plain function used as a value gains an ignored environment parameter so
  // every callable value shares one calling convention.
  std::vector<llvm::Type *> params{PtrTy};
  for (llvm::Type *p : target->getFunctionType()->params())
    params.push_back(p);
  auto *ft = FunctionType::get(target->getReturnType(), params, false);
  auto *thunk = Function::Create(ft, GlobalValue::InternalLinkage,
                                 target->getName() + ".value", *M);
  ValueThunks[target] = thunk;

  auto *saveBB = B->GetInsertBlock();
  auto saveIt = saveBB ? B->GetInsertPoint() : BasicBlock::iterator();
  B->SetInsertPoint(BasicBlock::Create(*Ctx, "entry", thunk));
  std::vector<Value *> args;
  for (unsigned i = 1; i < thunk->arg_size(); ++i)
    args.push_back(thunk->getArg(i));
  Value *r = B->CreateCall(target, args);
  if (target->getReturnType()->isVoidTy())
    B->CreateRetVoid();
  else
    B->CreateRet(r);
  if (saveBB)
    B->SetInsertPoint(saveBB, saveIt);
  return thunk;
}

//===----------------------------------------------------------------------===//
// Function bodies
//===----------------------------------------------------------------------===//

void CodeGen::emitFunctionBody(FunctionDecl *fn) {
  if (fn->IsExtern || fn->IsImported)
    return;
  if (ReplacedDefinitions.count(fn))
    return;
  if (fn->Flavour == FunctionFlavour::Closure) {
    emitClosureBody(fn);
    return;
  }
  if (!fn->Body)
    return;

  Function *f = declareFunction(fn);
  if (!f->empty())
    return;

  FunctionState st;
  st.Decl = fn;
  st.Fn = f;
  st.ReturnType = fn->Ty ? fn->Ty->result() : Types.voidType();
  FnStack.push_back(st);

  auto *entry = BasicBlock::Create(*Ctx, "entry", f);
  B->SetInsertPoint(entry);
  // Everything emitted from here carries a location inside this subprogram.
  DIScopes.push_back(debugSubprogramFor(fn, f));
  setDebugLocation(fn->Range);

  bool isInit = fn->Flavour == FunctionFlavour::Initialiser;
  Type *ret = fs().ReturnType;
  if (!isInit && ret && !ret->isVoid() && !ret->isNever() && !ret->isError())
    fs().ReturnSlot = createEntryAlloca(lower(ret), "retval");
  if (fs().ReturnSlot && ret->isRefCounted())
    B->CreateStore(Constant::getNullValue(lower(ret)), fs().ReturnSlot);
  fs().ReturnBlock = BasicBlock::Create(*Ctx, "return", f);

  fs().Scopes.push_back(LexicalScope{});

  // Bind parameters. Arguments arrive at +0; the callee retains what it keeps.
  unsigned argIndex = 0;
  for (Param &p : fn->Params) {
    Value *arg = f->getArg(argIndex++);
    arg->setName(p.Name);
    if (p.IsSelf)
      fs().SelfValue = arg;
    if (!p.Binding)
      continue;
    Value *slot = createEntryAlloca(lower(p.Ty), p.Name);
    B->CreateStore(arg, slot);
    fs().Slots[p.Binding] = slot;
    // `self` is borrowed for the duration of the call, so it is not retained
    // and must not be released on the way out. Under Zombie a `self` taken
    // by value (not `init`, whose object the caller keeps) was moved in, and
    // the method owns it like any other parameter — except `deinit`, whose
    // `self` is the object already being torn down: dropping it here would
    // destroy it a second time, so it stays borrowed like `init`'s.
    bool owns = !p.IsSelf;
    if (p.IsSelf && zombie() && !p.SelfByRef &&
        fn->Flavour != FunctionFlavour::Initialiser &&
        fn->Flavour != FunctionFlavour::Deinitialiser)
      owns = true;
    // A class's `&self` is the handle itself, borrowed for the call. Under
    // Zombie anything that copies it — a closure capturing `self` — takes a
    // second handle to an object it does not own, and must not drop it.
    // Saying the binding is an alias is what tells the capture so.
    if (p.IsSelf && zombie() && p.SelfByRef && p.Ty &&
        !p.Ty->is(TypeKind::Pointer))
      p.Binding->ZombieAlias = true;
    if (owns) {
      emitRetain(arg, p.Ty);
      // A parameter owns what it was passed, so it destroys it on the way
      // out — unless the body hands it on, which is what the flag says.
      // It starts true: an argument arrives already made.
      Value *liveFlag = nullptr;
      if ((p.Binding->MovedSomewhere || (zombie() && p.Binding->ZombieMoved)) &&
          hasValueDeinit(p.Ty)) {
        liveFlag = createEntryAlloca(B->getInt1Ty(), p.Name + ".owns");
        B->CreateStore(B->getTrue(), liveFlag);
        fs().LiveFlags[p.Binding] = liveFlag;
      }
      fs().Scopes.back().Locals.push_back({slot, p.Ty, liveFlag});
    }
    declareDebugVariable(p.Binding, slot, argIndex);
  }

  emitBlock(fn->Body.get(), isInit ? nullptr : fs().ReturnSlot,
            isInit ? nullptr : ret);

  if (!blockIsTerminated()) {
    emitStatementCleanup();
    emitAllScopeCleanups(0);
    B->CreateBr(fs().ReturnBlock);
  }
  fs().Scopes.pop_back();
  if (!DIScopes.empty())
    DIScopes.pop_back();

  B->SetInsertPoint(fs().ReturnBlock);
  if (fs().ReturnSlot)
    B->CreateRet(B->CreateLoad(lower(ret), fs().ReturnSlot));
  else if (f->getReturnType()->isVoidTy())
    B->CreateRetVoid();
  else
    B->CreateRet(Constant::getNullValue(f->getReturnType()));

  FnStack.pop_back();
}

void CodeGen::emitClosureBody(FunctionDecl *lifted) {
  ClosureExpr *c = lifted->SourceClosure;
  if (!c || !c->Body)
    return;
  Function *f = declareFunction(lifted);
  if (!f->empty())
    return;

  FunctionState st;
  st.Decl = lifted;
  st.Closure = c;
  st.Fn = f;
  st.ReturnType = lifted->Ty ? lifted->Ty->result() : Types.voidType();
  FnStack.push_back(st);

  B->SetInsertPoint(BasicBlock::Create(*Ctx, "entry", f));
  fs().EnvValue = f->getArg(0);
  fs().EnvValue->setName("env");

  Type *ret = fs().ReturnType;
  if (ret && !ret->isVoid() && !ret->isNever() && !ret->isError()) {
    fs().ReturnSlot = createEntryAlloca(lower(ret), "retval");
    if (ret->isRefCounted())
      B->CreateStore(Constant::getNullValue(lower(ret)), fs().ReturnSlot);
  }
  fs().ReturnBlock = BasicBlock::Create(*Ctx, "return", f);
  fs().Scopes.push_back(LexicalScope{});

  for (unsigned i = 0; i < c->Captures.size(); ++i)
    fs().CaptureIndex[c->Captures[i].Var] = i;
  // A captured `self` is reachable through the environment as well.
  if (FnStack.size() > 1)
    fs().SelfValue = nullptr;

  unsigned argIndex = 1; // 0 is the environment
  for (Param &p : c->Params) {
    Value *arg = f->getArg(argIndex++);
    arg->setName(p.Name);
    if (!p.Binding)
      continue;
    Value *slot = createEntryAlloca(lower(p.Ty), p.Name);
    B->CreateStore(arg, slot);
    emitRetain(arg, p.Ty);
    fs().Slots[p.Binding] = slot;
    Value *liveFlag = nullptr;
    if ((p.Binding->MovedSomewhere || (zombie() && p.Binding->ZombieMoved)) &&
        hasValueDeinit(p.Ty)) {
      liveFlag = createEntryAlloca(B->getInt1Ty(), p.Name + ".owns");
      B->CreateStore(B->getTrue(), liveFlag);
      fs().LiveFlags[p.Binding] = liveFlag;
    }
    fs().Scopes.back().Locals.push_back({slot, p.Ty, liveFlag});
  }

  emitBlock(c->Body.get(), fs().ReturnSlot, ret);
  if (!blockIsTerminated()) {
    emitStatementCleanup();
    emitAllScopeCleanups(0);
    B->CreateBr(fs().ReturnBlock);
  }
  fs().Scopes.pop_back();

  B->SetInsertPoint(fs().ReturnBlock);
  if (fs().ReturnSlot)
    B->CreateRet(B->CreateLoad(lower(ret), fs().ReturnSlot));
  else
    B->CreateRetVoid();

  FnStack.pop_back();
}

//===----------------------------------------------------------------------===//
// Statements
//===----------------------------------------------------------------------===//

void CodeGen::emitBlock(BlockExpr *b, Value *resultSlot, Type *resultType) {
  if (!b)
    return;
  // Statements inside this block own their own temporaries. The enclosing
  // statement's are set aside first: a block used as an operand — `"n = " +
  // unsafe { compute() }.$str()` — would otherwise release the values the
  // expression around it is still holding.
  TempScope ownTemps(*this);
  fs().Scopes.push_back(LexicalScope{});
  size_t depth = fs().Scopes.size();

  for (auto &s : b->Stmts) {
    if (blockIsTerminated())
      break;
    emitStmt(s.get());
  }

  if (!blockIsTerminated()) {
    if (b->Tail) {
      if (resultSlot && resultType && !resultType->isVoid()) {
        emitInto(b->Tail.get(), resultSlot, resultType);
      } else {
        emitRValue(b->Tail.get());
      }
      emitStatementCleanup();
    }
    emitScopeCleanup(depth - 1);
  }
  fs().Scopes.pop_back();
}

void CodeGen::emitStmt(Stmt *s) {
  if (!s)
    return;
  setDebugLocation(s->Range);
  switch (s->Kind) {
  case NodeKind::ExprStmt: {
    Expr *value = cast<ExprStmt>(s)->Value.get();
    Value *produced = emitRValue(value);
    // A statement's value is thrown away. When it owns something — a struct
    // with a `deinit`, made here and bound to nothing — this is the only
    // place it can be destroyed, so destroy it. Anything that names a binding
    // is left alone: the binding still owns it.
    if (produced && value->Ty && hasValueDeinit(value->Ty) &&
        value->Category == ValueCategory::RValue && !blockIsTerminated())
      emitDestroyValue(produced, value->Ty);
    emitStatementCleanup();
    break;
  }

  case NodeKind::VarStmt: {
    auto *v = cast<VarStmtNode>(s);
    if (v->IsGlobal) {
      // The storage lives at module scope; a `global` statement only makes the
      // name visible, and the initialiser already ran in rune.init_globals.
      break;
    }
    Type *ty = v->Binding ? v->Binding->Ty : nullptr;
    if (!ty)
      break;
    if (auto *bp = dyn_cast<BindingPattern>(v->Binding.get())) {
      if (!bp->Binding)
        break;
      Value *slot = declareLocalSlot(bp->Binding, bp->Name);
      // A local that never leaves this scope, initialised by a construction
      // nothing else holds: the object's own count is the binding's, so the
      // slot adopts it rather than retaining a second reference and dropping
      // the first. The scope hands it back on the way out exactly as before.
      Expr *savedAdopt = AdoptedResult;
      if (adoptsFreshConstruction(bp->Binding, v->Init.get()))
        AdoptedResult = v->Init.get();
      if (v->Init)
        emitInto(v->Init.get(), slot, ty);
      AdoptedResult = savedAdopt;
      // The slot holds a value now, so it owns one. (The flag starts false so
      // that a `return` before the initialiser — which only a loop can
      // arrange — does not destroy uninitialised storage.)
      if (Value *flag = liveFlagFor(bp->Binding))
        B->CreateStore(B->getTrue(), flag);
      emitStatementCleanup();
      break;
    }
    // Destructuring: evaluate once into a temporary, then bind each part.
    Value *tmp = createEntryAlloca(lower(ty), "destructure");
    if (ty->isRefCounted())
      B->CreateStore(Constant::getNullValue(lower(ty)), tmp);
    if (v->Init)
      emitInto(v->Init.get(), tmp, ty);
    fs().Scopes.back().Locals.push_back({tmp, ty});
    emitPatternBind(v->Binding.get(), tmp, ty);
    emitStatementCleanup();
    break;
  }

  case NodeKind::DeferStmt:
    fs().Scopes.back().Deferred.push_back(cast<DeferStmtNode>(s)->Body.get());
    break;

  case NodeKind::DeclStmtKind:
    // Nested functions are emitted from the module-level worklist.
    break;

  default:
    break;
  }
}

//===----------------------------------------------------------------------===//
// Module assembly
//===----------------------------------------------------------------------===//

void CodeGen::emitGlobalInitialisers() {
  auto *ft = FunctionType::get(B->getVoidTy(), {}, false);
  auto *f = Function::Create(ft, GlobalValue::InternalLinkage,
                             "rune.init_globals", *M);
  FunctionState st;
  st.Fn = f;
  st.ReturnType = Types.voidType();
  FnStack.push_back(st);
  B->SetInsertPoint(BasicBlock::Create(*Ctx, "entry", f));
  fs().Scopes.push_back(LexicalScope{});

  // A freestanding program has the standard library's code only where it
  // reaches it, and its globals the same way: `std::io`'s would need the
  // hosted runtime just to exist. Those are initialised first, once the
  // program's code is emitted and it is known which of them it reads.
  if (Opts.Freestanding) {
    BorrowedGlobalsInit = Function::Create(
        ft, GlobalValue::InternalLinkage, "rune.init_borrowed_globals", *M);
    B->CreateCall(BorrowedGlobalsInit);
  }

  for (GlobalVarDecl *g : Sema.Globals) {
    if (g->Parent && isa<ExternDecl>(g->Parent))
      continue;
    GlobalVariable *gv = declareGlobal(g);
    if (!g->Init)
      continue;
    if (Opts.Freestanding && isAncillary(g)) {
      DeferredGlobals.push_back(g);
      continue;
    }
    // Already in the object file; there is nothing to run for it. That
    // includes every global whose value is all zeros — a buffer, a counter,
    // `[0; 65536]` — which is where the zeroed storage already starts: storing
    // it again would cost start-up time, and a program with no `main` would
    // have to call `rune_init` for nothing.
    if (constantInitialiserFor(g) || isZeroInitialiser(g->Init.get(), g->Ty))
      continue;
    emitInto(g->Init.get(), gv, g->Ty);
    emitStatementCleanup();
  }

  fs().Scopes.pop_back();
  B->CreateRetVoid();
  FnStack.pop_back();
}

void CodeGen::emitDecoratorCalls() {
  auto *ft = FunctionType::get(B->getVoidTy(), {}, false);
  auto *f = Function::Create(ft, GlobalValue::InternalLinkage,
                             "rune.run_decorators", *M);
  FunctionState st;
  st.Fn = f;
  st.ReturnType = Types.voidType();
  FnStack.push_back(st);
  B->SetInsertPoint(BasicBlock::Create(*Ctx, "entry", f));
  fs().Scopes.push_back(LexicalScope{});

  // `@route("/health")` on `fn health()` is a call to `route("/health",
  // health)`, made once, in the order the decorators were written.
  for (const auto &use : Sema.DecoratorCalls) {
    if (!use.Decorator || !use.Target || !use.Args)
      continue;
    Function *dec = declareFunction(use.Decorator);
    const std::vector<Type *> &params = use.Decorator->Ty->params();
    std::vector<Value *> args;
    for (size_t i = 0; i + 1 < params.size() && i < use.Args->Args.size(); ++i) {
      Value *v = emitRValue(use.Args->Args[i].get());
      if (!v)
        v = Constant::getNullValue(lower(params[i]));
      args.push_back(coerce(v, use.Args->Args[i]->Ty, params[i]));
    }
    // The decorated function as a value. Every callable value carries a
    // leading environment parameter, so the *thunk* goes in the slot — not
    // the function itself, whose first parameter is its own. Passing the raw
    // address here shifted every argument by one, and the first one arrived
    // as the null environment.
    Value *closure = UndefValue::get(lower(params.back()));
    closure = B->CreateInsertValue(
        closure, thunkFor(declareFunction(use.Target), params.back()), 0);
    closure = B->CreateInsertValue(closure,
                                   ConstantPointerNull::get(PtrTy), 1);
    args.push_back(closure);
    B->CreateCall(dec, args);
    emitStatementCleanup();
  }

  fs().Scopes.pop_back();
  B->CreateRetVoid();
  FnStack.pop_back();
}

void CodeGen::emitGlobalTeardown() {
  auto *ft = FunctionType::get(B->getVoidTy(), {}, false);
  auto *f = Function::Create(ft, GlobalValue::InternalLinkage,
                             "rune.deinit_globals", *M);
  FunctionState st;
  st.Fn = f;
  st.ReturnType = Types.voidType();
  FnStack.push_back(st);
  B->SetInsertPoint(BasicBlock::Create(*Ctx, "entry", f));
  fs().Scopes.push_back(LexicalScope{});

  // Reverse declaration order, mirroring how locals unwind.
  for (auto it = Sema.Globals.rbegin(); it != Sema.Globals.rend(); ++it) {
    GlobalVarDecl *g = *it;
    if (g->Parent && isa<ExternDecl>(g->Parent))
      continue;
    if (!g->Ty || !g->Ty->isRefCounted())
      continue;
    // A freestanding program's borrowed globals exist only where it reads
    // them, and a program that stops by switching the machine off never
    // gets here; releasing one would be what brought it in.
    if (Opts.Freestanding && isAncillary(g))
      continue;
    GlobalVariable *gv = declareGlobal(g);
    emitRelease(B->CreateLoad(lower(g->Ty), gv), g->Ty);
    B->CreateStore(Constant::getNullValue(lower(g->Ty)), gv);
  }

  fs().Scopes.pop_back();
  B->CreateRetVoid();
  FnStack.pop_back();
}

void CodeGen::emitEntryPoint() {
  // A library has no entry point, and an object or IR dump may legitimately
  // lack one; only a finished executable must have `main`.
  if (Opts.Output == OutputKind::Library)
    return;

  // `@entry(none)`: the program starts wherever it says — a boot stub's
  // `call`, a reset vector. What `main` would have done first is handed to
  // it as one function, `rune_init`, to call before touching a global whose
  // value is worked out at run time. A global whose value is a constant is
  // in the image already and needs nothing.
  if (Opts.NoEntry) {
    // The program names it in an `extern "C"` block to call it, so a
    // declaration may be here already; this is its body.
    auto *ft = FunctionType::get(B->getVoidTy(), {}, false);
    Function *init = M->getFunction("rune_init");
    if (init && (!init->isDeclaration() || init->getFunctionType() != ft)) {
      Diags.error(SourceRange(), "`rune_init` is the compiler's under "
                                 "`@entry(none)`")
          .note("declare it as `fn rune_init()` in an `extern \"C\"` block, "
                "and define nothing by that name")
          .code(501);
      return;
    }
    if (!init)
      init = Function::Create(ft, GlobalValue::ExternalLinkage, "rune_init",
                              *M);
    if (Opts.Freestanding)
      init->addFnAttr("no-builtins");
    B->SetInsertPoint(BasicBlock::Create(*Ctx, "entry", init));
    B->CreateCall(M->getFunction("rune.init_globals"));
    if (llvm::Function *decorators = M->getFunction("rune.run_decorators"))
      B->CreateCall(decorators);
    B->CreateRetVoid();
    return;
  }

  FunctionDecl *userMain = Sema.EntryPoint;
  if (!userMain) {
    if (Opts.Output != OutputKind::Executable)
      return;
    Diags.fatal("no `main` function found")
        .note("a program needs `fn main() -> i64` (or `fn main()`) at the top "
              "level of its root module");
    return;
  }

  auto *ft = FunctionType::get(B->getInt32Ty(), {B->getInt32Ty(), PtrTy}, false);
  // WebAssembly has no variadic-by-convention `main`: wasi-libc's start code
  // calls `__main_argc_argv` when the program wants its arguments, which is
  // what clang renames a C `main(argc, argv)` to on that target.
  const char *entryName =
      llvm::Triple(M->getTargetTriple()).isWasm() ? "__main_argc_argv" : "main";
  auto *f = Function::Create(ft, GlobalValue::ExternalLinkage, entryName, *M);
  auto *entry = BasicBlock::Create(*Ctx, "entry", f);
  B->SetInsertPoint(entry);

  // A hosted runtime keeps the arguments and sets up what it needs; a
  // freestanding one has nothing to set up.
  if (!Opts.Freestanding)
    B->CreateCall(runtimeFn("rune_runtime_init", B->getVoidTy(),
                            {B->getInt32Ty(), PtrTy}),
                  {f->getArg(0), f->getArg(1)});
  B->CreateCall(M->getFunction("rune.init_globals"));
  // Decorators run after globals exist and before `main` is entered, so a
  // registry a decorator fills is ready by the time anything reads it.
  if (llvm::Function *decorators = M->getFunction("rune.run_decorators"))
    B->CreateCall(decorators);

  Function *mainFn = declareFunction(userMain);
  Value *result = B->CreateCall(mainFn);
  Type *ret = userMain->Ty ? userMain->Ty->result() : Types.voidType();

  Value *code = B->getInt32(0);
  if (ret && ret->isInt())
    code = B->CreateSExtOrTrunc(result, B->getInt32Ty());

  // Globals live for the whole program, so they are released here rather than
  // at any scope exit — before the leak report, so they are not counted.
  if (llvm::Function *teardown = M->getFunction("rune.deinit_globals"))
    B->CreateCall(teardown);
  // `full` refuses the shapes that leak, so the report is mostly for the
  // levels that allow them: a program working below `full` still wants to
  // hear about what it left behind. Only `none` opts out entirely.
  // A freestanding program has nowhere to report to; its heap is its own.
  if (Opts.Safety != SafetyLevel::None && !Opts.Freestanding)
    B->CreateCall(runtimeFn("rune_report_leaks", B->getVoidTy(), {}));
  B->CreateRet(code);
}

/// What a hosted-runtime entry point is part of, said the way a program's
/// author would look for it.
static const char *hostedFeatureOf(llvm::StringRef name) {
  if (name.starts_with("rune_read_") || name.starts_with("rune_stdin_"))
    return "standard input, which needs an operating system to read from";
  if (name.starts_with("rune_ucd_"))
    return "the Unicode tables `std::text` folds, composes and collates with";
  if (name.starts_with("rune_dir_") || name.starts_with("rune_path_") ||
      name == "rune_last_file_error")
    return "files, which need an operating system";
  if (name.starts_with("rune_task_") || name.starts_with("rune_pool_"))
    return "`std::task`, which needs threads and an event loop";
  if (name.starts_with("rune_thread_") || name.starts_with("rune_mutex_") ||
      name.starts_with("rune_cond_"))
    return "`std::thread`, which needs an operating system's threads";
  if (name.starts_with("rune_retain") || name.starts_with("rune_release") ||
      name.starts_with("rune_weak_"))
    return "reference counting; a freestanding program is built with "
           "`--memory zombie`, where nothing is counted";
  if (name.starts_with("rune_net_") || name.starts_with("rune_command_") ||
      name.starts_with("rune_env_") || name.starts_with("rune_file_"))
    return "the operating system's services, through the hosted runtime";
  return "the hosted runtime";
}

/// What `freestanding_type = "minimal"` leaves out of the freestanding
/// runtime, by symbol; null for what the minimal runtime keeps, or what no
/// freestanding runtime has. Kept in step with the `@Config` marks in
/// runetime/.
static const char *minimalLeavesOut(llvm::StringRef name) {
  if (name.starts_with("rune_string_from_f64"))
    return "floats as text";
  if (name == "rune_string_to_i64" || name == "rune_string_to_f64")
    return "text as numbers (`$toInt`, `$toFloat`)";
  if (name == "rune_string_substring" || name == "rune_string_repeat" ||
      name == "rune_string_find" || name.starts_with("rune_string_char_") ||
      name == "rune_string_from_char" || name == "rune_string_from_ptr")
    return "taking strings apart: `$substring`, `$repeat`, `$find`, and "
           "characters";
  if (name == "rune_clone_object")
    return "`$clone()` of a class object";
  if (name == "rune_is_kind_of" || name == "rune_any_is")
    return "`Any`, and testing what class an object is";
  if (name == "rune_hash_mix" || name == "rune_string_hash" ||
      name == "rune_cstring_hash")
    return "hashing";
  return nullptr;
}

/// A freestanding program links nothing but itself and the Rune-written
/// runtime compiled in with it, so a call into the hosted runtime would be an
/// undefined symbol at link time — reported by the linker, in the linker's
/// terms, far from its cause. Here it is reported against the function that
/// makes it.
void CodeGen::reportHostedRuntimeUses() {
  bool minimal = false;
  for (const auto &kv : Opts.ConfigValues)
    if (kv.first == "freestanding_type")
      minimal = kv.second == "minimal";
  std::map<const llvm::Function *, FunctionDecl *> declOf;
  for (auto &[decl, f] : Functions)
    declOf[f] = decl;
  // Each function is reported once, for the first thing it needs; fixing
  // that usually fixes the rest.
  std::vector<std::pair<FunctionDecl *, std::string>> needs;
  std::set<FunctionDecl *> seen;
  for (llvm::Function &callee : *M) {
    if (!callee.isDeclaration() || callee.isIntrinsic() || callee.use_empty() ||
        !callee.getName().starts_with("rune_"))
      continue;
    std::string generated; // what the compiler wrote itself, by name
    bool attributed = false;
    for (llvm::User *u : callee.users())
      if (auto *inst = dyn_cast<llvm::Instruction>(u)) {
        auto it = declOf.find(inst->getFunction());
        if (it == declOf.end()) {
          generated = inst->getFunction()->getName().str();
          continue;
        }
        attributed = true;
        // Inside the standard library, the function to point at is the
        // program's own that called into it: `io::println` needs the hosted
        // runtime, but `greet` is what to change.
        std::vector<FunctionDecl *> blamed;
        std::set<const llvm::Function *> visited;
        std::vector<const llvm::Function *> work = {inst->getFunction()};
        while (!work.empty()) {
          const llvm::Function *f = work.back();
          work.pop_back();
          if (!visited.insert(f).second)
            continue;
          auto d = declOf.find(f);
          if (d != declOf.end() && !isAncillary(d->second)) {
            blamed.push_back(d->second);
            continue;
          }
          for (const llvm::User *fu : f->users())
            if (auto *ci = dyn_cast<llvm::Instruction>(fu))
              work.push_back(ci->getFunction());
        }
        if (blamed.empty())
          blamed.push_back(it->second);
        for (FunctionDecl *fn : blamed)
          if (seen.insert(fn).second)
            needs.push_back({fn, callee.getName().str()});
      }
    if (attributed)
      continue;
    if (const char *left = minimal ? minimalLeavesOut(callee.getName()) : nullptr) {
      auto d = Diags.error(SourceRange(), "this program needs '{}', which the "
                                          "minimal freestanding runtime leaves "
                                          "out", callee.getName().str());
      d.note("it is {}", left);
      if (!generated.empty())
        d.note("it is called from '{}', which the compiler generated",
               generated);
      d.note("`freestanding_type = \"full\"` (the default) has it");
      d.code(542);
      continue;
    }
    auto d = Diags.error(SourceRange(), "this program needs '{}' from the "
                                        "hosted runtime, and is built "
                                        "without one",
                         callee.getName().str());
    d.note("{}", hostedFeatureOf(callee.getName()));
    if (!generated.empty())
      d.note("it is called from '{}', which the compiler generated",
             generated);
    d.code(542);
  }
  // One report per function and per thing it needs: `hashing` is three
  // symbols, and a generic instantiated twice is two declarations at one
  // place.
  std::set<std::pair<uint32_t, std::string>> said;
  for (const auto &[fn, symbol] : needs) {
    const char *left = minimal ? minimalLeavesOut(symbol) : nullptr;
    std::string what = left ? left : hostedFeatureOf(symbol);
    if (!said.insert({fn->NameRange.begin().raw(), what}).second)
      continue;
    if (left) {
      Diags.error(fn->NameRange, "'{}' needs more of the runtime than "
                                 "`freestanding_type = \"minimal\"` keeps",
                  fn->Name)
          .note("it uses {}, which the minimal freestanding runtime leaves out",
                left)
          .note("`freestanding_type = \"full\"` (the default) has it")
          .code(542);
      continue;
    }
    Diags.error(fn->NameRange, "'{}' needs the hosted runtime, and this "
                               "program is built without one",
                fn->Name)
        .note("it uses {}", hostedFeatureOf(symbol))
        .note("`@runtime(none)` links only the program and the freestanding "
              "runtime compiled with it")
        .code(542);
  }
}

/// Two definitions of one symbol in this module: a `@weak` default and the
/// definition that replaces it — the freestanding runtime's panic handler and
/// the program's own, compiled together. Across objects the linker would pick
/// the strong one; within a module that choice is made here, and the default
/// is never emitted.
void CodeGen::resolveWeakDefinitions() {
  std::map<llvm::Function *, std::vector<FunctionDecl *>> bySymbol;
  for (FunctionDecl *fn : Sema.Functions) {
    if (fn->IsExtern || fn->IsImported || !fn->Body || isAncillary(fn))
      continue;
    auto it = Functions.find(fn);
    if (it != Functions.end())
      bySymbol[it->second].push_back(fn);
  }
  for (auto &[f, decls] : bySymbol) {
    // One symbol, several declarations, and no `@weak` among them is the
    // ordinary case of a generic instantiated twice: one function.
    if (decls.size() < 2 ||
        std::none_of(decls.begin(), decls.end(), [](FunctionDecl *fn) {
          return fn->hasAttr("weak");
        }))
      continue;
    FunctionDecl *winner = nullptr;
    for (FunctionDecl *fn : decls)
      if (!fn->hasAttr("weak")) {
        if (winner) {
          auto d = Diags.error(fn->NameRange, "'{}' is defined twice",
                               f->getName().str());
          d.note("mark the default `@weak` so another definition can replace "
                 "it")
              .code(501);
          continue;
        }
        winner = fn;
      }
    if (!winner)
      winner = decls.front();
    else
      f->setLinkage(GlobalValue::ExternalLinkage);
    for (FunctionDecl *fn : decls)
      if (fn != winner)
        ReplacedDefinitions.insert(fn);
  }
}

/// Borrowed code — the standard library, an imported library's generics —
/// gets a body only where this module turns out to refer to it. See `run`.
void CodeGen::emitReachedBodies() {
  std::set<FunctionDecl *> &offered = OfferedBodies;
  for (bool more = true; more;) {
    more = false;
    for (FunctionDecl *fn : Sema.Functions) {
      if (!isAncillary(fn) || fn->hasAttr("intrinsic"))
        continue;
      auto it = Functions.find(fn);
      if (it == Functions.end() || !it->second->isDeclaration() ||
          it->second->use_empty())
        continue;
      if (!offered.insert(fn).second)
        continue;
      emitFunctionBody(fn);
      more = true;
    }
  }
}

/// The standard library's globals that a freestanding program reads, and only
/// those, initialised before its own. An initialiser can reach code and
/// globals of its own, so this runs until nothing new turns up.
void CodeGen::emitBorrowedGlobalInitialisers() {
  FunctionState st;
  st.Fn = BorrowedGlobalsInit;
  st.ReturnType = Types.voidType();
  FnStack.push_back(st);
  B->SetInsertPoint(BasicBlock::Create(*Ctx, "entry", BorrowedGlobalsInit));
  fs().Scopes.push_back(LexicalScope{});
  std::set<GlobalVarDecl *> done;
  for (bool more = true; more;) {
    more = false;
    for (GlobalVarDecl *g : DeferredGlobals) {
      GlobalVariable *gv = declareGlobal(g);
      if (done.count(g) || gv->use_empty())
        continue;
      done.insert(g);
      more = true;
      if (constantInitialiserFor(g) || isZeroInitialiser(g->Init.get(), g->Ty))
        continue;
      emitInto(g->Init.get(), gv, g->Ty);
      emitStatementCleanup();
    }
    if (more) {
      // Emitting bodies moves the builder; come back to this function after.
      BasicBlock *here = B->GetInsertBlock();
      emitReachedBodies();
      B->SetInsertPoint(here);
    }
  }
  fs().Scopes.pop_back();
  B->CreateRetVoid();
  FnStack.pop_back();
}

bool CodeGen::run() {
  // Layouts and type metadata for this artefact's own types first, so calls
  // into them are already typed. Borrowed types — the standard library's, an
  // imported library's — are laid out and given their descriptors the first
  // time something here reaches them, which for most programs is a small
  // fraction of what the library declares; declaring all of it up front was
  // most of what a hello world spent in the code generator.
  for (NominalDecl *nd : Sema.Nominals) {
    if (!nd->DeclaredType || isAncillary(nd))
      continue;
    lower(nd->DeclaredType);
    if (isa<ClassDecl>(static_cast<Decl *>(nd)))
      emitTypeInfo(nd);
  }

  for (GlobalVarDecl *g : Sema.Globals)
    declareGlobal(g);
  // Functions likewise: a declaration is made where a call, a vtable or a
  // global first names one, and the artefact's own bodies are emitted below.
  for (FunctionDecl *fn : Sema.Functions)
    if (!isAncillary(fn))
      declareFunction(fn);
  resolveWeakDefinitions();

  // The runtime prints a traceback only for a build that carries debug
  // information; this is how it finds out.
  {
    // Every Rune object defines this, and the runtime reads it from whichever
    // one the linker keeps. On COFF a `weak_odr` definition becomes a weak
    // external, which does not satisfy the runtime's reference; a COMDAT with
    // strong linkage merges the duplicates and resolves. Mach-O has no
    // COMDATs, and its weak definitions resolve fine.
    auto *flag = new GlobalVariable(
        *M, B->getInt32Ty(), /*isConstant=*/true, GlobalValue::WeakODRLinkage,
        ConstantInt::get(B->getInt32Ty(), Opts.DebugInfo ? 1 : 0),
        "rune_debug_build");
    setMergeableLinkage(flag, "rune_debug_build");
  }

  initDebugInfo();

  emitGlobalInitialisers();
  emitDecoratorCalls();
  emitGlobalTeardown();

  // This artefact's own code, all of it: what it exports is not for this pass
  // to second-guess, and a `.rul` has to carry every public thing it declares
  // whether or not anything inside it calls them.
  for (FunctionDecl *fn : Sema.Functions)
    if (!fn->hasAttr("intrinsic") && !isAncillary(fn))
      emitFunctionBody(fn);
  // `--tiers` asks about all of the standard library, so all of it is built.
  // Whatever building it all eagerly turns up is not this report's to say.
  if (Opts.TierReport) {
    Diags.beginSpeculation();
    for (FunctionDecl *fn : Sema.Functions)
      if (!fn->hasAttr("intrinsic") && isAncillary(fn) && !fn->WhereUnmet)
        emitFunctionBody(fn);
    Diags.endSpeculation();
  }

  emitEntryPoint();

  // Borrowed code — the standard library, an imported library's generics —
  // gets a body only where this module turns out to refer to it. The module
  // itself is what says so: a function left standing as a bodyless
  // declaration with users is one something here reached, whether through a
  // call, a vtable slot, a global's initialiser or a thunk. Emitting a body
  // can reach further, so this runs to a fixed point.
  //
  // Asking the IR rather than walking the AST is the point: every way one
  // function can name another ends up as a use, so no list of reference kinds
  // has to be kept correct as the code generator grows.
  //
  // Each candidate is offered once and no more. Not everything asked for has
  // a body to give — a generic template has none until it is instantiated, a
  // mark's requirement may have none at all — and a loop that judged by the
  // result would offer those again forever.
  emitReachedBodies();
  if (Opts.Freestanding)
    emitBorrowedGlobalInitialisers();
  if (Opts.TierReport) {
    reportTiers();
    return true;
  }

  // Documentation links nothing, and is built without the freestanding
  // runtime compiled in — every runtime call would look like a hosted one.
  if (Opts.Freestanding && Opts.Output != OutputKind::Library &&
      Opts.Output != OutputKind::Docs)
    reportHostedRuntimeUses();

  finishDebugInfo();

  if (Diags.hadError())
    return false;

  exportCAdapters();
  if (Opts.Freestanding && Opts.Output == OutputKind::Executable &&
      !Opts.LinksRuneLibraries)
    releaseUnusedRuntime();

  // Pruning first, and verifying what is left: the module that goes to the
  // back end is the one worth checking, and there is a great deal more of it
  // before the prune than after.
  pruneUnreachable();

  std::string err;
  raw_string_ostream os(err);
  if (verifyModule(*M, &os)) {
    os.flush();
    Diags.fatal("internal error: generated IR did not verify")
        .note("this is a compiler bug, not a problem with your program")
        .note(err.c_str());
    return false;
  }
  if (Opts.OptLevel)
    optimizeModule(*M, Opts.OptLevel);
  return true;
}

/// Throws away what this artefact does not reach.
///
/// The emission loop above already declines to lower most of what it will not
/// need, but it decides by asking whether anything refers to a function, and
/// something may refer to a function that is itself about to go. A type's
/// metadata names a `deinit` for a type nothing ends up constructing; a vtable
/// is built for a mark object the last use of which was in a function that
/// was not emitted either. This settles all of that at once, over the finished
/// module, where the answer is no longer a moving target.
///
/// Only definitions marked discardable are candidates: this artefact's own
/// code, its exports and anything foreign keep linkage that says "somebody
/// outside may want this", and are roots of the walk rather than casualties
/// of it.
///
/// Nothing is lost. A dropped definition is one no call, no vtable, no
/// initialiser and no metadata in this module mentions, and another artefact
/// that wants it carries — or merges in — its own copy.
/// A freestanding executable carries the freestanding runtime, and every
/// entry point in it is exported: a library compiled on its own calls them by
/// name. When no such library is linked, this module is every caller there
/// is, so an entry point it never calls — `rune_any_is` in a kernel with no
/// `Any` — is made discardable and goes with the rest of the unreached code.
///
/// Not the C library's names, nor the 64-bit division helpers: the back end
/// calls those itself, after the IR is final, for a large copy or a `/` the
/// processor cannot do.
/// An `@export`ed function is called from C, by C's rules. Where those
/// differ from how Rune passes the same signature — a struct by value — the
/// exported symbol is an adapter that takes its arguments the C way and makes
/// the Rune call; the Rune body keeps an internal name, and Rune's own calls
/// go straight to it. `@cfunction` pointers handed to C get the same adapter.
void CodeGen::exportCAdapters() {
  std::vector<std::pair<FunctionDecl *, llvm::Function *>> exported;
  for (auto &[decl, f] : Functions)
    if (f && !f->isDeclaration() && !decl->IsExtern && decl->hasAttr("export"))
      exported.push_back({decl, f});
  for (auto &[decl, f] : exported) {
    if (!decl->Ty ||
        !cSignatureFor(decl->Ty->params(), decl->Ty->result(), false))
      continue;
    const std::string name = f->getName().str();
    const GlobalValue::LinkageTypes linkage = f->getLinkage();
    f->setName(name + ".rune");
    f->setLinkage(GlobalValue::InternalLinkage);
    Function *adapter = cAdapterFor(decl, f);
    if (adapter == f)
      continue;
    adapter->setName(name);
    adapter->setLinkage(linkage);
  }
}

void CodeGen::releaseUnusedRuntime() {
  const std::filesystem::path dir =
      std::filesystem::path(Opts.RunetimeDir).lexically_normal();
  for (auto &[decl, f] : Functions) {
    if (!f || f->isDeclaration() || !f->hasExternalLinkage())
      continue;
    const SourceFile *file = SM.fileFor(decl->Range.begin());
    if (!file)
      continue;
    const std::filesystem::path path =
        std::filesystem::path(file->Path).lexically_normal();
    if (path.parent_path() != dir ||
        path.filename().string().rfind("freestanding", 0) != 0)
      continue;
    llvm::StringRef name = f->getName();
    if (name.starts_with("mem") || name.starts_with("__"))
      continue;
    setDiscardableLinkage(f, name.str());
  }
}

/// What each standard library function needs on a freestanding build. A
/// function is `bare` when nothing it reaches — through calls, vtables,
/// descriptors, function pointers — is left only declared; otherwise the
/// answer is the first such symbol: something the C library or the hosted
/// runtime defines and a bare-metal program does not have. A generic's body
/// exists only once instantiated, so for one never instantiated here its own
/// calls are followed as written.
void CodeGen::reportTiers() {
  std::map<const llvm::Function *, std::string> memo;
  std::set<const llvm::Function *> busy;
  std::set<const llvm::Value *> seenConst;
  std::function<std::string(const llvm::Function *)> needs;
  std::function<std::string(const llvm::Value *)> refs =
      [&](const llvm::Value *v) -> std::string {
    if (auto *f = dyn_cast<llvm::Function>(v))
      return needs(f);
    if (auto *gv = dyn_cast<llvm::GlobalVariable>(v)) {
      if (!gv->hasInitializer() || !seenConst.insert(gv).second)
        return "";
      return refs(gv->getInitializer());
    }
    if (auto *c = dyn_cast<llvm::Constant>(v)) {
      if (isa<llvm::ConstantData>(c) || !seenConst.insert(c).second)
        return "";
      for (const llvm::Use &op : c->operands()) {
        std::string r = refs(op.get());
        if (!r.empty())
          return r;
      }
    }
    return "";
  };
  needs = [&](const llvm::Function *f) -> std::string {
    auto it = memo.find(f);
    if (it != memo.end())
      return it->second;
    if (f->isIntrinsic())
      return "";
    if (f->isDeclaration())
      return memo[f] = f->getName().str();
    if (!busy.insert(f).second)
      return "";
    std::string found;
    for (const llvm::BasicBlock &bb : *f) {
      for (const llvm::Instruction &in : bb) {
        for (const llvm::Use &op : in.operands())
          if (!isa<llvm::BasicBlock>(op.get()) &&
              (found = refs(op.get()), !found.empty()))
            break;
        if (!found.empty())
          break;
      }
      if (!found.empty())
        break;
    }
    busy.erase(f);
    return memo[f] = found;
  };

  auto key = [](const FunctionDecl *fn) {
    std::string owner;
    if (fn->OwnerType && fn->OwnerType->nominal()) {
      NominalDecl *nd = fn->OwnerType->nominal();
      if (nd->GenericTemplate)
        nd = nd->GenericTemplate;
      owner = static_cast<Decl *>(nd)->Name;
    } else if (fn->Parent && isa<NominalDecl>(fn->Parent)) {
      owner = fn->Parent->Name;
    }
    return fn->ModulePath + "|" + owner + "|" + fn->Name;
  };
  auto note = [&](const FunctionDecl *fn, const std::string &need) {
    std::string &slot = Tiers[key(fn)];
    // One instantiation that needs a hosted build is enough to say so.
    if (slot.empty() || slot == "bare")
      slot = need.empty() ? "bare" : need;
  };
  for (auto &[decl, f] : Functions)
    if (decl && f && !f->isDeclaration() && isAncillary(decl))
      note(decl, needs(f));

  // Generic templates. An instantiation the library made says it best;
  // otherwise the template's own calls are followed by name, as written —
  // its body is only checked once instantiated, so nothing is resolved yet.
  // A call to a module's `extern` function nothing here defines is a need.
  std::map<std::string, const FunctionDecl *> byName; // module|name
  for (Module *m : TierModules)
    for (auto &d : m->Decls) {
      if (auto *fn = dyn_cast<FunctionDecl>(d.get()))
        byName[m->Name + "|" + fn->Name] = fn;
      if (auto *ex = dyn_cast<ExternDecl>(d.get()))
        for (auto &fn : ex->Functions)
          byName[m->Name + "|" + fn->Name] = fn.get();
    }
  auto lastSegment = [](const std::string &path) {
    size_t at = path.rfind("::");
    return at == std::string::npos ? path : path.substr(at + 2);
  };
  std::map<const FunctionDecl *, std::string> templ;
  std::function<std::string(const FunctionDecl *, const std::string &)>
      templateNeeds = [&](const FunctionDecl *fn,
                          const std::string &module) -> std::string {
    auto it = templ.find(fn);
    if (it != templ.end())
      return it->second;
    templ[fn] = "";
    for (FunctionDecl *inst : fn->Instantiations) {
      auto f = Functions.find(inst);
      if (f != Functions.end() && f->second && !f->second->isDeclaration())
        return templ[fn] = needs(f->second);
    }
    auto external = [&](const FunctionDecl *t) -> std::string {
      std::string sym = t->MangledName.empty() ? t->Name : t->MangledName;
      llvm::Function *def = M->getFunction(sym);
      return !def || def->isDeclaration() ? sym : "";
    };
    std::string found;
    std::function<void(const Node *)> walk = [&](const Node *n) {
      if (!n || !found.empty())
        return;
      if (auto *c = dyn_cast<CallExpr>(n)) {
        const FunctionDecl *t = c->Target;
        if (!t)
          if (auto *r = dyn_cast<DeclRefExpr>(c->Callee.get())) {
            if (r->Path.size() == 1) {
              auto hit = byName.find(module + "|" + r->Path[0]);
              if (hit != byName.end())
                t = hit->second;
            } else if (r->Path.size() >= 2) {
              // `thread::spawn` from another module: the module whose last
              // segment is the qualifier.
              for (Module *m : TierModules)
                if (lastSegment(m->Name) == r->Path[r->Path.size() - 2]) {
                  auto hit = byName.find(m->Name + "|" + r->Path.back());
                  if (hit != byName.end())
                    t = hit->second;
                }
            }
          }
        if (t) {
          auto f = Functions.find(const_cast<FunctionDecl *>(t));
          if (t->IsExtern)
            found = external(t);
          else if (f != Functions.end() && f->second &&
                   !f->second->isDeclaration())
            found = needs(f->second);
          else if (t->Body && t != fn)
            found = templateNeeds(t, t->ModulePath);
        }
      }
      forEachChild(n, walk);
    };
    if (fn->Body)
      walk(fn->Body.get());
    return templ[fn] = found;
  };
  std::function<void(const Node *, const std::string &)> collect =
      [&](const Node *n, const std::string &module) {
    if (!n)
      return;
    if (auto *fn = dyn_cast<FunctionDecl>(n)) {
      if (fn->Body && fn->IsPublic && !Tiers.count(key(fn)) &&
          !fn->hasAttr("intrinsic"))
        note(fn, templateNeeds(fn, module));
      return;
    }
    forEachChild(n, [&](const Node *c) { collect(c, module); });
  };
  for (Module *m : TierModules)
    for (auto &d : m->Decls)
      if (Sema.isAncillary(d->ModulePath))
        collect(d.get(), m->Name);
}

void CodeGen::pruneUnreachable() {
  ModuleAnalysisManager mam;
  PassBuilder pb;
  pb.registerModuleAnalyses(mam);
  ModulePassManager mpm;
  mpm.addPass(GlobalDCEPass());
  mpm.run(*M, mam);
}

void initialiseTargets() {
  static const bool once = [] {
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();
    return true;
  }();
  (void)once;
}

std::unique_ptr<llvm::TargetMachine>
createTargetMachine(const llvm::Triple &triple, std::string &err,
                    llvm::CodeGenOptLevel level) {
  initialiseTargets();
  const llvm::Target *target = llvm::TargetRegistry::lookupTarget(triple, err);
  if (!target)
    return nullptr;
  llvm::Reloc::Model reloc = llvm::Reloc::PIC_;
  std::string features;
  // Bare metal — `x86_64-unknown-none`, `i686-unknown-none-elf`: an image
  // loaded where its linker script says, with no dynamic loader to relocate
  // it and no GOT anyone will fill in.
  if (triple.getOS() == llvm::Triple::UnknownOS && !triple.isWasm())
    reloc = llvm::Reloc::Static;
  // LLVM's generic RISC-V has no multiply or divide; every board Rune would
  // run on has them, and clang's bare-metal default is the same `imac`.
  if (triple.isRISCV())
    features = "+m,+a,+c";
  if (triple.isWasm()) {
    reloc = llvm::Reloc::Static;
    if (triple.str().find("threads") != std::string::npos)
      features = "+atomics,+bulk-memory,+mutable-globals";
  }
  llvm::TargetOptions targetOpts;
  return std::unique_ptr<llvm::TargetMachine>(target->createTargetMachine(
      triple, "generic", features, targetOpts,
      std::optional<llvm::Reloc::Model>(reloc),
      std::optional<llvm::CodeModel::Model>(), level));
}

void optimizeModule(llvm::Module &m, unsigned level) {
  PassBuilder pb;
  LoopAnalysisManager lam;
  FunctionAnalysisManager fam;
  CGSCCAnalysisManager cgam;
  ModuleAnalysisManager mam;
  pb.registerModuleAnalyses(mam);
  pb.registerCGSCCAnalyses(cgam);
  pb.registerFunctionAnalyses(fam);
  pb.registerLoopAnalyses(lam);
  pb.crossRegisterProxies(lam, fam, cgam, mam);

  OptimizationLevel ol = OptimizationLevel::O0;
  switch (level) {
  case 1: ol = OptimizationLevel::O1; break;
  case 2: ol = OptimizationLevel::O2; break;
  case 3: ol = OptimizationLevel::O3; break;
  default: ol = OptimizationLevel::O0; break;
  }
  if (ol == OptimizationLevel::O0)
    return;
  ModulePassManager mpm = pb.buildPerModuleDefaultPipeline(ol);
  mpm.run(m, mam);
}

} // namespace rune
