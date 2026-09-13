//===- CodeGenExpr.cpp - Lowering expressions and patterns -----*- C++ -*-===//

#include "rune/CodeGen.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Intrinsics.h>

namespace rune {

using namespace llvm;

//===----------------------------------------------------------------------===//
// Literals and simple values
//===----------------------------------------------------------------------===//

Value *CodeGen::emitStringLiteral(const std::string &text, bool asCString) {
  std::string key = (asCString ? "c:" : "s:") + text;
  GlobalVariable *gv = nullptr;
  auto it = StringLiterals.find("bytes:" + text);
  if (it != StringLiterals.end()) {
    gv = it->second;
  } else {
    Constant *c = ConstantDataArray::getString(*Ctx, text, /*AddNull=*/true);
    gv = new GlobalVariable(*M, c->getType(), true, GlobalValue::PrivateLinkage,
                            c, ".rune.str");
    gv->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
    StringLiterals["bytes:" + text] = gv;
  }
  if (asCString)
    return gv;
  // One String object per literal, built on first use and shared from then
  // on. A String is immutable — every operation returns a new one — so there
  // is nothing to observe in the sharing, and the object is marked immortal:
  // never freed, never counted, and retain/release on it do nothing. That is
  // why the result is not tracked as a temporary.
  GlobalVariable *slot = nullptr;
  auto cached = StringLiterals.find(key);
  if (cached != StringLiterals.end()) {
    slot = cached->second;
  } else {
    slot = new GlobalVariable(*M, PtrTy, /*isConstant=*/false,
                              GlobalValue::PrivateLinkage,
                              Constant::getNullValue(PtrTy), ".rune.strobj");
    StringLiterals[key] = slot;
  }
  return B->CreateCall(
      runtimeFn("rune_string_literal", PtrTy, {PtrTy, B->getInt64Ty(), PtrTy}),
      {gv, ConstantInt::get(B->getInt64Ty(), text.size()), slot});
}

//===----------------------------------------------------------------------===//
// Addresses
//===----------------------------------------------------------------------===//

/// The declaration behind a member access, when it names a `weak` field.
static FieldDecl *weakFieldOf(const MemberExpr *m, Type *effectiveBase) {
  if (!m || m->FieldIndex < 0 || !effectiveBase || !effectiveBase->isNominal())
    return nullptr;
  NominalDecl *nd = effectiveBase->nominal();
  std::vector<NominalDecl *> chain{nd};
  if (auto *c = dyn_cast<ClassDecl>(static_cast<Decl *>(nd)))
    for (ClassDecl *s = c->Super; s; s = s->Super)
      chain.push_back(s);
  for (NominalDecl *n : chain)
    for (const auto &f : n->Fields)
      if (static_cast<int>(f->Index) == m->FieldIndex && f->IsWeak)
        return f.get();
  return nullptr;
}

Value *CodeGen::emitMemberAddress(MemberExpr *m) {
  Type *baseTy = m->Base->Ty;
  Value *addr = nullptr;
  Type *eff = baseTy;

  if (eff->is(TypeKind::Pointer)) {
    addr = emitRValue(m->Base.get());
    // Under Zombie a shared `&Node` is the object itself.
    bool isObject = handleBorrow(eff);
    eff = eff->pointee();
    while (eff->is(TypeKind::Pointer)) {
      addr = B->CreateLoad(PtrTy, addr);
      isObject = handleBorrow(eff);
      eff = eff->pointee();
    }
    // A class reference is itself a pointer, so `&Node` names a slot holding
    // the object's address rather than the object. One more load reaches it.
    if (eff->is(TypeKind::Class) && !isObject)
      addr = B->CreateLoad(PtrTy, addr, "borrowed.obj");
  } else if (eff->is(TypeKind::Class)) {
    addr = emitRValue(m->Base.get());
  } else {
    addr = emitLValue(m->Base.get());
  }
  if (!addr)
    return nullptr;

  if (eff->is(TypeKind::Tuple)) {
    return B->CreateStructGEP(lower(eff), addr,
                              static_cast<unsigned>(m->FieldIndex), m->Name);
  }
  if (!eff->isNominal())
    return nullptr;
  StructType *layout = layoutOf(eff->nominal(), eff);
  unsigned index = static_cast<unsigned>(m->FieldIndex);
  // Class instances carry the object header in slot 0.
  if (eff->is(TypeKind::Class))
    index += 1;
  return B->CreateStructGEP(layout, addr, index, m->Name);
}

Value *CodeGen::emitIndexAddress(IndexExpr *i) {
  Type *baseTy = i->Base->Ty;
  // `p[n]` on a raw pointer: offset arithmetic on the pointee's size, with
  // nothing to check it against. Sema has already required an unsafe context.
  if (i->ThroughRawPointer) {
    Value *p = emitRValue(i->Base.get());
    Value *index = coerce(emitRValue(i->Index.get()), i->Index->Ty, Types.i64());
    if (!p || !index)
      return nullptr;
    return B->CreateGEP(lower(baseTy->pointee()), p, {index}, "raw.elem");
  }
  Type *eff = baseTy;
  Value *addr = nullptr;
  if (eff->is(TypeKind::Pointer)) {
    addr = emitRValue(i->Base.get());
    eff = eff->pointee();
  } else {
    addr = emitLValue(i->Base.get());
  }
  if (!addr)
    return nullptr;

  Value *index = emitRValue(i->Index.get());
  index = coerce(index, i->Index->Ty, Types.i64());

  if (eff->is(TypeKind::Array)) {
    emitBoundsCheck(index, ConstantInt::get(B->getInt64Ty(), eff->arraySize()),
                    i->BracketRange.isValid() ? i->BracketRange : i->Range);
    return B->CreateInBoundsGEP(lower(eff), addr,
                                {B->getInt64(0), index}, "elem");
  }
  if (eff->is(TypeKind::Slice)) {
    StructType *sliceTy = cast<StructType>(lower(eff));
    Value *data = B->CreateLoad(PtrTy, B->CreateStructGEP(sliceTy, addr, 0));
    Value *len =
        B->CreateLoad(B->getInt64Ty(), B->CreateStructGEP(sliceTy, addr, 1));
    emitBoundsCheck(index, len,
                    i->BracketRange.isValid() ? i->BracketRange : i->Range);
    return B->CreateInBoundsGEP(lower(eff->element()), data, index, "elem");
  }
  return nullptr;
}

Value *CodeGen::emitLValue(Expr *e) {
  if (!e)
    return nullptr;
  switch (e->Kind) {
  case NodeKind::DeclRef: {
    auto *r = cast<DeclRefExpr>(e);
    if (auto *v = dyn_cast<VarDecl>(r->Resolved)) {
      // A variable captured from an enclosing function lives in the closure's
      // environment rather than in a local slot.
      auto cap = fs().CaptureIndex.find(v);
      if (cap != fs().CaptureIndex.end() && fs().EnvValue && fs().Closure) {
        StructType *envTy = envTypeFor(fs().Closure);
        return B->CreateStructGEP(envTy, fs().EnvValue, 1 + cap->second,
                                  v->Name);
      }
      auto it = fs().Slots.find(v);
      if (it != fs().Slots.end())
        return it->second;
      return nullptr;
    }
    if (auto *g = dyn_cast<GlobalVarDecl>(r->Resolved))
      return declareGlobal(g);
    // Anything else a name can resolve to — an enum variant used as a value,
    // a function used as one — is an rvalue with no storage of its own.
    // Borrowing it means putting it somewhere first, which is what the
    // materialising fallback below does.
    break;
  }
  case NodeKind::SelfRef: {
    auto *s = cast<SelfExpr>(e);
    if (s->Binding) {
      auto cap = fs().CaptureIndex.find(s->Binding);
      if (cap != fs().CaptureIndex.end() && fs().EnvValue && fs().Closure)
        return B->CreateStructGEP(envTypeFor(fs().Closure), fs().EnvValue,
                                  1 + cap->second, "self");
      auto it = fs().Slots.find(s->Binding);
      if (it != fs().Slots.end())
        return it->second;
    }
    return nullptr;
  }
  case NodeKind::Member: {
    auto *m = cast<MemberExpr>(e);
    // A weak field's storage is a bare pointer, but the field *reads* as an
    // Option. Anything that wants its address — a `&self` receiver, a borrow —
    // has to be handed the Option, so materialise it below rather than
    // exposing the slot.
    Type *base = m->Base->Ty;
    while (base && base->is(TypeKind::Pointer))
      base = base->pointee();
    if (!weakFieldOf(m, base))
      return emitMemberAddress(m);
    break;
  }
  case NodeKind::Index: {
    auto *i = cast<IndexExpr>(e);
    // `values[a..b]` builds a new slice value rather than naming storage, so
    // it has to be materialised before it can be addressed.
    if (isa<RangeExpr>(i->Index.get()))
      break;
    // An overloaded `[]` is a call. What it returns is a value, not a place,
    // so it has no address either — materialise it below. Writing through one
    // does not come here; that is `index_set`, handled in emitAssign.
    if (i->OverloadResolved)
      break;
    return emitIndexAddress(i);
  }
  case NodeKind::Deref: {
    auto *d = cast<DerefExpr>(e);
    // An overloaded `*` is a call, like an overloaded `[]`: the operand is the
    // object, not the address of what it yields. Materialise it below.
    if (d->OverloadResolved)
      break;
    // A shared `&Class` under Zombie is the object, not a slot holding it:
    // the value has no address of its own, so it is given one here.
    if (handleBorrow(d->Operand->Ty)) {
      Value *obj = emitRValue(d->Operand.get());
      Value *tmp = createEntryAlloca(lower(e->Ty), "borrowed.slot");
      B->CreateStore(obj, tmp);
      return tmp;
    }
    // Otherwise the pointer value is already the address we want.
    return emitRValue(d->Operand.get());
  }
  default:
    break;
  }
  // Anything else is a temporary; materialise it so it can be addressed.
  Value *v = emitRValue(e);
  if (!v)
    return nullptr;
  Value *tmp = createEntryAlloca(lower(e->Ty), "materialised");
  B->CreateStore(v, tmp);
  return tmp;
}

//===----------------------------------------------------------------------===//
// Stores
//===----------------------------------------------------------------------===//

void CodeGen::emitInto(Expr *e, Value *slot, Type *slotType, bool raw) {
  if (!e || !slot)
    return;
  // `AdoptedResult` applies to this expression alone; a sub-expression is
  // counted the ordinary way.
  struct AdoptGuard {
    Expr *&Slot;
    Expr *Saved;
    ~AdoptGuard() { Slot = Saved; }
  } adoptGuard{AdoptedResult, AdoptedResult};
  if (AdoptedResult != e)
    AdoptedResult = nullptr;

  // Structured expressions write straight into the destination so no
  // intermediate copy of an aggregate is needed.
  switch (e->Kind) {
  case NodeKind::If:
    emitIf(cast<IfExpr>(e), slot, slotType);
    return;
  case NodeKind::Match:
    emitMatch(cast<MatchExpr>(e), slot, slotType);
    return;
  case NodeKind::Block:
    emitBlock(cast<BlockExpr>(e), slot, slotType);
    return;
  case NodeKind::UnsafeBlock:
    emitBlock(cast<UnsafeBlockExpr>(e)->Body.get(), slot, slotType);
    return;
  case NodeKind::Loop:
    emitLoop(cast<LoopExpr>(e), slot, slotType);
    return;
  default:
    break;
  }

  Value *v = emitRValue(e);
  if (!v || !slotType || slotType->isVoid())
    return;
  Value *produced = v;
  v = coerce(v, e->Ty, slotType);

  // A store through a raw pointer is exactly that: no counting, and no
  // assumption that the slot already held anything. `mem::retain` and
  // `mem::release` are how the caller keeps the books in that case. Under
  // Zombie the value still moves in: the source gives it up either way.
  if (zombie() && AdoptedResult != e)
    takeOwnership(e, produced, e->Ty);
  if (!raw && AdoptedResult != e) {
    // Retain before releasing the old value: they may be the same object.
    emitRetain(v, slotType);
    if (slotType->isRefCounted()) {
      Value *old = B->CreateLoad(lower(slotType), slot);
      emitRelease(old, slotType);
    }
  }
  B->CreateStore(v, slot);
}

//===----------------------------------------------------------------------===//
// Closures
//===----------------------------------------------------------------------===//

StructType *CodeGen::envTypeFor(ClosureExpr *c) {
  auto it = ClosureEnvTypes.find(c);
  if (it != ClosureEnvTypes.end())
    return it->second;
  std::vector<llvm::Type *> body{ObjectHeaderTy};
  for (const Capture &cap : c->Captures)
    body.push_back(lower(cap.Ty));
  auto *st = StructType::create(*Ctx, body, "rune.env");
  ClosureEnvTypes[c] = st;
  return st;
}

GlobalVariable *CodeGen::envTypeInfoFor(ClosureExpr *c) {
  auto it = ClosureEnvInfos.find(c);
  if (it != ClosureEnvInfos.end())
    return it->second;

  StructType *envTy = envTypeFor(c);
  // The environment's deinit releases every captured reference.
  auto *deinitTy = FunctionType::get(B->getVoidTy(), {PtrTy}, false);
  auto *deinit = Function::Create(deinitTy, GlobalValue::InternalLinkage,
                                  "rune.env.deinit", *M);
  {
    auto *saveBB = B->GetInsertBlock();
    auto saveIt = saveBB ? B->GetInsertPoint() : BasicBlock::iterator();
    // As with class destructors, releasing a capture may need scratch storage
    // and extra blocks, so give the generated function its own state.
    FunctionState st;
    st.Fn = deinit;
    st.ReturnType = Types.voidType();
    FnStack.push_back(st);
    fs().Scopes.push_back(LexicalScope{});
    B->SetInsertPoint(BasicBlock::Create(*Ctx, "entry", deinit));
    Value *env = deinit->getArg(0);
    for (unsigned i = 0; i < c->Captures.size(); ++i) {
      Type *ct = c->Captures[i].Ty;
      if (!ct || !ct->isRefCounted())
        continue;
      Value *p = B->CreateStructGEP(envTy, env, 1 + i);
      emitRelease(B->CreateLoad(lower(ct), p), ct);
    }
    B->CreateRetVoid();
    fs().Scopes.pop_back();
    FnStack.pop_back();
    if (saveBB)
      B->SetInsertPoint(saveBB, saveIt);
  }
  ClosureEnvDeinits[c] = deinit;

  Constant *nameConst = ConstantDataArray::getString(*Ctx, "closure", true);
  auto *nameGV = new GlobalVariable(*M, nameConst->getType(), true,
                                    GlobalValue::PrivateLinkage, nameConst,
                                    ".rune.envname");
  uint64_t size = M->getDataLayout().getTypeAllocSize(envTy).getFixedValue();
  auto *gv = new GlobalVariable(
      *M, TypeInfoTy, true, GlobalValue::InternalLinkage,
      ConstantStruct::get(TypeInfoTy,
                          {nameGV, ConstantInt::get(B->getInt64Ty(), size),
                           deinit, ConstantPointerNull::get(PtrTy),
                           ConstantPointerNull::get(PtrTy), B->getInt32(0)}),
      "rune.typeinfo.env");
  ClosureEnvInfos[c] = gv;
  return gv;
}

Value *CodeGen::emitClosureValue(ClosureExpr *c) {
  if (!c->Lifted)
    return Constant::getNullValue(lower(c->Ty));
  Function *fn = declareFunction(c->Lifted);
  StructType *envTy = envTypeFor(c);
  GlobalVariable *info = envTypeInfoFor(c);

  uint64_t size = M->getDataLayout().getTypeAllocSize(envTy).getFixedValue();
  Value *env = B->CreateCall(
      runtimeFn("rune_alloc", PtrTy, {B->getInt64Ty(), PtrTy}),
      {ConstantInt::get(B->getInt64Ty(), size), info});

  // Captures are copied by value at the moment the closure is created.
  for (unsigned i = 0; i < c->Captures.size(); ++i) {
    const Capture &cap = c->Captures[i];
    Value *src = nullptr;
    auto outerCap = fs().CaptureIndex.find(cap.Var);
    if (outerCap != fs().CaptureIndex.end() && fs().EnvValue && fs().Closure) {
      src = B->CreateStructGEP(envTypeFor(fs().Closure), fs().EnvValue,
                               1 + outerCap->second);
    } else {
      auto it = fs().Slots.find(cap.Var);
      if (it == fs().Slots.end())
        continue;
      src = it->second;
    }
    Value *v = B->CreateLoad(lower(cap.Ty), src);
    if (zombie()) {
      // Moved into the closure: the outer binding is empty from here — an
      // alias binding, which owns nothing, is simply copied.
      if (cap.Var && !cap.Var->ZombieAlias && cap.Ty && cap.Ty->isRefCounted())
        B->CreateStore(Constant::getNullValue(lower(cap.Ty)), src);
    } else {
      emitRetain(v, cap.Ty);
    }
    B->CreateStore(v, B->CreateStructGEP(envTy, env, 1 + i));
  }

  Value *closure = UndefValue::get(lower(c->Ty));
  closure = B->CreateInsertValue(closure, fn, 0);
  closure = B->CreateInsertValue(closure, env, 1);
  return track(closure, c->Ty);
}

//===----------------------------------------------------------------------===//
// Aggregates
//===----------------------------------------------------------------------===//

Value *CodeGen::emitStructLit(StructLitExpr *s) {
  Type *t = s->Ty;
  Value *slot = createEntryAlloca(lower(t), "literal");
  if (t->isRefCounted())
    B->CreateStore(Constant::getNullValue(lower(t)), slot);

  if (s->VariantIndex >= 0) {
    // A struct-shaped enum variant: discriminant, then the payload.
    auto *e = reinterpret_cast<EnumDecl *>(t->nominal());
    StructType *layout = layoutOf(t->nominal(), t);
    auto *variant = e->Variants[static_cast<size_t>(s->VariantIndex)].get();
    B->CreateStore(B->getInt32(static_cast<uint32_t>(variant->Value)),
                   B->CreateStructGEP(layout, slot, 0));
    if (layout->getNumElements() > 1) {
      Value *payload = B->CreateStructGEP(layout, slot, 1);
      llvm::Type *pt = variantPayloadType(e, static_cast<unsigned>(s->VariantIndex));
      for (const auto &lf : s->Fields) {
        if (!lf.Value)
          continue;
        Value *fieldPtr = B->CreateStructGEP(pt, payload, lf.FieldIndex);
        Type *ft = variant->Fields[lf.FieldIndex]->Ty;
        emitInto(lf.Value.get(), fieldPtr, ft);
      }
    }
    return B->CreateLoad(lower(t), slot);
  }

  StructType *layout = layoutOf(t->nominal(), t);
  auto fields = allFieldsOf(t->nominal());

  // `..base` supplies everything the literal does not mention.
  if (s->Base) {
    emitInto(s->Base.get(), slot, t);
  } else {
    for (size_t i = 0; i < fields.size(); ++i) {
      if (!fields[i]->DefaultValue)
        continue;
      bool given = false;
      for (const auto &lf : s->Fields)
        if (lf.FieldIndex == fields[i]->Index)
          given = true;
      if (!given)
        emitInto(fields[i]->DefaultValue.get(),
                 B->CreateStructGEP(layout, slot, static_cast<unsigned>(i)),
                 fields[i]->Ty);
    }
  }

  for (const auto &lf : s->Fields) {
    if (!lf.Value)
      continue;
    Value *fieldPtr = B->CreateStructGEP(layout, slot, lf.FieldIndex);
    emitInto(lf.Value.get(), fieldPtr, fields[lf.FieldIndex]->Ty);
  }
  Value *v = B->CreateLoad(lower(t), slot);
  // The slot owns +1 on every reference-counted field; hand that to the
  // statement so the temporary is released once the value is consumed.
  return track(v, t);
}

/// A scalar literal as an LLVM constant of `t`, or null when `e` is not one.
/// Negative literals arrive as a unary minus over a literal.
static Constant *scalarConstantOf(Expr *e, Type *t, llvm::Type *lowered,
                                  IRBuilder<> &B) {
  if (!e || !t || !lowered)
    return nullptr;
  bool negate = false;
  if (auto *u = dyn_cast<UnaryExpr>(e)) {
    if (u->Op != UnaryOp::Neg)
      return nullptr;
    negate = true;
    e = u->Operand.get();
  }
  if (auto *i = dyn_cast<IntLitExpr>(e)) {
    if (t->isInt())
      return ConstantInt::get(lowered, negate ? 0 - i->Value : i->Value);
    if (t->isFloat()) {
      double v = static_cast<double>(i->Value);
      return ConstantFP::get(lowered, negate ? -v : v);
    }
    return nullptr;
  }
  if (auto *f = dyn_cast<FloatLitExpr>(e))
    return t->isFloat() ? ConstantFP::get(lowered, negate ? -f->Value
                                                          : f->Value)
                        : nullptr;
  if (negate)
    return nullptr;
  if (auto *b = dyn_cast<BoolLitExpr>(e))
    return t->isBool() ? B.getInt1(b->Value) : nullptr;
  if (auto *c = dyn_cast<CharLitExpr>(e))
    return t->is(TypeKind::Char) ? B.getInt32(c->Value) : nullptr;
  return nullptr;
}

Value *CodeGen::emitArrayLit(ArrayLitExpr *a) {
  Type *t = a->Ty;
  llvm::Type *arrTy = lower(t);
  Type *elem = t->element();

  // An array of scalar literals — `[0u32; 256]`, a table of round
  // constants — is one constant aggregate, not a store per element. A
  // global table would otherwise cost every program its initialiser.
  if (elem && !elem->isRefCounted() && arrTy->isArrayTy()) {
    llvm::Type *elemTy = lower(elem);
    std::vector<Constant *> values;
    bool allConstant = true;
    if (a->RepeatCount) {
      Constant *one = a->Elements.empty()
                          ? nullptr
                          : scalarConstantOf(a->Elements[0].get(), elem,
                                             elemTy, *B);
      if (!one)
        allConstant = false;
      else
        values.assign(static_cast<size_t>(t->arraySize()), one);
    } else {
      for (const auto &e : a->Elements) {
        Constant *c = scalarConstantOf(e.get(), elem, elemTy, *B);
        if (!c) {
          allConstant = false;
          break;
        }
        values.push_back(c);
      }
      if (values.size() != t->arraySize())
        allConstant = false;
    }
    if (allConstant && !values.empty())
      return ConstantArray::get(cast<ArrayType>(arrTy), values);
  }

  Value *slot = createEntryAlloca(arrTy, "array");
  if (t->isRefCounted())
    B->CreateStore(Constant::getNullValue(arrTy), slot);

  if (a->RepeatCount) {
    // `[value; n]` evaluates the element once and copies it into every slot.
    if (!a->Elements.empty()) {
      Value *tmp = createEntryAlloca(lower(elem), "repeat.value");
      if (elem->isRefCounted())
        B->CreateStore(Constant::getNullValue(lower(elem)), tmp);
      emitInto(a->Elements[0].get(), tmp, elem);
      Value *v = B->CreateLoad(lower(elem), tmp);
      for (uint64_t i = 0; i < t->arraySize(); ++i) {
        emitRetain(v, elem);
        B->CreateStore(v, B->CreateInBoundsGEP(
                              arrTy, slot,
                              {B->getInt64(0), B->getInt64(i)}));
      }
      emitRelease(v, elem); // drop the extra reference held by `tmp`
    }
  } else {
    for (size_t i = 0; i < a->Elements.size(); ++i) {
      Value *elemPtr = B->CreateInBoundsGEP(
          arrTy, slot, {B->getInt64(0), B->getInt64(i)});
      emitInto(a->Elements[i].get(), elemPtr, elem);
    }
  }
  return track(B->CreateLoad(arrTy, slot), t);
}

//===----------------------------------------------------------------------===//
// Calls
//===----------------------------------------------------------------------===//

std::vector<Value *> CodeGen::buildArguments(CallExpr *c, FunctionDecl *fn,
                                             const std::vector<Type *> &paramTypes,
                                             bool variadic) {
  std::vector<Value *> args;
  std::vector<bool> used(c->Args.size(), false);

  // Formal parameters excluding `self`.
  std::vector<const Param *> formals;
  if (fn)
    for (const Param &p : fn->Params)
      if (!p.IsSelf)
        formals.push_back(&p);

  size_t count = std::max(paramTypes.size(),
                          c->ArgOrder.size() ? c->ArgOrder.size() : 0);
  for (size_t i = 0; i < count; ++i) {
    Type *want = i < paramTypes.size() ? paramTypes[i] : nullptr;
    Expr *value = nullptr;
    if (i < c->ArgOrder.size() && c->ArgOrder[i] != static_cast<unsigned>(-1) &&
        c->ArgOrder[i] < c->Args.size()) {
      value = c->Args[c->ArgOrder[i]].Value.get();
      used[c->ArgOrder[i]] = true;
    } else if (i < formals.size() && formals[i]->DefaultValue) {
      value = formals[i]->DefaultValue.get();
    }
    if (!value) {
      args.push_back(want ? Constant::getNullValue(lower(want))
                          : ConstantPointerNull::get(PtrTy));
      continue;
    }
    Value *v = emitRValue(value);
    // Under Zombie an argument passed by value is moved in: the callee owns
    // it from here. A borrow (`&T`) is a copy of a pointer and owns nothing.
    if (zombie() && want && !want->is(TypeKind::Pointer))
      takeOwnership(value, v, value->Ty);
    if (want)
      v = coerce(v, value->Ty, want);
    args.push_back(v);
  }

  if (!variadic)
    return args;

  // Trailing variadic arguments follow the C promotions, so a foreign
  // `printf("%d", small)` receives what it expects.
  for (size_t i = 0; i < c->Args.size(); ++i) {
    if (used[i])
      continue;
    Expr *e = c->Args[i].Value.get();
    Value *v = emitRValue(e);
    if (zombie())
      takeOwnership(e, v, e->Ty);
    Type *t = e->Ty;
    if (t && t->isFloat() && t->floatWidth() < 64)
      v = B->CreateFPExt(v, B->getDoubleTy());
    else if (t && t->isInt() && t->intWidth() < 32)
      v = t->isSigned() ? B->CreateSExt(v, B->getInt32Ty())
                        : B->CreateZExt(v, B->getInt32Ty());
    else if (t && t->isBool())
      v = B->CreateZExt(v, B->getInt32Ty());
    args.push_back(v);
  }
  return args;
}

Value *CodeGen::emitClassConstruction(CallExpr *c) {
  ClassDecl *cls = c->ConstructsClass;
  Type *t = cls->DeclaredType;
  StructType *layout = layoutOf(static_cast<NominalDecl *>(cls), t);
  GlobalVariable *info = emitTypeInfo(static_cast<NominalDecl *>(cls));
  uint64_t size = M->getDataLayout().getTypeAllocSize(layout).getFixedValue();

  Value *obj = B->CreateCall(
      runtimeFn("rune_alloc", PtrTy, {B->getInt64Ty(), PtrTy}),
      {ConstantInt::get(B->getInt64Ty(), size), info});
  obj->setName(cls->Name.c_str());

  // Field defaults run before `init` so an initialiser can overwrite them.
  auto fields = allFieldsOf(static_cast<NominalDecl *>(cls));
  for (size_t i = 0; i < fields.size(); ++i) {
    if (!fields[i]->DefaultValue)
      continue;
    emitInto(fields[i]->DefaultValue.get(),
             B->CreateStructGEP(layout, obj, 1 + static_cast<unsigned>(i)),
             fields[i]->Ty);
  }

  if (auto *init = dyn_cast<FunctionDecl>(c->Target)) {
    Function *initFn = declareFunction(init);
    std::vector<Value *> args{obj};
    for (Value *a : buildArguments(c, init, init->Ty->params(), false))
      args.push_back(a);
    B->CreateCall(initFn, args);
  }
  // A construction whose result goes straight into a slot that owns it hands
  // its allocation count over as it stands: nothing else holds the object, so
  // there is nothing to balance. `AdoptedResult` names that expression.
  if (AdoptedResult == static_cast<Expr *>(c))
    return obj;
  return track(obj, t);
}

Value *CodeGen::emitEnumConstruction(CallExpr *c) {
  EnumDecl *e = c->ConstructsEnum;
  Type *t = e->DeclaredType;
  StructType *layout = layoutOf(static_cast<NominalDecl *>(e), t);
  auto *variant = e->Variants[static_cast<size_t>(c->ConstructsVariant)].get();

  Value *slot = createEntryAlloca(layout, "variant");
  if (t->isRefCounted())
    B->CreateStore(Constant::getNullValue(layout), slot);
  B->CreateStore(B->getInt32(static_cast<uint32_t>(variant->Value)),
                 B->CreateStructGEP(layout, slot, 0));

  if (layout->getNumElements() > 1 && !c->Args.empty()) {
    Value *payload = B->CreateStructGEP(layout, slot, 1);
    llvm::Type *pt =
        variantPayloadType(e, static_cast<unsigned>(c->ConstructsVariant));
    for (size_t i = 0; i < c->Args.size() && i < variant->TupleTypes.size(); ++i)
      emitInto(c->Args[i].Value.get(),
               B->CreateStructGEP(pt, payload, static_cast<unsigned>(i)),
               variant->TupleTypes[i]->Resolved);
  }
  return track(B->CreateLoad(layout, slot), t);
}

Value *CodeGen::emitBuiltinMethod(CallExpr *c) {
  auto *member = cast<MemberExpr>(c->Callee.get());
  Type *recvTy = member->Base->Ty;
  while (recvTy->is(TypeKind::Pointer))
    recvTy = recvTy->pointee();

  // The receiver may arrive borrowed (`&self` inside a bind on a builtin), so
  // read through any layers of reference before using it.
  auto recvValue = [&]() -> Value * {
    Value *v = emitRValue(member->Base.get());
    Type *t = member->Base->Ty;
    while (t->is(TypeKind::Pointer)) {
      // A shared `&String` under Zombie is the string itself.
      if (handleBorrow(t)) {
        t = t->pointee();
        continue;
      }
      v = B->CreateLoad(lower(t->pointee()), v);
      t = t->pointee();
    }
    return v;
  };
  auto recvAddress = [&]() -> Value * {
    return member->Base->Ty->is(TypeKind::Pointer)
               ? emitRValue(member->Base.get())
               : emitLValue(member->Base.get());
  };
  auto arg = [&](size_t i) -> Value * {
    return i < c->Args.size() ? emitRValue(c->Args[i].Value.get()) : nullptr;
  };
  auto i64Arg = [&](size_t i) -> Value * {
    Value *v = arg(i);
    return v ? coerce(v, c->Args[i].Value->Ty, Types.i64()) : B->getInt64(0);
  };
  auto loc = [&] { return locationString(c->Range); };

  switch (c->Builtin) {
  case BuiltinMethod::Clone:
    return track(emitClone(recvValue(), recvTy), c->Ty);
  case BuiltinMethod::ToString: {
    Value *v = recvValue();
    const char *fn = "rune_string_from_i64";
    llvm::Type *argTy = B->getInt64Ty();
    if (recvTy->is(TypeKind::String)) {
      // Already a String. Under Zombie the caller must get a string of
      // its own, since the receiver keeps the one it has.
      if (zombie())
        return track(emitClone(v, recvTy), c->Ty);
      return v;
    }
    if (recvTy->isFloat()) {
      fn = "rune_string_from_f64";
      argTy = B->getDoubleTy();
      v = coerce(v, recvTy, Types.f64());
    } else if (recvTy->isBool()) {
      fn = "rune_string_from_bool";
      argTy = B->getInt8Ty();
      v = B->CreateZExt(v, B->getInt8Ty());
    } else if (recvTy->is(TypeKind::Char)) {
      fn = "rune_string_from_char";
      argTy = B->getInt32Ty();
    } else if (recvTy->is(TypeKind::CString)) {
      fn = "rune_string_from_cstr";
      argTy = PtrTy;
    } else if (recvTy->is(TypeKind::Pointer)) {
      fn = "rune_string_from_ptr";
      argTy = PtrTy;
    } else if (recvTy->isInt() && !recvTy->isSigned()) {
      fn = "rune_string_from_u64";
      v = coerce(v, recvTy, Types.u64());
    } else {
      v = coerce(v, recvTy, Types.i64());
    }
    return track(B->CreateCall(runtimeFn(fn, PtrTy, {argTy}), {v}),
                 Types.stringType());
  }
  case BuiltinMethod::StringLength:
    return B->CreateCall(runtimeFn("rune_string_length", B->getInt64Ty(), {PtrTy}),
                         {recvValue()});
  case BuiltinMethod::StringCharCount:
    return B->CreateCall(
        runtimeFn("rune_string_char_count", B->getInt64Ty(), {PtrTy}),
        {recvValue()});
  case BuiltinMethod::StringIsEmpty: {
    Value *len = B->CreateCall(
        runtimeFn("rune_string_length", B->getInt64Ty(), {PtrTy}),
        {recvValue()});
    return B->CreateICmpEQ(len, B->getInt64(0));
  }
  case BuiltinMethod::StringAt: {
    Value *s = recvValue();
    Value *next = createEntryAlloca(B->getInt64Ty(), "next");
    return B->CreateCall(
        runtimeFn("rune_string_char_at", B->getInt32Ty(),
                  {PtrTy, B->getInt64Ty(), PtrTy, PtrTy}),
        {s, i64Arg(0), next, loc()});
  }
  case BuiltinMethod::StringByteAt:
    return B->CreateCall(runtimeFn("rune_string_byte_at", B->getInt8Ty(),
                                   {PtrTy, B->getInt64Ty(), PtrTy}),
                         {recvValue(), i64Arg(0), loc()});
  case BuiltinMethod::StringSubstring: {
    Value *s = recvValue();
    Value *a0 = i64Arg(0), *a1 = i64Arg(1);
    Value *r = B->CreateCall(
        runtimeFn("rune_string_substring", PtrTy,
                  {PtrTy, B->getInt64Ty(), B->getInt64Ty(), PtrTy}),
        {s, a0, a1, loc()});
    return track(r, Types.stringType());
  }
  case BuiltinMethod::StringFind:
    return B->CreateCall(
        runtimeFn("rune_string_find", B->getInt64Ty(), {PtrTy, PtrTy}),
        {recvValue(), arg(0)});
  case BuiltinMethod::StringRepeat: {
    Value *s = recvValue();
    Value *r = B->CreateCall(
        runtimeFn("rune_string_repeat", PtrTy, {PtrTy, B->getInt64Ty()}),
        {s, i64Arg(0)});
    return track(r, Types.stringType());
  }
  case BuiltinMethod::StringCStr:
    return B->CreateCall(runtimeFn("rune_string_cstr", PtrTy, {PtrTy}),
                         {recvValue()});
  case BuiltinMethod::StringHash:
    return B->CreateCall(runtimeFn("rune_string_hash", B->getInt64Ty(), {PtrTy}),
                         {recvValue()});
  case BuiltinMethod::StringToInt:
  case BuiltinMethod::StringToFloat: {
    bool isInt = c->Builtin == BuiltinMethod::StringToInt;
    Value *ok = createEntryAlloca(B->getInt8Ty(), "ok");
    Value *v = B->CreateCall(
        runtimeFn(isInt ? "rune_string_to_i64" : "rune_string_to_f64",
                  isInt ? static_cast<llvm::Type *>(B->getInt64Ty())
                        : static_cast<llvm::Type *>(B->getDoubleTy()),
                  {PtrTy, PtrTy}),
        {recvValue(), ok});
    Value *parsed = B->CreateICmpNE(B->CreateLoad(B->getInt8Ty(), ok),
                                    B->getInt8(0));

    // Build `Option::Some(v)` or `Option::None` depending on the flag.
    Function *f = fs().Fn;
    Value *slot = createEntryAlloca(lower(c->Ty), "parsed");
    B->CreateStore(Constant::getNullValue(lower(c->Ty)), slot);
    auto *someBB = BasicBlock::Create(*Ctx, "parse.some", f);
    auto *noneBB = BasicBlock::Create(*Ctx, "parse.none", f);
    auto *doneBB = BasicBlock::Create(*Ctx, "parse.done", f);
    B->CreateCondBr(parsed, someBB, noneBB);

    B->SetInsertPoint(someBB);
    B->CreateStore(emitEnumVariant(c->Ty, variantIndexNamed(c->Ty, "Some"), {v}),
                   slot);
    B->CreateBr(doneBB);

    B->SetInsertPoint(noneBB);
    B->CreateStore(emitEnumVariant(c->Ty, variantIndexNamed(c->Ty, "None"), {}),
                   slot);
    B->CreateBr(doneBB);

    B->SetInsertPoint(doneBB);
    return B->CreateLoad(lower(c->Ty), slot);
  }
  case BuiltinMethod::IntWrappingAdd:
  case BuiltinMethod::IntWrappingSub:
  case BuiltinMethod::IntWrappingMul:
  case BuiltinMethod::IntSaturatingAdd:
  case BuiltinMethod::IntSaturatingSub:
  case BuiltinMethod::IntSaturatingMul: {
    Value *l = recvValue();
    Value *r = arg(0);
    if (!r)
      return l;
    r = coerce(r, c->Args[0].Value->Ty, recvTy);
    BinaryOp op = BinaryOp::Add;
    switch (c->Builtin) {
    case BuiltinMethod::IntWrappingSub:
    case BuiltinMethod::IntSaturatingSub: op = BinaryOp::Sub; break;
    case BuiltinMethod::IntWrappingMul:
    case BuiltinMethod::IntSaturatingMul: op = BinaryOp::Mul; break;
    default: break;
    }
    const bool saturating = c->Builtin == BuiltinMethod::IntSaturatingAdd ||
                            c->Builtin == BuiltinMethod::IntSaturatingSub ||
                            c->Builtin == BuiltinMethod::IntSaturatingMul;
    return emitIntArith(op, l, r, recvTy,
                        saturating ? OverflowMode::Saturating
                                   : OverflowMode::Wrapping,
                        c->Range);
  }
  case BuiltinMethod::IntCheckedAdd:
  case BuiltinMethod::IntCheckedSub:
  case BuiltinMethod::IntCheckedMul: {
    Value *l = recvValue();
    Value *r = arg(0);
    if (!r)
      return Constant::getNullValue(lower(c->Ty));
    r = coerce(r, c->Args[0].Value->Ty, recvTy);
    BinaryOp op = c->Builtin == BuiltinMethod::IntCheckedSub   ? BinaryOp::Sub
                  : c->Builtin == BuiltinMethod::IntCheckedMul ? BinaryOp::Mul
                                                               : BinaryOp::Add;
    auto [res, ovf] = emitIntArithWithOverflow(op, l, r, recvTy);
    Function *f = fs().Fn;
    Value *slot = createEntryAlloca(lower(c->Ty), "checked");
    auto *someBB = BasicBlock::Create(*Ctx, "checked.fits", f);
    auto *noneBB = BasicBlock::Create(*Ctx, "checked.overflow", f);
    auto *doneBB = BasicBlock::Create(*Ctx, "checked.done", f);
    B->CreateCondBr(ovf, noneBB, someBB);
    B->SetInsertPoint(someBB);
    B->CreateStore(emitEnumVariant(c->Ty, variantIndexNamed(c->Ty, "Some"), {res}),
                   slot);
    B->CreateBr(doneBB);
    B->SetInsertPoint(noneBB);
    B->CreateStore(emitEnumVariant(c->Ty, variantIndexNamed(c->Ty, "None"), {}),
                   slot);
    B->CreateBr(doneBB);
    B->SetInsertPoint(doneBB);
    return B->CreateLoad(lower(c->Ty), slot);
  }
  case BuiltinMethod::SequenceLength: {
    Value *addr = recvAddress();
    Value *len = emitLengthOf(addr, recvTy);
    return len ? len : B->getInt64(0);
  }
  case BuiltinMethod::SequenceIsEmpty: {
    Value *addr = recvAddress();
    Value *len = emitLengthOf(addr, recvTy);
    return B->CreateICmpEQ(len ? len : B->getInt64(0), B->getInt64(0));
  }
  default:
    return nullptr;
  }
}

Value *CodeGen::emitCall(CallExpr *c) {
  // Builtins decide their own ownership: some create a fresh object (which
  // they track), while others hand back a borrowed reference that the receiver
  // still owns. Tracking here unconditionally would over-release the latter.
  if (c->Builtin != BuiltinMethod::None)
    return emitBuiltinMethod(c);
  if (c->ConstructsClass)
    return emitClassConstruction(c);
  if (c->ConstructsEnum)
    return emitEnumConstruction(c);

  auto *target = dyn_cast<FunctionDecl>(c->Target);

  // `@intrinsic` functions have no body — the compiler answers them here, from
  // the layout it is already computing for the target machine.
  if (target && target->hasAttr("intrinsic")) {
    const Attribute *a = target->findAttr("intrinsic");
    std::string which;
    if (a && !a->Args.empty())
      if (const auto *lit = dyn_cast<StringLitExpr>(a->Args[0].get()))
        which = lit->Value;
    Type *arg = target->TypeArguments.empty() ? nullptr
                                              : target->TypeArguments[0];
    if (arg && (which == "retain" || which == "release")) {
      Value *v = c->Args.empty() ? nullptr : emitRValue(c->Args[0].Value.get());
      if (v) {
        if (which == "retain")
          emitRetain(v, arg);
        else
          emitRelease(v, arg);
      }
      return nullptr;
    }
    // Raw storage keeps its books through these four, and they mean the
    // same thing under either memory model: what goes in is owned by the
    // slot, what comes out is owned by the caller.
    if (arg && (which == "store" || which == "take" || which == "drop_at" ||
                which == "replace")) {
      Expr *slotExpr = c->Args.empty() ? nullptr : c->Args[0].Value.get();
      Value *slot = slotExpr ? emitRValue(slotExpr) : nullptr;
      if (!slot)
        return nullptr;
      llvm::Type *ty = lower(arg);
      if (which == "store") {
        Expr *ve = c->Args.size() > 1 ? c->Args[1].Value.get() : nullptr;
        Value *v = ve ? emitRValue(ve) : nullptr;
        if (!v)
          return nullptr;
        takeOwnership(ve, v, arg);   // ARC retains; Zombie moves
        B->CreateStore(coerce(v, ve->Ty, arg), slot);
        return nullptr;
      }
      if (which == "take") {
        Value *v = B->CreateLoad(ty, slot, "taken");
        if (arg->isRefCounted())
          B->CreateStore(Constant::getNullValue(ty), slot);
        return track(v, arg);
      }
      if (which == "drop_at") {
        emitRelease(B->CreateLoad(ty, slot), arg);
        if (arg->isRefCounted())
          B->CreateStore(Constant::getNullValue(ty), slot);
        return nullptr;
      }
      // replace: the old value is the caller's, the new one the slot's.
      Expr *ve = c->Args.size() > 1 ? c->Args[1].Value.get() : nullptr;
      Value *v = ve ? emitRValue(ve) : nullptr;
      if (!v)
        return nullptr;
      takeOwnership(ve, v, arg);
      Value *old = B->CreateLoad(ty, slot, "replaced");
      B->CreateStore(coerce(v, ve->Ty, arg), slot);
      return track(old, arg);
    }
    if (arg && (which == "size_of" || which == "align_of")) {
      llvm::Type *lowered = lower(arg);
      const llvm::DataLayout &dl = M->getDataLayout();
      uint64_t v = which == "size_of"
                       ? dl.getTypeAllocSize(lowered).getFixedValue()
                       : dl.getABITypeAlign(lowered).value();
      return ConstantInt::get(lower(c->Ty), v);
    }
    // Structural hash and equality. The compiler knows the layout, so a
    // container can key on any type at all — no `Hashable` mark to bind, and
    // no `Display` standing in for one.
    // `hash` and `equals` take their operands as `&T`, so what arrives is a
    // borrow: the value behind it is what they work on. A handle borrow is
    // the value already; anything else is loaded from the address.
    auto behindBorrow = [&](Expr *e) -> Value * {
      Value *p = emitRValue(e);
      if (!p)
        return nullptr;
      Type *bt = e->Ty;
      if (bt && bt->is(TypeKind::Pointer) && !handleBorrow(bt))
        return B->CreateLoad(lower(bt->pointee()), p, "arg");
      return p;
    };
    if (arg && which == "hash") {
      Value *v = c->Args.empty() ? nullptr : behindBorrow(c->Args[0].Value.get());
      if (!v)
        return ConstantInt::get(lower(c->Ty), 0);
      return emitHash(v, arg, ConstantInt::get(B->getInt64Ty(), 0));
    }
    if (arg && which == "equals") {
      if (c->Args.size() < 2)
        return B->getInt1(false);
      Value *a = behindBorrow(c->Args[0].Value.get());
      Value *b = behindBorrow(c->Args[1].Value.get());
      if (!a || !b)
        return B->getInt1(false);
      return emitEquals(a, b, arg);
    }
    // The same question the compiler asks itself before emitting a retain, so
    // a container written in Rune can ask it too and agree by construction.
    if (arg && which == "is_counted")
      return ConstantInt::get(lower(c->Ty), arg->isRefCounted() ? 1 : 0);
    // A slice is an address and a count, and this is the one place they are
    // put together from parts rather than taken from an array: no copy, no
    // check, and the block's extent is the caller's promise.
    if (arg && which == "slice_of") {
      if (c->Args.size() < 2)
        return Constant::getNullValue(lower(c->Ty));
      Value *block = emitRValue(c->Args[0].Value.get());
      Value *count = emitRValue(c->Args[1].Value.get());
      if (!block || !count)
        return Constant::getNullValue(lower(c->Ty));
      Value *slice = UndefValue::get(lower(c->Ty));
      slice = B->CreateInsertValue(slice, block, 0);
      slice = B->CreateInsertValue(
          slice, B->CreateZExtOrTrunc(count, B->getInt64Ty()), 1);
      return slice;
    }
    // --- atomics ---------------------------------------------------------
    // Reference counting is the reason these exist: a count two threads may
    // touch has to be read-modify-written as one step. They are ordinary
    // intrinsics, so anything else needing one can use them as well.
    //
    // All of them are sequentially consistent. A weaker ordering would be
    // faster and much easier to get wrong, and nothing here is hot enough to
    // be worth that trade.
    if (which.compare(0, 7, "atomic_") == 0) {
      const auto ordering = llvm::AtomicOrdering::SequentiallyConsistent;
      if (c->Args.empty())
        return Constant::getNullValue(lower(c->Ty));
      Value *ptr = emitRValue(c->Args[0].Value.get());
      if (!ptr)
        return Constant::getNullValue(lower(c->Ty));

      if (which == "atomic_load") {
        llvm::Type *valueTy = lower(c->Ty);
        const llvm::Align align =
            M->getDataLayout().getABITypeAlign(valueTy);
        auto *load = B->CreateAlignedLoad(valueTy, ptr, align);
        load->setAtomic(ordering);
        return load;
      }
      if (which == "atomic_store") {
        if (c->Args.size() < 2)
          return nullptr;
        Value *v = emitRValue(c->Args[1].Value.get());
        const llvm::Align align =
            M->getDataLayout().getABITypeAlign(v->getType());
        auto *store = B->CreateAlignedStore(v, ptr, align);
        store->setAtomic(ordering);
        return nullptr;
      }
      if (which == "atomic_add" || which == "atomic_sub" ||
          which == "atomic_exchange") {
        if (c->Args.size() < 2)
          return Constant::getNullValue(lower(c->Ty));
        Value *operand = emitRValue(c->Args[1].Value.get());
        auto op = which == "atomic_add"   ? llvm::AtomicRMWInst::Add
                  : which == "atomic_sub" ? llvm::AtomicRMWInst::Sub
                                          : llvm::AtomicRMWInst::Xchg;
        const llvm::Align align =
            M->getDataLayout().getABITypeAlign(operand->getType());
        // Returns the value as it was *before* the operation, which is what
        // lets the thread that reached zero know that it did.
        return B->CreateAtomicRMW(op, ptr, operand, align, ordering);
      }
      if (which == "atomic_compare_exchange") {
        if (c->Args.size() < 3)
          return B->getInt1(false);
        Value *expected = emitRValue(c->Args[1].Value.get());
        Value *desired = emitRValue(c->Args[2].Value.get());
        const llvm::Align align =
            M->getDataLayout().getABITypeAlign(expected->getType());
        Value *pair = B->CreateAtomicCmpXchg(ptr, expected, desired, align,
                                             ordering, ordering);
        return B->CreateExtractValue(pair, 1, "swapped");
      }
    }
    // --- std::reflect ----------------------------------------------------
    // Everything the compiler already knows in order to lay a value out,
    // answered as a constant. None of these survives to run time.
    if (arg && which == "type_name")
      return emitStringLiteral(arg->toString(), false);
    if (arg && which == "type_id") {
      // Derived from the name, so the same type is the same id however the
      // program was split into objects.
      const std::string name = arg->toString();
      uint64_t h = 1469598103934665603ull;   // FNV-1a
      for (unsigned char c : name) {
        h ^= c;
        h *= 1099511628211ull;
      }
      return ConstantInt::get(lower(c->Ty), h);
    }
    if (arg && which == "stride_of") {
      // Rune's size already carries tail padding, so the stride equals it.
      // Kept apart because they are different questions.
      const llvm::DataLayout &dl = M->getDataLayout();
      return ConstantInt::get(lower(c->Ty),
                              dl.getTypeAllocSize(lower(arg)).getFixedValue());
    }
    if (arg && which == "kind_of")
      return emitKindOf(arg, c->Ty);
    if (arg && which == "field_count")
      return ConstantInt::get(lower(c->Ty), reflectFieldCount(arg));
    if (arg && (which == "field_name" || which == "field_type")) {
      int64_t index = 0;
      if (!c->Args.empty())
        if (const auto *lit = dyn_cast<IntLitExpr>(c->Args[0].Value.get()))
          index = static_cast<int64_t>(lit->Value);
        else {
          Diags.error(c->Args[0].Value->Range,
                      "the index has to be a literal, because the answer is "
                      "chosen while compiling")
              .note("write `reflect::{}<T>(0)` rather than passing a variable",
                    which == "field_name" ? "fieldName" : "fieldType")
              .code(506);
          return emitStringLiteral("", false);
        }
      return emitStringLiteral(
          reflectFieldText(arg, index, which == "field_type"), false);
    }
    if (arg && which == "offset_of") {
      std::string field;
      if (!c->Args.empty())
        if (const auto *lit = dyn_cast<StringLitExpr>(c->Args[0].Value.get()))
          field = lit->Value;
      return emitOffsetOf(c, arg, field);
    }
    if (arg && (which == "is_send" || which == "is_sync"))
      return B->getInt1(typeIsThreadSafe(arg, which == "is_sync"));
    if (arg && which == "conforms") {
      Type *markType = target->TypeArguments.size() > 1
                           ? target->TypeArguments[1]
                           : nullptr;
      return B->getInt1(reflectConforms(arg, markType, c->Range));
    }
    if (arg && which == "describe") {
      Value *v = c->Args.empty() ? nullptr : emitRValue(c->Args[0].Value.get());
      if (!v)
        return emitStringLiteral("", false);
      return track(emitDescribe(v, arg), c->Ty);
    }
    if (which == "asm" || which == "asm_value")
      return emitInlineAsm(c, which == "asm_value" ? arg : nullptr,
                           /*hasSideEffects=*/which == "asm");
    // `std::any` is a handful of these: the methods have no body because the
    // question they answer is about the object header, not about Rune values.
    if (which.compare(0, 4, "any_") == 0)
      if (Value *v = emitAnyIntrinsic(c, which, arg))
        return v;
    reportUnsupported(c->Range, "this intrinsic");
    return Constant::getNullValue(lower(c->Ty));
  }

  // Method call: the receiver becomes the first argument.
  if (auto *member = dyn_cast<MemberExpr>(c->Callee.get())) {
    // A call on a mark object goes through its dispatch table rather than to
    // any particular implementation.
    Type *receiverTy = member->Base->Ty;
    if (target && receiverTy && receiverTy->is(TypeKind::DynMark)) {
      MarkDecl *mark = receiverTy->mark();
      int slotIndex = -1;
      if (mark)
        for (size_t i = 0; i < mark->Methods.size(); ++i)
          if (mark->Methods[i].get() == target ||
              mark->Methods[i]->Name == target->Name)
            slotIndex = static_cast<int>(i);
      if (slotIndex < 0) {
        reportUnsupported(c->Range, "this mark-object call");
        return nullptr;
      }

      Value *object = emitRValue(member->Base.get());
      Value *data = B->CreateExtractValue(object, 0, "dyn.data");
      Value *vtable = B->CreateExtractValue(object, 1, "dyn.vtable");
      Value *slot = B->CreateInBoundsGEP(PtrTy, vtable, B->getInt32(slotIndex));
      Value *callee = B->CreateLoad(PtrTy, slot, "dyn.method");

      std::vector<llvm::Type *> paramTys{PtrTy};
      for (Type *p : target->Ty->params())
        paramTys.push_back(lower(p));
      auto *ft = FunctionType::get(lowerReturn(target->Ty->result()), paramTys,
                                   false);

      std::vector<Value *> args{data};
      for (Value *a : buildArguments(c, target, target->Ty->params(), false))
        args.push_back(a);
      return track(B->CreateCall(ft, callee, args), c->Ty);
    }

    if (target) {
      Value *self = nullptr;
      Type *selfParam = nullptr;
      for (const Param &p : target->Params)
        if (p.IsSelf)
          selfParam = p.Ty;

      if (isa<SuperExpr>(member->Base.get())) {
        // `super.method()` reuses the current instance.
        self = fs().SelfValue;
        if (!self && fs().Decl) {
          for (Param &p : fs().Decl->Params)
            if (p.IsSelf && p.Binding && fs().Slots.count(p.Binding))
              self = B->CreateLoad(lower(p.Ty), fs().Slots[p.Binding]);
        }
      } else if (selfParam && selfParam->is(TypeKind::Pointer)) {
        Type *baseTy = member->Base->Ty;
        self = baseTy->is(TypeKind::Pointer) ? emitRValue(member->Base.get())
                                             : emitLValue(member->Base.get());
      } else {
        self = emitRValue(member->Base.get());
        if (member->Base->Ty && member->Base->Ty->is(TypeKind::Pointer) &&
            selfParam && !selfParam->is(TypeKind::Pointer) &&
            !handleBorrow(member->Base->Ty))
          self = B->CreateLoad(lower(selfParam), self);
      }

      std::vector<Value *> args;
      if (self)
        args.push_back(self);
      for (Value *a : buildArguments(c, target, target->Ty->params(),
                                     target->IsVariadic))
        args.push_back(a);

      FunctionType *ft = functionTypeFor(target);
      Value *callee = nullptr;
      if (target->IsVirtual && target->VTableIndex >= 0 && !member->NonVirtual &&
          self) {
        // Dynamic dispatch: object -> typeinfo -> vtable -> slot.
        Value *typePtr = B->CreateLoad(
            PtrTy, B->CreateStructGEP(ObjectHeaderTy, self, 1), "typeinfo");
        Value *vtable = B->CreateLoad(
            PtrTy, B->CreateStructGEP(TypeInfoTy, typePtr, 4), "vtable");
        Value *slot = B->CreateInBoundsGEP(
            PtrTy, vtable, B->getInt32(target->VTableIndex));
        callee = B->CreateLoad(PtrTy, slot, "method");
      } else {
        callee = declareFunction(target);
      }
      Value *r = B->CreateCall(ft, callee, args);
      return track(r, c->Ty);
    }
  }

  if (target) {
    Function *f = declareFunction(target);
    std::vector<Value *> args =
        buildArguments(c, target, target->Ty->params(), target->IsVariadic);
    Value *r = B->CreateCall(f, args);
    if (c->Ty && c->Ty->isNever()) {
      // The callee does not come back; nothing after this point runs.
      B->CreateUnreachable();
      return nullptr;
    }
    return track(r, c->Ty);
  }

  Type *ft = c->Callee->Ty;

  // A bare function pointer: no environment, so the call is exactly what C
  // would emit.
  if (ft && ft->is(TypeKind::CFunction)) {
    Value *target = emitRValue(c->Callee.get());
    std::vector<llvm::Type *> paramTys;
    for (Type *p : ft->params())
      paramTys.push_back(lower(p));
    auto *cft = FunctionType::get(lowerReturn(ft->result()), paramTys,
                                  ft->isVariadicFunction());
    std::vector<Value *> args =
        buildArguments(c, nullptr, ft->params(), ft->isVariadicFunction());
    Value *r = B->CreateCall(cft, target, args);
    return ft->result()->isVoid() ? nullptr : track(r, c->Ty);
  }

  // Indirect call through a function value: { code, environment }.
  if (!ft || !ft->is(TypeKind::Function)) {
    reportUnsupported(c->Range, "this call");
    return nullptr;
  }
  Value *closure = emitRValue(c->Callee.get());
  Value *code = B->CreateExtractValue(closure, 0, "fn");
  Value *env = B->CreateExtractValue(closure, 1, "env");

  std::vector<llvm::Type *> paramTys{PtrTy};
  for (Type *p : ft->params())
    paramTys.push_back(lower(p));
  auto *llvmFt = FunctionType::get(lowerReturn(ft->result()), paramTys,
                                   ft->isVariadicFunction());

  std::vector<Value *> args{env};
  for (Value *a : buildArguments(c, nullptr, ft->params(),
                                 ft->isVariadicFunction()))
    args.push_back(a);
  Value *r = B->CreateCall(llvmFt, code, args);
  return track(r, c->Ty);
}

//===----------------------------------------------------------------------===//
// Operators
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Inline assembly
//===----------------------------------------------------------------------===//

/// Lowers `asm::run` and `asm::value` to an LLVM inline-assembly call.
///
/// The template and the constraints have to be literals — they are part of
/// what is being compiled rather than something it computes — and Sema has
/// already said so, so anything else arriving here is a compiler bug.
///
/// `hasSideEffects` is what tells LLVM whether the call may be dropped when
/// nothing uses its result, or merged with an identical one. `run` returns
/// nothing, so its whole purpose is its effects; `value` is a function of its
/// inputs and is left free to be optimised like one.
Value *CodeGen::emitInlineAsm(CallExpr *c, Type *resultType,
                              bool hasSideEffects) {
  auto literal = [&](size_t index) -> std::string {
    if (index >= c->Args.size())
      return {};
    const auto *lit = dyn_cast<StringLitExpr>(c->Args[index].Value.get());
    return lit ? lit->Value : std::string();
  };
  const std::string templateText = literal(0);
  const std::string constraints = literal(1);

  // Everything after the two literals is an operand, in the order written.
  std::vector<Value *> operands;
  std::vector<llvm::Type *> operandTypes;
  for (size_t i = 2; i < c->Args.size(); ++i) {
    Value *v = emitRValue(c->Args[i].Value.get());
    if (!v) {
      Diags.error(c->Args[i].Value->Range,
                  "this cannot be handed to inline assembly")
          .note("an operand has to be a value with a machine representation")
          .code(508);
      return resultType ? Constant::getNullValue(lower(resultType)) : nullptr;
    }
    operands.push_back(v);
    operandTypes.push_back(v->getType());
  }

  llvm::Type *ret = resultType && !resultType->isVoid() ? lower(resultType)
                                                        : B->getVoidTy();
  auto *fnTy = FunctionType::get(ret, operandTypes, /*isVarArg=*/false);

  // LLVM knows whether the constraints describe this signature — how many
  // operands there are, which of them are results, whether this target has
  // such registers. Asking first turns what would be an assertion inside the
  // back end into a diagnostic pointing at the call.
  if (llvm::Error err = llvm::InlineAsm::verify(fnTy, constraints)) {
    std::string detail = llvm::toString(std::move(err));
    Diags.error(c->Range, "these inline assembly constraints do not fit this call")
        .note("LLVM said: {}", detail.c_str())
        .note("constraints are comma separated with results first, one per "
              "operand: `\"=r,r\"` is a result in a register and one input "
              "in a register")
        .code(508);
    return ret->isVoidTy() ? nullptr : Constant::getNullValue(ret);
  }

  auto *callee =
      llvm::InlineAsm::get(fnTy, templateText, constraints, hasSideEffects);
  CallInst *call = B->CreateCall(callee, operands);
  call->addFnAttr(llvm::Attribute::NoUnwind);
  return ret->isVoidTy() ? nullptr : call;
}

//===----------------------------------------------------------------------===//
// Integer arithmetic
//
// `+`, `-` and `*` on a fixed-width integer can produce a value that does not
// fit. What happens then follows the build: a debug build (`-O0`) traps, the
// way a bounds check does, and an optimised build wraps. `--overflow-checks`
// and `--no-overflow-checks` say so explicitly. The `$wrapping*`,
// `$saturating*` and `$checked*` intrinsics ask for one behaviour by name and
// get it at every setting.
//===----------------------------------------------------------------------===//

std::pair<Value *, Value *>
CodeGen::emitIntArithWithOverflow(BinaryOp op, Value *l, Value *r, Type *t) {
  const bool isSigned = t->isSigned();
  Intrinsic::ID id;
  switch (op) {
  case BinaryOp::Add: id = isSigned ? Intrinsic::sadd_with_overflow
                                    : Intrinsic::uadd_with_overflow; break;
  case BinaryOp::Sub: id = isSigned ? Intrinsic::ssub_with_overflow
                                    : Intrinsic::usub_with_overflow; break;
  case BinaryOp::Mul: id = isSigned ? Intrinsic::smul_with_overflow
                                    : Intrinsic::umul_with_overflow; break;
  default:
    return {nullptr, nullptr};
  }
  Value *pair = B->CreateBinaryIntrinsic(id, l, r);
  return {B->CreateExtractValue(pair, 0), B->CreateExtractValue(pair, 1)};
}

Value *CodeGen::emitIntArith(BinaryOp op, Value *l, Value *r, Type *t,
                             OverflowMode mode, SourceRange range) {
  const bool isSigned = t->isSigned();
  auto plain = [&]() -> Value * {
    switch (op) {
    case BinaryOp::Add: return B->CreateAdd(l, r);
    case BinaryOp::Sub: return B->CreateSub(l, r);
    case BinaryOp::Mul: return B->CreateMul(l, r);
    default: return nullptr;
    }
  };
  if (mode == OverflowMode::Wrapping ||
      (mode == OverflowMode::Checked && !Opts.overflowChecksEnabled()))
    return plain();

  if (mode == OverflowMode::Saturating) {
    Intrinsic::ID id;
    switch (op) {
    case BinaryOp::Add: id = isSigned ? Intrinsic::sadd_sat : Intrinsic::uadd_sat; break;
    case BinaryOp::Sub: id = isSigned ? Intrinsic::ssub_sat : Intrinsic::usub_sat; break;
    case BinaryOp::Mul: {
      // There is no saturating multiply intrinsic; clamp the overflowing case
      // by hand. Signed overflow goes to the limit matching the sign of the
      // true product, which is the XOR of the operands' signs.
      auto [res, ovf] = emitIntArithWithOverflow(op, l, r, t);
      unsigned w = t->intWidth();
      Value *limit = nullptr;
      if (isSigned) {
        Value *neg = B->CreateICmpSLT(B->CreateXor(l, r),
                                      ConstantInt::get(l->getType(), 0));
        limit = B->CreateSelect(
            neg, ConstantInt::get(l->getType(), APInt::getSignedMinValue(w)),
            ConstantInt::get(l->getType(), APInt::getSignedMaxValue(w)));
      } else {
        limit = ConstantInt::get(l->getType(), APInt::getMaxValue(w));
      }
      return B->CreateSelect(ovf, limit, res);
    }
    default: return plain();
    }
    return B->CreateBinaryIntrinsic(id, l, r);
  }

  // Checked, and the build wants the trap.
  auto [res, ovf] = emitIntArithWithOverflow(op, l, r, t);
  if (!res)
    return plain();
  Function *f = fs().Fn;
  auto *okBB = BasicBlock::Create(*Ctx, "arith.ok", f);
  auto *failBB = BasicBlock::Create(*Ctx, "arith.overflow", f);
  B->CreateCondBr(ovf, failBB, okBB);
  B->SetInsertPoint(failBB);
  B->CreateCall(runtimeFn("rune_panic_overflow", B->getVoidTy(), {PtrTy, PtrTy}),
                {emitStringLiteral(binaryOpSpelling(op), /*asCString=*/true),
                 locationString(range)});
  B->CreateUnreachable();
  B->SetInsertPoint(okBB);
  return res;
}

Value *CodeGen::emitIntNeg(Value *v, Type *t, SourceRange range) {
  // Only a signed type has a value with no negation — its minimum — and only
  // a checked build cares.
  if (!t->isSigned() || !Opts.overflowChecksEnabled())
    return B->CreateNeg(v);
  Value *zero = ConstantInt::get(v->getType(), 0);
  auto [res, ovf] = emitIntArithWithOverflow(BinaryOp::Sub, zero, v, t);
  Function *f = fs().Fn;
  auto *okBB = BasicBlock::Create(*Ctx, "neg.ok", f);
  auto *failBB = BasicBlock::Create(*Ctx, "neg.overflow", f);
  B->CreateCondBr(ovf, failBB, okBB);
  B->SetInsertPoint(failBB);
  B->CreateCall(runtimeFn("rune_panic_overflow", B->getVoidTy(), {PtrTy, PtrTy}),
                {emitStringLiteral("-", /*asCString=*/true),
                 locationString(range)});
  B->CreateUnreachable();
  B->SetInsertPoint(okBB);
  return res;
}

Value *CodeGen::emitUnary(UnaryExpr *u) {
  if (auto *impl = u->OverloadResolved) {
    Function *f = declareFunction(impl);
    Type *selfParam = nullptr;
    for (const Param &p : impl->Params)
      if (p.IsSelf)
        selfParam = p.Ty;
    Value *self = selfParam && selfParam->is(TypeKind::Pointer)
                      ? emitLValue(u->Operand.get())
                      : emitRValue(u->Operand.get());
    return track(B->CreateCall(f, {self}), u->Ty);
  }

  Type *t = u->Operand->Ty;
  // `-128` written against an `i8` is one literal, not a negation of a value
  // that does not fit: fold it before any overflow check can see it.
  if (u->Op == UnaryOp::Neg && t->isInt())
    if (auto *lit = dyn_cast<IntLitExpr>(u->Operand.get()))
      return ConstantInt::get(lower(t), 0 - lit->Value);
  Value *v = emitRValue(u->Operand.get());
  switch (u->Op) {
  case UnaryOp::Neg:
    if (t->isFloat())
      return B->CreateFNeg(v);
    return t->isInt() ? emitIntNeg(v, t, u->Range) : B->CreateNeg(v);
  case UnaryOp::Not:
    return B->CreateNot(v);
  case UnaryOp::BitNot:
    return B->CreateNot(v);
  }
  return v;
}

Value *CodeGen::emitBinary(BinaryExpr *b) {
  // Short-circuiting operators need their own blocks.
  if (b->Op == BinaryOp::LogicalAnd || b->Op == BinaryOp::LogicalOr) {
    Function *f = fs().Fn;
    Value *lhs = emitRValue(b->LHS.get());
    BasicBlock *lhsBB = B->GetInsertBlock();
    auto *rhsBB = BasicBlock::Create(*Ctx, "logic.rhs", f);
    auto *doneBB = BasicBlock::Create(*Ctx, "logic.done", f);
    if (b->Op == BinaryOp::LogicalAnd)
      B->CreateCondBr(lhs, rhsBB, doneBB);
    else
      B->CreateCondBr(lhs, doneBB, rhsBB);

    B->SetInsertPoint(rhsBB);
    Value *rhs = emitRValue(b->RHS.get());
    rhsBB = B->GetInsertBlock();
    B->CreateBr(doneBB);

    B->SetInsertPoint(doneBB);
    PHINode *phi = B->CreatePHI(B->getInt1Ty(), 2);
    phi->addIncoming(B->getInt1(b->Op == BinaryOp::LogicalOr), lhsBB);
    phi->addIncoming(rhs, rhsBB);
    return phi;
  }

  if (b->Op == BinaryOp::Coalesce) {
    Function *f = fs().Fn;
    Type *optTy = b->LHS->Ty;
    Value *opt = emitRValue(b->LHS.get());
    int someIndex = variantIndexNamed(optTy, "Some");
    auto *someBB = BasicBlock::Create(*Ctx, "coalesce.some", f);
    auto *noneBB = BasicBlock::Create(*Ctx, "coalesce.none", f);
    auto *doneBB = BasicBlock::Create(*Ctx, "coalesce.done", f);
    B->CreateCondBr(emitVariantTest(opt, optTy, "Some"), someBB, noneBB);

    B->SetInsertPoint(someBB);
    Value *someVal = emitVariantPayload(opt, optTy, someIndex, 0, b->Ty);
    someBB = B->GetInsertBlock();
    B->CreateBr(doneBB);

    B->SetInsertPoint(noneBB);
    Value *noneVal = emitRValue(b->RHS.get());
    noneVal = coerce(noneVal, b->RHS->Ty, b->Ty);
    noneBB = B->GetInsertBlock();
    B->CreateBr(doneBB);

    B->SetInsertPoint(doneBB);
    PHINode *phi = B->CreatePHI(lower(b->Ty), 2);
    phi->addIncoming(someVal, someBB);
    phi->addIncoming(noneVal, noneBB);
    return phi;
  }

  // A user-provided overload wins over the builtin behaviour.
  if (auto *impl = b->OverloadResolved) {
    Function *f = declareFunction(impl);
    Type *selfParam = nullptr, *rhsParam = nullptr;
    for (const Param &p : impl->Params) {
      if (p.IsSelf)
        selfParam = p.Ty;
      else if (!rhsParam)
        rhsParam = p.Ty;
    }
    Value *self = selfParam && selfParam->is(TypeKind::Pointer)
                      ? emitLValue(b->LHS.get())
                      : emitRValue(b->LHS.get());
    Value *rhs = rhsParam && rhsParam->is(TypeKind::Pointer)
                     ? emitLValue(b->RHS.get())
                     : emitRValue(b->RHS.get());
    if (rhsParam && !rhsParam->is(TypeKind::Pointer))
      rhs = coerce(rhs, b->RHS->Ty, rhsParam);
    Value *r = B->CreateCall(f, {self, rhs});

    // `cmp` returns an ordering; turn it back into the boolean asked for.
    if (isComparison(b->Op) && impl->Ty && impl->Ty->result()->isInt()) {
      Value *zero = ConstantInt::get(r->getType(), 0);
      switch (b->Op) {
      case BinaryOp::Lt: return B->CreateICmpSLT(r, zero);
      case BinaryOp::Le: return B->CreateICmpSLE(r, zero);
      case BinaryOp::Gt: return B->CreateICmpSGT(r, zero);
      case BinaryOp::Ge: return B->CreateICmpSGE(r, zero);
      default: break;
      }
    }
    if (isComparison(b->Op) && impl->Ty && impl->Ty->result()->isBool()) {
      if (b->Op == BinaryOp::Ne)
        return B->CreateNot(r);
      return r;
    }
    return track(r, b->Ty);
  }

  Type *lt = b->LHS->Ty, *rt = b->RHS->Ty;

  // Strings.
  if (lt->is(TypeKind::String) && rt->is(TypeKind::String)) {
    Value *l = emitRValue(b->LHS.get());
    Value *r = emitRValue(b->RHS.get());
    if (b->Op == BinaryOp::Add)
      return track(B->CreateCall(
                       runtimeFn("rune_string_concat", PtrTy, {PtrTy, PtrTy}),
                       {l, r}),
                   Types.stringType());
    Value *cmp = B->CreateCall(
        runtimeFn("rune_string_compare", B->getInt32Ty(), {PtrTy, PtrTy}),
        {l, r});
    Value *zero = B->getInt32(0);
    switch (b->Op) {
    case BinaryOp::Eq: return B->CreateICmpEQ(cmp, zero);
    case BinaryOp::Ne: return B->CreateICmpNE(cmp, zero);
    case BinaryOp::Lt: return B->CreateICmpSLT(cmp, zero);
    case BinaryOp::Le: return B->CreateICmpSLE(cmp, zero);
    case BinaryOp::Gt: return B->CreateICmpSGT(cmp, zero);
    case BinaryOp::Ge: return B->CreateICmpSGE(cmp, zero);
    default: break;
    }
  }

  // Operands are computed in the result's type so widening happens once.
  Type *opTy = isComparison(b->Op) ? (lt->isNumeric() && rt->isNumeric()
                                          ? (lt->intWidth() >= rt->intWidth() ||
                                                     lt->isFloat()
                                                 ? lt
                                                 : rt)
                                          : lt)
                                   : b->Ty;
  if (isComparison(b->Op) && lt->isFloat())
    opTy = lt;
  else if (isComparison(b->Op) && rt->isFloat())
    opTy = rt;

  Value *l = emitRValue(b->LHS.get());
  Value *r = emitRValue(b->RHS.get());

  if (lt->is(TypeKind::Enum) && rt->is(TypeKind::Enum)) {
    // Simple enums compare by discriminant.
    Value *lAddr = createEntryAlloca(lower(lt), "enum.l");
    Value *rAddr = createEntryAlloca(lower(rt), "enum.r");
    B->CreateStore(l, lAddr);
    B->CreateStore(r, rAddr);
    Value *lTag = B->CreateLoad(B->getInt32Ty(),
                                B->CreateStructGEP(lower(lt), lAddr, 0));
    Value *rTag = B->CreateLoad(B->getInt32Ty(),
                                B->CreateStructGEP(lower(rt), rAddr, 0));
    return b->Op == BinaryOp::Eq ? B->CreateICmpEQ(lTag, rTag)
                                 : B->CreateICmpNE(lTag, rTag);
  }

  if (lt->isPointerLike() && rt->isPointerLike() && !lt->isNumeric()) {
    Value *li = B->CreatePtrToInt(l, B->getInt64Ty());
    Value *ri = B->CreatePtrToInt(r, B->getInt64Ty());
    return b->Op == BinaryOp::Eq ? B->CreateICmpEQ(li, ri)
                                 : B->CreateICmpNE(li, ri);
  }

  l = coerce(l, lt, opTy);
  // Shift counts keep the left operand's type but need not match otherwise.
  if (b->Op == BinaryOp::Shl || b->Op == BinaryOp::Shr)
    r = coerce(r, rt, opTy);
  else
    r = coerce(r, rt, opTy);

  const bool isFloat = opTy->isFloat();
  const bool isSigned = opTy->isInt() ? opTy->isSigned() : true;

  // Integer division and remainder trap on a zero divisor.
  if ((b->Op == BinaryOp::Div || b->Op == BinaryOp::Rem) && !isFloat &&
      Opts.Safety == SafetyLevel::Full) {
    Function *f = fs().Fn;
    auto *okBB = BasicBlock::Create(*Ctx, "div.ok", f);
    auto *failBB = BasicBlock::Create(*Ctx, "div.zero", f);
    B->CreateCondBr(B->CreateICmpEQ(r, Constant::getNullValue(r->getType())),
                    failBB, okBB);
    B->SetInsertPoint(failBB);
    B->CreateCall(runtimeFn("rune_panic_div_zero", B->getVoidTy(), {PtrTy}),
                  {locationString(b->OpRange)});
    B->CreateUnreachable();
    B->SetInsertPoint(okBB);
  }

  switch (b->Op) {
  case BinaryOp::Add:
  case BinaryOp::Sub:
  case BinaryOp::Mul:
    if (isFloat)
      return b->Op == BinaryOp::Add ? B->CreateFAdd(l, r)
             : b->Op == BinaryOp::Sub ? B->CreateFSub(l, r)
                                      : B->CreateFMul(l, r);
    if (opTy->isInt())
      return emitIntArith(b->Op, l, r, opTy, OverflowMode::Checked, b->OpRange);
    return b->Op == BinaryOp::Add ? B->CreateAdd(l, r)
           : b->Op == BinaryOp::Sub ? B->CreateSub(l, r)
                                    : B->CreateMul(l, r);
  case BinaryOp::Div:
    return isFloat ? B->CreateFDiv(l, r)
                   : (isSigned ? B->CreateSDiv(l, r) : B->CreateUDiv(l, r));
  case BinaryOp::Rem:
    return isFloat ? B->CreateFRem(l, r)
                   : (isSigned ? B->CreateSRem(l, r) : B->CreateURem(l, r));
  case BinaryOp::BitAnd: return B->CreateAnd(l, r);
  case BinaryOp::BitOr: return B->CreateOr(l, r);
  case BinaryOp::BitXor: return B->CreateXor(l, r);
  case BinaryOp::Shl: return B->CreateShl(l, r);
  case BinaryOp::Shr:
    return isSigned ? B->CreateAShr(l, r) : B->CreateLShr(l, r);
  case BinaryOp::Eq:
    return isFloat ? B->CreateFCmpOEQ(l, r) : B->CreateICmpEQ(l, r);
  case BinaryOp::Ne:
    // Unordered, so that `a != b` is exactly `!(a == b)`. NaN compares equal
    // to nothing, itself included, which is what makes `v != v` the test for
    // one — and an *ordered* `!=` would answer false there and break it.
    return isFloat ? B->CreateFCmpUNE(l, r) : B->CreateICmpNE(l, r);
  case BinaryOp::Lt:
    return isFloat ? B->CreateFCmpOLT(l, r)
                   : (isSigned ? B->CreateICmpSLT(l, r) : B->CreateICmpULT(l, r));
  case BinaryOp::Le:
    return isFloat ? B->CreateFCmpOLE(l, r)
                   : (isSigned ? B->CreateICmpSLE(l, r) : B->CreateICmpULE(l, r));
  case BinaryOp::Gt:
    return isFloat ? B->CreateFCmpOGT(l, r)
                   : (isSigned ? B->CreateICmpSGT(l, r) : B->CreateICmpUGT(l, r));
  case BinaryOp::Ge:
    return isFloat ? B->CreateFCmpOGE(l, r)
                   : (isSigned ? B->CreateICmpSGE(l, r) : B->CreateICmpUGE(l, r));
  default:
    break;
  }
  return l;
}

Value *CodeGen::emitAssign(AssignExpr *a) {
  if (a->DeclaresBinding && a->DeclaredVar) {
    Value *slot = declareLocalSlot(a->DeclaredVar, a->DeclaredVar->Name);
    emitInto(a->RHS.get(), slot, a->DeclaredVar->Ty);
    return nullptr;
  }

  // `*handle = value`, and `*handle += value`, on a type that overloads
  // `deref`: read through the getter into a scratch slot, let the ordinary
  // store machinery work on that, then hand the result to the setter. The
  // receiver is evaluated exactly once.
  if (a->DerefSetImpl) {
    auto *deref = cast<DerefExpr>(a->LHS.get());
    FunctionDecl *setter = a->DerefSetImpl;
    FunctionDecl *getter = deref->OverloadResolved;
    Type *valueTy = a->LHS->Ty;
    Type *selfParam = nullptr;
    for (const Param &p : setter->Params)
      if (p.IsSelf)
        selfParam = p.Ty;
    Value *self = selfParam && selfParam->is(TypeKind::Pointer)
                      ? emitLValue(deref->Operand.get())
                      : emitRValue(deref->Operand.get());
    if (!self || !valueTy)
      return nullptr;

    Value *scratch = createEntryAlloca(lower(valueTy), "deref.value");
    if (valueTy->isRefCounted())
      B->CreateStore(Constant::getNullValue(lower(valueTy)), scratch);
    if (a->Op != AssignOp::Assign && getter) {
      // The getter returns owned, so the scratch slot already holds a
      // reference; retaining again would strand one.
      B->CreateStore(B->CreateCall(declareFunction(getter), {self}), scratch);
    }
    emitAssignInto(a, scratch, valueTy);
    Value *result = B->CreateLoad(lower(valueTy), scratch);
    B->CreateCall(declareFunction(setter), {self, result});
    emitRelease(result, valueTy);
    return nullptr;
  }

  // `buffer[i] = value`, and `buffer[i] += value`, on a type that overloads
  // `index`: read through the getter into a scratch slot, let the ordinary
  // store machinery work on that, then hand the result to the setter. The
  // receiver and the index are each evaluated once.
  if (a->IndexSetImpl) {
    auto *idx = cast<IndexExpr>(a->LHS.get());
    FunctionDecl *setter = a->IndexSetImpl;
    FunctionDecl *getter = idx->OverloadResolved;
    Type *valueTy = a->LHS->Ty;
    Type *selfParam = nullptr, *indexParam = nullptr;
    size_t seen = 0;
    for (const Param &p : setter->Params) {
      if (p.IsSelf) { selfParam = p.Ty; continue; }
      if (++seen == 1) indexParam = p.Ty;
    }
    Value *self = selfParam && selfParam->is(TypeKind::Pointer)
                      ? emitLValue(idx->Base.get())
                      : emitRValue(idx->Base.get());
    Value *where = emitRValue(idx->Index.get());
    if (where && indexParam)
      where = coerce(where, idx->Index->Ty, indexParam);
    if (!self || !where || !valueTy)
      return nullptr;

    Value *scratch = createEntryAlloca(lower(valueTy), "index.value");
    if (valueTy->isRefCounted())
      B->CreateStore(Constant::getNullValue(lower(valueTy)), scratch);
    if (a->Op != AssignOp::Assign && getter) {
      // The getter returns owned, so the slot already holds a reference.
      B->CreateStore(B->CreateCall(declareFunction(getter), {self, where}),
                     scratch);
    }
    emitAssignInto(a, scratch, valueTy);
    Value *result = B->CreateLoad(lower(valueTy), scratch);
    B->CreateCall(declareFunction(setter), {self, where, result});
    emitRelease(result, valueTy);
    return nullptr;
  }

  // Writing a weak field targets the bare slot itself, not the Option that
  // reading it produces, so ask for the address directly.
  Value *slot = nullptr;
  if (auto *lhsMember = dyn_cast<MemberExpr>(a->LHS.get())) {
    Type *b = lhsMember->Base->Ty;
    while (b && b->is(TypeKind::Pointer))
      b = b->pointee();
    if (weakFieldOf(lhsMember, b))
      slot = emitMemberAddress(lhsMember);
  }
  if (!slot)
    slot = emitLValue(a->LHS.get());
  if (!slot)
    return nullptr;
  Type *lt = a->LHS->Ty;

  // Writing a weak field registers the slot so the runtime can empty it later.
  if (auto *member = dyn_cast<MemberExpr>(a->LHS.get())) {
    Type *base = member->Base->Ty;
    while (base && base->is(TypeKind::Pointer))
      base = base->pointee();
    if (FieldDecl *weak = weakFieldOf(member, base)) {
      Value *incoming = emitRValue(a->RHS.get());
      Type *rt = a->RHS->Ty;
      Value *target = nullptr;
      if (isOptionType(rt)) {
        // Unwrap: `None` stores a null, which unregisters the slot.
        int someIndex = variantIndexNamed(rt, "Some");
        Function *f = fs().Fn;
        Value *tmp = createEntryAlloca(PtrTy, "weak.target");
        B->CreateStore(ConstantPointerNull::get(PtrTy), tmp);
        auto *someBB = BasicBlock::Create(*Ctx, "weak.some", f);
        auto *doneBB = BasicBlock::Create(*Ctx, "weak.set", f);
        B->CreateCondBr(emitVariantTest(incoming, rt, "Some"), someBB, doneBB);
        B->SetInsertPoint(someBB);
        B->CreateStore(emitVariantPayload(incoming, rt, someIndex, 0,
                                          weak->WeakTarget),
                       tmp);
        B->CreateBr(doneBB);
        B->SetInsertPoint(doneBB);
        target = B->CreateLoad(PtrTy, tmp);
      } else {
        target = incoming;
      }
      B->CreateCall(runtimeFn("rune_weak_store", B->getVoidTy(), {PtrTy, PtrTy}),
                    {slot, target});
      return nullptr;
    }
  }

  return emitAssignInto(a, slot, lt);
}

/// The store half of an assignment, once the destination is a real slot.
Value *CodeGen::emitAssignInto(AssignExpr *a, Value *slot, Type *lt) {
  if (a->Op == AssignOp::Assign) {
    bool raw = false;
    if (auto *idx = dyn_cast<IndexExpr>(a->LHS.get()))
      raw = idx->ThroughRawPointer;
    else if (auto *d = dyn_cast<DerefExpr>(a->LHS.get()))
      raw = !d->OverloadResolved && d->Operand->Ty &&
            d->Operand->Ty->isRawPointer();
    emitInto(a->RHS.get(), slot, lt, raw);
    return nullptr;
  }

  // Compound assignment via an operator overload.
  if (auto *impl = a->OperatorImpl) {
    Function *f = declareFunction(impl);
    Type *selfParam = nullptr, *rhsParam = nullptr;
    for (const Param &p : impl->Params) {
      if (p.IsSelf) selfParam = p.Ty;
      else if (!rhsParam) rhsParam = p.Ty;
    }
    Value *self = selfParam && selfParam->is(TypeKind::Pointer)
                      ? slot
                      : B->CreateLoad(lower(lt), slot);
    Value *rhs = rhsParam && rhsParam->is(TypeKind::Pointer)
                     ? emitLValue(a->RHS.get())
                     : emitRValue(a->RHS.get());
    if (rhsParam && !rhsParam->is(TypeKind::Pointer))
      rhs = coerce(rhs, a->RHS->Ty, rhsParam);
    // The call hands back a result the caller owns, so there is nothing to
    // retain: the slot simply takes over that reference, and the value the
    // slot used to hold is released. Retaining here as well left every
    // `x += y` through an overload one reference too high.
    Value *r = B->CreateCall(f, {self, rhs});
    emitRelease(B->CreateLoad(lower(lt), slot), lt);
    B->CreateStore(coerce(r, impl->Ty->result(), lt), slot);
    return nullptr;
  }

  Value *cur = B->CreateLoad(lower(lt), slot);
  Value *rhs = emitRValue(a->RHS.get());
  BinaryOp op = assignOpToBinary(a->Op);

  if (lt->is(TypeKind::String) && op == BinaryOp::Add) {
    Value *joined = B->CreateCall(
        runtimeFn("rune_string_concat", PtrTy, {PtrTy, PtrTy}), {cur, rhs});
    emitRelease(cur, lt);
    B->CreateStore(joined, slot);
    return nullptr;
  }

  rhs = coerce(rhs, a->RHS->Ty, lt);
  const bool isFloat = lt->isFloat();
  const bool isSigned = lt->isInt() ? lt->isSigned() : true;

  if ((op == BinaryOp::Div || op == BinaryOp::Rem) && !isFloat &&
      Opts.Safety == SafetyLevel::Full) {
    Function *f = fs().Fn;
    auto *okBB = BasicBlock::Create(*Ctx, "div.ok", f);
    auto *failBB = BasicBlock::Create(*Ctx, "div.zero", f);
    B->CreateCondBr(B->CreateICmpEQ(rhs, Constant::getNullValue(rhs->getType())),
                    failBB, okBB);
    B->SetInsertPoint(failBB);
    B->CreateCall(runtimeFn("rune_panic_div_zero", B->getVoidTy(), {PtrTy}),
                  {locationString(a->OpRange)});
    B->CreateUnreachable();
    B->SetInsertPoint(okBB);
  }

  Value *result = nullptr;
  switch (op) {
  case BinaryOp::Add:
  case BinaryOp::Sub:
  case BinaryOp::Mul:
    if (isFloat)
      result = op == BinaryOp::Add ? B->CreateFAdd(cur, rhs)
               : op == BinaryOp::Sub ? B->CreateFSub(cur, rhs)
                                     : B->CreateFMul(cur, rhs);
    else if (lt->isInt())
      result = emitIntArith(op, cur, rhs, lt, OverflowMode::Checked, a->OpRange);
    else
      result = op == BinaryOp::Add ? B->CreateAdd(cur, rhs)
               : op == BinaryOp::Sub ? B->CreateSub(cur, rhs)
                                     : B->CreateMul(cur, rhs);
    break;
  case BinaryOp::Div:
    result = isFloat ? B->CreateFDiv(cur, rhs)
                     : (isSigned ? B->CreateSDiv(cur, rhs) : B->CreateUDiv(cur, rhs));
    break;
  case BinaryOp::Rem:
    result = isFloat ? B->CreateFRem(cur, rhs)
                     : (isSigned ? B->CreateSRem(cur, rhs) : B->CreateURem(cur, rhs));
    break;
  case BinaryOp::BitAnd: result = B->CreateAnd(cur, rhs); break;
  case BinaryOp::BitOr: result = B->CreateOr(cur, rhs); break;
  case BinaryOp::BitXor: result = B->CreateXor(cur, rhs); break;
  case BinaryOp::Shl: result = B->CreateShl(cur, rhs); break;
  case BinaryOp::Shr:
    result = isSigned ? B->CreateAShr(cur, rhs) : B->CreateLShr(cur, rhs);
    break;
  default:
    result = cur;
    break;
  }
  B->CreateStore(result, slot);
  return nullptr;
}

Value *CodeGen::emitInto(IntoExpr *e) {
  FunctionDecl *impl = e->Conversion;
  if (!impl) {
    reportUnsupported(e->Range, "this conversion");
    return nullptr;
  }
  Function *fn = declareFunction(impl);
  Type *selfParam = nullptr;
  for (const Param &p : impl->Params)
    if (p.IsSelf)
      selfParam = p.Ty;
  Value *self = selfParam && selfParam->is(TypeKind::Pointer)
                    ? emitLValue(e->Operand.get())
                    : emitRValue(e->Operand.get());
  if (!self)
    return Constant::getNullValue(lower(e->Ty));
  return track(B->CreateCall(fn, {self}), e->Ty);
}

Value *CodeGen::emitCast(CastExpr *c) {
  if (c->IsFunctionAddress) {
    auto *ref = cast<DeclRefExpr>(c->Operand.get());
    return declareFunction(cast<FunctionDecl>(ref->Resolved));
  }
  Value *v = emitRValue(c->Operand.get());
  Type *from = c->Operand->Ty;
  Type *to = c->Ty;
  if (from->is(TypeKind::String) && to->is(TypeKind::CString))
    return B->CreateCall(runtimeFn("rune_string_cstr", PtrTy, {PtrTy}), {v});
  if (from->is(TypeKind::CString) && to->is(TypeKind::String))
    return track(
        B->CreateCall(runtimeFn("rune_string_from_cstr", PtrTy, {PtrTy}), {v}),
        Types.stringType());
  return coerce(v, from, to);
}

Value *CodeGen::emitTry(TryExpr *t) {
  Type *operandTy = t->Operand->Ty;
  Value *value = emitRValue(t->Operand.get());
  // Under Zombie the whole value moves into the `?`: its error leaves with
  // the function, its payload becomes this expression's own temporary.
  if (zombie())
    takeOwnership(t->Operand.get(), value, operandTy);
  const bool isOption = isOptionType(operandTy);
  const char *goodName = isOption ? "Some" : "Ok";
  int goodIndex = variantIndexNamed(operandTy, goodName);

  Function *f = fs().Fn;
  auto *goodBB = BasicBlock::Create(*Ctx, "try.value", f);
  auto *earlyBB = BasicBlock::Create(*Ctx, "try.early", f);
  B->CreateCondBr(emitVariantTest(value, operandTy, goodName), goodBB, earlyBB);

  // The empty or failing case leaves the function with the same shape of
  // value: `None` for an Option, and the original `Err` for a Result.
  B->SetInsertPoint(earlyBB);
  if (fs().ReturnSlot && fs().ReturnType) {
    Type *retTy = fs().ReturnType;
    Value *outgoing = nullptr;
    if (isOption) {
      outgoing = emitEnumVariant(retTy, variantIndexNamed(retTy, "None"), {});
    } else {
      Type *errTy = resultError(operandTy);
      Type *outErrTy = resultError(retTy);
      int errIndex = variantIndexNamed(operandTy, "Err");
      Value *err = emitVariantPayload(value, operandTy, errIndex, 0, errTy);
      if (FunctionDecl *conv = t->ErrorConversion) {
        // Through the program's own `convert`. The error is still owned by
        // the operand, so it is handed over borrowed — by address when the
        // binding takes `&self`, by value otherwise — and what comes back
        // is a fresh value the return slot takes as it is.
        Function *fn = declareFunction(conv);
        Type *selfParam = nullptr;
        for (const Param &p : conv->Params)
          if (p.IsSelf)
            selfParam = p.Ty;
        Value *self = err;
        if (selfParam && selfParam->is(TypeKind::Pointer)) {
          Value *slot = createEntryAlloca(lower(errTy), "err");
          B->CreateStore(err, slot);
          self = slot;
        }
        err = B->CreateCall(fn, {self});
      } else {
        // Carried across as it is, widened when the types differ.
        emitRetain(err, errTy);
        if (t->ErrorCoerces && outErrTy)
          err = coerce(err, errTy, outErrTy);
      }
      outgoing =
          emitEnumVariant(retTy, variantIndexNamed(retTy, "Err"), {err});
    }
    emitRelease(B->CreateLoad(lower(retTy), fs().ReturnSlot), retTy);
    B->CreateStore(outgoing, fs().ReturnSlot);
  }
  emitStatementCleanup(/*consume=*/false);
  emitAllScopeCleanups(0);
  B->CreateBr(fs().ReturnBlock);

  B->SetInsertPoint(goodBB);
  Value *payload = emitVariantPayload(value, operandTy, goodIndex, 0, t->Ty);
  // The payload belongs to the value we just took it out of, which the
  // statement still owns, so hand it back borrowed — under Zombie the
  // statement owns the payload itself now, until something adopts it.
  if (zombie())
    return track(payload, t->Ty);
  return payload;
}

//===----------------------------------------------------------------------===//
// Control flow
//===----------------------------------------------------------------------===//

Value *CodeGen::emitIf(IfExpr *i, Value *slot, Type *slotType) {
  TempScope ownTemps(*this);
  Function *f = fs().Fn;
  auto *thenBB = BasicBlock::Create(*Ctx, "if.then", f);
  auto *elseBB = BasicBlock::Create(*Ctx, "if.else", f);
  auto *doneBB = BasicBlock::Create(*Ctx, "if.done", f);

  if (i->BindingPat) {
    // `if value is Pattern { ... }` tests and binds in one step.
    Type *condTy = i->Cond->Ty;
    Value *addr = nullptr;
    fs().Scopes.push_back(LexicalScope{});
    if (condTy->is(TypeKind::Pointer)) {
      addr = emitRValue(i->Cond.get());
      condTy = condTy->pointee();
      while (condTy->is(TypeKind::Pointer)) {
        addr = B->CreateLoad(PtrTy, addr);
        condTy = condTy->pointee();
      }
    } else {
      addr = createEntryAlloca(lower(condTy), "if.subject");
      if (condTy->isRefCounted())
        B->CreateStore(Constant::getNullValue(lower(condTy)), addr);
      emitInto(i->Cond.get(), addr, condTy);
      fs().Scopes.back().Locals.push_back({addr, condTy});
      // The subject now owns its own reference, so whatever computing it
      // created can go. The scope releases the subject itself at the end.
      emitStatementCleanup();
    }
    emitPatternTest(i->BindingPat.get(), addr, condTy, elseBB);
    ensureTerminated(thenBB);
    B->SetInsertPoint(thenBB);
    emitBlock(i->Then.get(), slot, slotType);
    emitScopeCleanup(fs().Scopes.size() - 1);
    fs().Scopes.pop_back();
    ensureTerminated(doneBB);
  } else {
    Value *cond = emitRValue(i->Cond.get());
    emitStatementCleanup();
    B->CreateCondBr(cond, thenBB, elseBB);
    B->SetInsertPoint(thenBB);
    emitBlock(i->Then.get(), slot, slotType);
    ensureTerminated(doneBB);
  }

  B->SetInsertPoint(elseBB);
  if (i->Else) {
    if (slot && slotType && !slotType->isVoid())
      emitInto(i->Else.get(), slot, slotType);
    else
      emitRValue(i->Else.get());
    emitStatementCleanup();
  }
  ensureTerminated(doneBB);

  B->SetInsertPoint(doneBB);
  return nullptr;
}

Value *CodeGen::emitWhile(WhileExpr *w) {
  TempScope ownTemps(*this);
  Function *f = fs().Fn;
  auto *condBB = BasicBlock::Create(*Ctx, "while.cond", f);
  auto *bodyBB = BasicBlock::Create(*Ctx, "while.body", f);
  auto *doneBB = BasicBlock::Create(*Ctx, "while.done", f);

  B->CreateBr(condBB);
  B->SetInsertPoint(condBB);

  size_t bindDepth = 0;
  if (w->BindingPat) {
    // `while value is Some(v)`: evaluate the subject, test the pattern, and
    // bind for this turn. The scope is entered here and left at the bottom of
    // the body, so each iteration gets its own bindings and releases them.
    Type *condTy = w->Cond->Ty;
    fs().Scopes.push_back(LexicalScope{});
    bindDepth = fs().Scopes.size();
    Value *addr = nullptr;
    if (condTy->is(TypeKind::Pointer)) {
      addr = emitRValue(w->Cond.get());
      condTy = condTy->pointee();
      while (condTy->is(TypeKind::Pointer)) {
        addr = B->CreateLoad(PtrTy, addr);
        condTy = condTy->pointee();
      }
    } else {
      addr = createEntryAlloca(lower(condTy), "while.subject");
      if (condTy->isRefCounted())
        B->CreateStore(Constant::getNullValue(lower(condTy)), addr);
      emitInto(w->Cond.get(), addr, condTy);
      fs().Scopes.back().Locals.push_back({addr, condTy});
      emitStatementCleanup();
    }
    auto *failBB = BasicBlock::Create(*Ctx, "while.nomatch", f);
    emitPatternTest(w->BindingPat.get(), addr, condTy, failBB);
    ensureTerminated(bodyBB);
    // The pattern said no: give the subject back before leaving.
    B->SetInsertPoint(failBB);
    emitScopeCleanup(bindDepth - 1);
    ensureTerminated(doneBB);
  } else {
    Value *cond = emitRValue(w->Cond.get());
    emitStatementCleanup();
    B->CreateCondBr(cond, bodyBB, doneBB);
  }

  B->SetInsertPoint(bodyBB);
  LoopFrame frame;
  frame.Continue = condBB;
  frame.Break = doneBB;
  frame.Label = w->Label;
  frame.ScopeDepth = fs().Scopes.size();
  fs().Loops.push_back(frame);
  emitBlock(w->Body.get(), nullptr, nullptr);
  fs().Loops.pop_back();
  if (bindDepth) {
    emitScopeCleanup(bindDepth - 1);
    fs().Scopes.pop_back();
  }
  ensureTerminated(condBB);

  B->SetInsertPoint(doneBB);
  return nullptr;
}

Value *CodeGen::emitLoop(LoopExpr *l, Value *slot, Type *slotType) {
  TempScope ownTemps(*this);
  Function *f = fs().Fn;
  auto *bodyBB = BasicBlock::Create(*Ctx, "loop.body", f);
  auto *doneBB = BasicBlock::Create(*Ctx, "loop.done", f);

  B->CreateBr(bodyBB);
  B->SetInsertPoint(bodyBB);
  LoopFrame frame;
  frame.Continue = bodyBB;
  frame.Break = doneBB;
  frame.ResultSlot = slot;
  frame.ResultType = slotType;
  frame.Label = l->Label;
  frame.ScopeDepth = fs().Scopes.size();
  fs().Loops.push_back(frame);
  emitBlock(l->Body.get(), nullptr, nullptr);
  fs().Loops.pop_back();
  ensureTerminated(bodyBB);

  B->SetInsertPoint(doneBB);
  return nullptr;
}

/// The receiver a method wants: the slot's address for `&self` / `&var self`,
/// and the value itself for a receiver taken by value.
Value *CodeGen::selfArgumentFor(FunctionDecl *m, Value *slot, Type *slotType) {
  Type *selfParam = nullptr;
  for (const Param &p : m->Params)
    if (p.IsSelf)
      selfParam = p.Ty;
  if (selfParam && selfParam->is(TypeKind::Pointer))
    return slot;
  return B->CreateLoad(lower(slotType), slot);
}

/// `for value in iterator`, driven by `Iterator::next` rather than by an
/// index. The iterator lives in a slot of the loop's own, because `next`
/// takes `&var self` and because the thing it walks has to outlive the walk —
/// `for v in makeVector()` would otherwise free the vector before the first
/// turn.
Value *CodeGen::emitIteratorFor(ForExpr *f) {
  TempScope ownTemps(*this);
  Function *fn = fs().Fn;
  fs().Scopes.push_back(LexicalScope{});
  const size_t loopDepth = fs().Scopes.size();

  Type *iterTy = f->IterType;
  Type *optTy = f->NextResult;
  Type *elemTy = f->Binding->Ty;

  Value *iterSlot = createEntryAlloca(lower(iterTy), "iter");
  if (iterTy->isRefCounted())
    B->CreateStore(Constant::getNullValue(lower(iterTy)), iterSlot);

  if (f->IterateMethod) {
    // The sequence is a container. Keep it alive in a slot of its own for as
    // long as the cursor over it exists, then ask it for that cursor once.
    Type *seqTy = f->Sequence->Ty;
    Value *seqSlot = createEntryAlloca(lower(seqTy), "seq");
    if (seqTy->isRefCounted())
      B->CreateStore(Constant::getNullValue(lower(seqTy)), seqSlot);
    Value *sv = emitRValue(f->Sequence.get());
    if (zombie()) {
      // The container is borrowed for the loop when it is somebody's
      // place, and owned by the loop when it was made for it.
      if (movedPlaceOf(f->Sequence.get())) {
        B->CreateStore(sv, seqSlot);
      } else {
        adopt(sv);
        B->CreateStore(sv, seqSlot);
        fs().Scopes.back().Locals.push_back({seqSlot, seqTy});
      }
    } else {
      emitRetain(sv, seqTy);
      B->CreateStore(sv, seqSlot);
      fs().Scopes.back().Locals.push_back({seqSlot, seqTy});
    }

    Value *self = selfArgumentFor(f->IterateMethod, seqSlot, seqTy);
    B->CreateStore(B->CreateCall(functionTypeFor(f->IterateMethod),
                                 declareFunction(f->IterateMethod), {self}),
                   iterSlot);
  } else {
    // The sequence is the cursor. The loop advances a copy of it, which is
    // what a value type means; a class iterator is shared, and advances.
    // Under Zombie the cursor moves into the loop.
    Value *iv = emitRValue(f->Sequence.get());
    if (zombie())
      takeOwnership(f->Sequence.get(), iv, iterTy);
    else
      emitRetain(iv, iterTy);
    B->CreateStore(iv, iterSlot);
  }
  fs().Scopes.back().Locals.push_back({iterSlot, iterTy});
  emitStatementCleanup();

  // What `next` hands back each turn. It is owned, and released either at the
  // end of the turn that consumed it or on the way out.
  Value *optSlot = createEntryAlloca(lower(optTy), "iter.step");
  if (optTy->isRefCounted())
    B->CreateStore(Constant::getNullValue(lower(optTy)), optSlot);

  auto *condBB = BasicBlock::Create(*Ctx, "for.next", fn);
  auto *bodyBB = BasicBlock::Create(*Ctx, "for.body", fn);
  auto *spentBB = BasicBlock::Create(*Ctx, "for.spent", fn);
  auto *doneBB = BasicBlock::Create(*Ctx, "for.done", fn);

  B->CreateBr(condBB);
  B->SetInsertPoint(condBB);
  Value *self = selfArgumentFor(f->NextMethod, iterSlot, iterTy);
  Value *step = B->CreateCall(functionTypeFor(f->NextMethod),
                              declareFunction(f->NextMethod), {self});
  B->CreateStore(step, optSlot);
  B->CreateCondBr(emitVariantTest(step, optTy, "Some"), bodyBB, spentBB);

  // The turn that ends the loop still produced a value; release it here,
  // because no iteration scope will.
  B->SetInsertPoint(spentBB);
  emitRelease(B->CreateLoad(lower(optTy), optSlot), optTy);
  B->CreateStore(Constant::getNullValue(lower(optTy)), optSlot);
  B->CreateBr(doneBB);

  B->SetInsertPoint(bodyBB);
  fs().Scopes.push_back(LexicalScope{});
  const size_t iterationDepth = fs().Scopes.size();
  // Both the step and the element belong to this turn, so `break` and
  // `continue` unwind them along with anything the body declared.
  fs().Scopes.back().Locals.push_back({optSlot, optTy});

  Value *elemSlot = createEntryAlloca(lower(elemTy), "elem");
  if (elemTy->isRefCounted())
    B->CreateStore(Constant::getNullValue(lower(elemTy)), elemSlot);
  Value *payload = emitVariantPayload(B->CreateLoad(lower(optTy), optSlot),
                                      optTy, variantIndexNamed(optTy, "Some"),
                                      0, elemTy);
  if (zombie()) {
    // The element moves out of the step; the step then holds nothing.
    if (optTy->isRefCounted())
      B->CreateStore(Constant::getNullValue(lower(optTy)), optSlot);
  } else {
    emitRetain(payload, elemTy);
  }
  B->CreateStore(payload, elemSlot);
  fs().Scopes.back().Locals.push_back({elemSlot, elemTy});
  emitPatternBind(f->Binding.get(), elemSlot, elemTy);

  LoopFrame frame;
  frame.Continue = condBB;
  frame.Break = doneBB;
  frame.Label = f->Label;
  frame.ScopeDepth = iterationDepth - 1;
  fs().Loops.push_back(frame);
  emitBlock(f->Body.get(), nullptr, nullptr);
  fs().Loops.pop_back();
  if (!blockIsTerminated())
    emitScopeCleanup(iterationDepth - 1);
  fs().Scopes.pop_back();
  ensureTerminated(condBB);

  B->SetInsertPoint(doneBB);
  emitScopeCleanup(loopDepth - 1);
  fs().Scopes.pop_back();
  return nullptr;
}

Value *CodeGen::emitFor(ForExpr *f) {
  if (f->NextMethod)
    return emitIteratorFor(f);
  TempScope ownTemps(*this);
  Function *fn = fs().Fn;
  fs().Scopes.push_back(LexicalScope{});
  size_t scopeDepth = fs().Scopes.size();

  Value *index = nullptr, *limit = nullptr, *dataPtr = nullptr;
  Type *elemTy = nullptr;
  bool inclusive = false;
  llvm::Type *seqLLVM = nullptr;
  Type *seqTy = f->Sequence->Ty;

  if (auto *range = dyn_cast<RangeExpr>(f->Sequence.get())) {
    elemTy = f->Binding->Ty ? f->Binding->Ty : Types.i64();
    Value *lo = range->Lo ? emitRValue(range->Lo.get()) : B->getInt64(0);
    if (range->Lo)
      lo = coerce(lo, range->Lo->Ty, elemTy);
    Value *hi = range->Hi ? emitRValue(range->Hi.get()) : B->getInt64(0);
    if (range->Hi)
      hi = coerce(hi, range->Hi->Ty, elemTy);
    inclusive = range->Inclusive;
    index = createEntryAlloca(lower(elemTy), "i");
    B->CreateStore(lo, index);
    limit = hi;
  } else {
    elemTy = f->Binding->Ty;
    Value *addr = seqTy->is(TypeKind::Pointer) ? emitRValue(f->Sequence.get())
                                               : emitLValue(f->Sequence.get());
    Type *eff = seqTy->is(TypeKind::Pointer) ? seqTy->pointee() : seqTy;
    seqLLVM = lower(eff);
    if (eff->is(TypeKind::Array)) {
      dataPtr = addr;
      limit = ConstantInt::get(B->getInt64Ty(), eff->arraySize());
    } else {
      dataPtr = B->CreateLoad(PtrTy, B->CreateStructGEP(seqLLVM, addr, 0));
      limit = B->CreateLoad(B->getInt64Ty(),
                            B->CreateStructGEP(seqLLVM, addr, 1));
      seqLLVM = lower(eff->element());
    }
    index = createEntryAlloca(B->getInt64Ty(), "i");
    B->CreateStore(B->getInt64(0), index);
  }
  emitStatementCleanup();

  auto *condBB = BasicBlock::Create(*Ctx, "for.cond", fn);
  auto *bodyBB = BasicBlock::Create(*Ctx, "for.body", fn);
  auto *stepBB = BasicBlock::Create(*Ctx, "for.step", fn);
  auto *doneBB = BasicBlock::Create(*Ctx, "for.done", fn);

  B->CreateBr(condBB);
  B->SetInsertPoint(condBB);
  llvm::Type *indexTy = dataPtr ? static_cast<llvm::Type *>(B->getInt64Ty())
                                : lower(elemTy);
  Value *cur = B->CreateLoad(indexTy, index);
  bool isSigned = dataPtr ? true : (elemTy->isInt() ? elemTy->isSigned() : true);
  Value *cmp = inclusive
                   ? (isSigned ? B->CreateICmpSLE(cur, limit)
                               : B->CreateICmpULE(cur, limit))
                   : (isSigned ? B->CreateICmpSLT(cur, limit)
                               : B->CreateICmpULT(cur, limit));
  B->CreateCondBr(cmp, bodyBB, doneBB);

  B->SetInsertPoint(bodyBB);
  // The element and any names the pattern introduces live in a scope of their
  // own that is torn down at the end of every iteration, so a sequence of
  // reference-counted values does not accumulate retains.
  fs().Scopes.push_back(LexicalScope{});
  size_t iterationDepth = fs().Scopes.size();

  Value *elemSlot = createEntryAlloca(lower(elemTy), "elem");
  if (elemTy->isRefCounted())
    B->CreateStore(Constant::getNullValue(lower(elemTy)), elemSlot);
  if (dataPtr) {
    Value *src = B->CreateInBoundsGEP(
        seqTy->is(TypeKind::Array) ||
                (seqTy->is(TypeKind::Pointer) && seqTy->pointee() &&
                 seqTy->pointee()->is(TypeKind::Array))
            ? lower(seqTy->is(TypeKind::Pointer) ? seqTy->pointee() : seqTy)
            : lower(elemTy),
        dataPtr,
        seqTy->is(TypeKind::Slice) ||
                (seqTy->is(TypeKind::Pointer) && seqTy->pointee() &&
                 seqTy->pointee()->is(TypeKind::Slice))
            ? std::vector<Value *>{cur}
            : std::vector<Value *>{B->getInt64(0), cur});
    Value *v = B->CreateLoad(lower(elemTy), src);
    // Under Zombie the element is looked at where it is: the loop variable
    // is an alias of the array's slot, and owns nothing.
    emitRetain(v, elemTy);
    B->CreateStore(v, elemSlot);
  } else {
    B->CreateStore(cur, elemSlot);
  }
  if (!zombie())
    fs().Scopes.back().Locals.push_back({elemSlot, elemTy});
  emitPatternBind(f->Binding.get(), elemSlot, elemTy);

  LoopFrame frame;
  frame.Continue = stepBB;
  frame.Break = doneBB;
  frame.Label = f->Label;
  // `break` and `continue` unwind the iteration scope as well.
  frame.ScopeDepth = iterationDepth - 1;
  fs().Loops.push_back(frame);
  emitBlock(f->Body.get(), nullptr, nullptr);
  fs().Loops.pop_back();
  if (!blockIsTerminated())
    emitScopeCleanup(iterationDepth - 1);
  fs().Scopes.pop_back();
  ensureTerminated(stepBB);

  B->SetInsertPoint(stepBB);
  Value *next = B->CreateAdd(B->CreateLoad(indexTy, index),
                             ConstantInt::get(indexTy, 1));
  B->CreateStore(next, index);
  B->CreateBr(condBB);

  B->SetInsertPoint(doneBB);
  fs().Scopes.pop_back();
  return nullptr;
}

//===----------------------------------------------------------------------===//
// Patterns
//===----------------------------------------------------------------------===//

Value *CodeGen::simplePatternCondition(Pattern *pat, Value *addr, Type *t) {
  if (!pat)
    return nullptr;
  switch (pat->Kind) {
  case NodeKind::WildcardPat:
    return B->getInt1(true);
  case NodeKind::BindingPat: {
    auto *b = cast<BindingPattern>(pat);
    if (!b->isVariantTest())
      return nullptr; // introduces a binding
    Value *tag = B->CreateLoad(B->getInt32Ty(),
                               B->CreateStructGEP(lower(t), addr, 0));
    auto *e = b->VariantOwner;
    int64_t want = e->Variants[static_cast<size_t>(b->VariantIndex)]->Value;
    return B->CreateICmpEQ(tag, B->getInt32(static_cast<uint32_t>(want)));
  }
  case NodeKind::PathPat: {
    auto *p = cast<PathPattern>(pat);
    if (p->VariantIndex < 0)
      return nullptr;
    Value *tag = B->CreateLoad(B->getInt32Ty(),
                               B->CreateStructGEP(lower(t), addr, 0));
    auto *e = reinterpret_cast<EnumDecl *>(p->ResolvedDecl);
    int64_t want = e->Variants[static_cast<size_t>(p->VariantIndex)]->Value;
    return B->CreateICmpEQ(tag, B->getInt32(static_cast<uint32_t>(want)));
  }
  case NodeKind::LiteralPat: {
    auto *lp = cast<LiteralPattern>(pat);
    Value *want = emitRValue(lp->Value.get());
    Value *have = B->CreateLoad(lower(t), addr);
    if (t->is(TypeKind::String))
      return B->CreateCall(
          runtimeFn("rune_string_equal", B->getInt32Ty(), {PtrTy, PtrTy}),
          {have, want}) != nullptr
                 ? B->CreateICmpNE(
                       B->CreateCall(runtimeFn("rune_string_equal",
                                               B->getInt32Ty(), {PtrTy, PtrTy}),
                                     {have, want}),
                       B->getInt32(0))
                 : nullptr;
    want = coerce(want, lp->Value->Ty, t);
    return t->isFloat() ? B->CreateFCmpOEQ(have, want)
                        : B->CreateICmpEQ(have, want);
  }
  case NodeKind::RangePat: {
    auto *rp = cast<RangePattern>(pat);
    Value *have = B->CreateLoad(lower(t), addr);
    Value *lo = rp->Lo ? coerce(emitRValue(rp->Lo.get()), rp->Lo->Ty, t) : nullptr;
    Value *hi = rp->Hi ? coerce(emitRValue(rp->Hi.get()), rp->Hi->Ty, t) : nullptr;
    bool isSigned = t->isInt() ? t->isSigned() : true;
    Value *cond = B->getInt1(true);
    if (lo)
      cond = B->CreateAnd(cond, t->isFloat()
                                    ? B->CreateFCmpOGE(have, lo)
                                    : (isSigned ? B->CreateICmpSGE(have, lo)
                                                : B->CreateICmpUGE(have, lo)));
    if (hi) {
      Value *upper = rp->Inclusive
                         ? (t->isFloat() ? B->CreateFCmpOLE(have, hi)
                                         : (isSigned ? B->CreateICmpSLE(have, hi)
                                                     : B->CreateICmpULE(have, hi)))
                         : (t->isFloat() ? B->CreateFCmpOLT(have, hi)
                                         : (isSigned ? B->CreateICmpSLT(have, hi)
                                                     : B->CreateICmpULT(have, hi)));
      cond = B->CreateAnd(cond, upper);
    }
    return cond;
  }
  case NodeKind::OrPat: {
    Value *cond = nullptr;
    for (auto &alt : cast<OrPattern>(pat)->Alternatives) {
      Value *c = simplePatternCondition(alt.get(), addr, t);
      if (!c)
        return nullptr;
      cond = cond ? B->CreateOr(cond, c) : c;
    }
    return cond;
  }
  default:
    return nullptr;
  }
}

void CodeGen::emitPatternBind(Pattern *pat, Value *addr, Type *t) {
  if (!pat)
    return;
  switch (pat->Kind) {
  case NodeKind::BindingPat: {
    auto *b = cast<BindingPattern>(pat);
    if (b->isVariantTest() || !b->Binding)
      return;
    Value *slot = declareLocalSlot(b->Binding, b->Name);
    Value *v = B->CreateLoad(lower(t), addr);
    if (zombie()) {
      // The binding takes the value: what it was taken from is emptied —
      // unless the binding is an alias of borrowed content, which just
      // looks at the value where it is.
      if (!b->Binding->ZombieAlias && t->isRefCounted())
        B->CreateStore(Constant::getNullValue(lower(t)), addr);
    } else {
      emitRetain(v, t);
    }
    B->CreateStore(v, slot);
    // The binding owns what it was given from here. A binding something can
    // hand on carries a flag saying whether it still does; this is where it
    // starts owning one.
    if (Value *flag = liveFlagFor(b->Binding))
      B->CreateStore(B->getTrue(), flag);
    if (b->Sub)
      emitPatternBind(b->Sub.get(), addr, t);
    return;
  }
  case NodeKind::TuplePat: {
    auto *tp = cast<TuplePattern>(pat);
    for (size_t i = 0; i < tp->Elements.size() && i < t->tupleElements().size();
         ++i)
      emitPatternBind(tp->Elements[i].get(),
                      B->CreateStructGEP(lower(t), addr, static_cast<unsigned>(i)),
                      t->tupleElements()[i]);
    return;
  }
  case NodeKind::StructPat: {
    auto *sp = cast<StructPattern>(pat);
    if (sp->VariantIndex >= 0)
      return; // refutable; handled by emitPatternTest
    StructType *layout = layoutOf(t->nominal(), t);
    auto fields = allFieldsOf(t->nominal());
    unsigned base = t->is(TypeKind::Class) ? 1 : 0;
    for (auto &pf : sp->Fields) {
      if (!pf.Value || pf.FieldIndex >= fields.size())
        continue;
      emitPatternBind(pf.Value.get(),
                      B->CreateStructGEP(layout, addr, base + pf.FieldIndex),
                      fields[pf.FieldIndex]->Ty);
    }
    return;
  }
  case NodeKind::RefPat:
    emitPatternBind(cast<RefPattern>(pat)->Sub.get(), addr, t);
    return;
  case NodeKind::SlicePat:
    emitSlicePattern(cast<SlicePattern>(pat), addr, t, nullptr);
    return;
  default:
    return;
  }
}

void CodeGen::emitSlicePattern(SlicePattern *sp, Value *addr, Type *t,
                               BasicBlock *fail) {
  Type *elem = sp->ElementTy ? sp->ElementTy : t->element();
  if (!elem)
    return;
  Function *f = fs().Fn;
  llvm::Type *elemTy = lower(elem);

  // Where the elements are, and how many.
  Value *data = nullptr;
  Value *length = nullptr;
  if (t->is(TypeKind::Array)) {
    data = B->CreateInBoundsGEP(lower(t), addr, {B->getInt64(0), B->getInt64(0)},
                                "elems");
    length = B->getInt64(t->arraySize());
  } else {
    StructType *sliceTy = cast<StructType>(lower(t));
    data = B->CreateLoad(PtrTy, B->CreateStructGEP(sliceTy, addr, 0), "elems");
    length = B->CreateLoad(B->getInt64Ty(),
                           B->CreateStructGEP(sliceTy, addr, 1), "len");
    // A slice's length is only known now. The pattern wants exactly its
    // named elements, or at least that many when `..` is there.
    if (fail) {
      const uint64_t named = sp->Prefix.size() + sp->Suffix.size();
      Value *ok = sp->HasRest ? B->CreateICmpUGE(length, B->getInt64(named))
                              : B->CreateICmpEQ(length, B->getInt64(named));
      auto *okBB = BasicBlock::Create(*Ctx, "pat.len.ok", f);
      B->CreateCondBr(ok, okBB, fail);
      B->SetInsertPoint(okBB);
    }
  }

  auto elementAt = [&](Value *index) {
    return B->CreateInBoundsGEP(elemTy, data, index, "elem");
  };
  auto one = [&](Pattern *sub, Value *index) {
    Value *at = elementAt(index);
    if (fail)
      emitPatternTest(sub, at, elem, fail);
    else
      emitPatternBind(sub, at, elem);
  };

  for (size_t i = 0; i < sp->Prefix.size(); ++i)
    one(sp->Prefix[i].get(), B->getInt64(i));
  const uint64_t suffixCount = sp->Suffix.size();
  for (size_t i = 0; i < sp->Suffix.size(); ++i) {
    // Counted back from the end: the last suffix element is at `len - 1`.
    Value *index = B->CreateSub(length, B->getInt64(suffixCount - i));
    one(sp->Suffix[i].get(), index);
  }

  // `..rest`: a slice of whatever sits between the two runs, pointing into
  // the same storage rather than copying it.
  if (sp->Rest) {
    Type *restTy = Types.sliceOf(elem);
    Value *start = elementAt(B->getInt64(sp->Prefix.size()));
    Value *count = B->CreateSub(
        length, B->getInt64(sp->Prefix.size() + suffixCount), "rest.len");
    Value *slice = UndefValue::get(lower(restTy));
    slice = B->CreateInsertValue(slice, start, 0);
    slice = B->CreateInsertValue(slice, count, 1);
    Value *slot = createEntryAlloca(lower(restTy), "rest");
    B->CreateStore(slice, slot);
    if (fail)
      emitPatternTest(sp->Rest.get(), slot, restTy, fail);
    else
      emitPatternBind(sp->Rest.get(), slot, restTy);
  }
}

void CodeGen::emitPatternTest(Pattern *pat, Value *addr, Type *t,
                              BasicBlock *fail) {
  if (!pat)
    return;
  Function *f = fs().Fn;

  switch (pat->Kind) {
  case NodeKind::WildcardPat:
    return;

  case NodeKind::BindingPat: {
    auto *b = cast<BindingPattern>(pat);
    if (b->isVariantTest()) {
      Value *cond = simplePatternCondition(pat, addr, t);
      auto *okBB = BasicBlock::Create(*Ctx, "pat.ok", f);
      B->CreateCondBr(cond, okBB, fail);
      B->SetInsertPoint(okBB);
      return;
    }
    emitPatternBind(pat, addr, t);
    return;
  }

  case NodeKind::LiteralPat:
  case NodeKind::RangePat:
  case NodeKind::PathPat: {
    Value *cond = simplePatternCondition(pat, addr, t);
    if (!cond) {
      reportUnsupported(pat->Range, "this pattern");
      return;
    }
    auto *okBB = BasicBlock::Create(*Ctx, "pat.ok", f);
    B->CreateCondBr(cond, okBB, fail);
    B->SetInsertPoint(okBB);
    return;
  }

  case NodeKind::OrPat: {
    auto *o = cast<OrPattern>(pat);
    if (o->Alternatives.empty())
      return;
    // A pure test folds to one condition; anything that binds needs a block
    // per alternative, all converging on a shared success block where the
    // bindings are live regardless of which alternative matched.
    if (Value *cond = simplePatternCondition(pat, addr, t)) {
      auto *okBB = BasicBlock::Create(*Ctx, "pat.ok", f);
      B->CreateCondBr(cond, okBB, fail);
      B->SetInsertPoint(okBB);
      return;
    }
    auto *successBB = BasicBlock::Create(*Ctx, "pat.or.match", f);
    for (size_t i = 0; i < o->Alternatives.size(); ++i) {
      bool isLast = i + 1 == o->Alternatives.size();
      BasicBlock *nextBB =
          isLast ? fail : BasicBlock::Create(*Ctx, "pat.or.next", f);
      emitPatternTest(o->Alternatives[i].get(), addr, t, nextBB);
      ensureTerminated(successBB);
      if (!isLast)
        B->SetInsertPoint(nextBB);
    }
    B->SetInsertPoint(successBB);
    return;
  }

  case NodeKind::TuplePat: {
    auto *tp = cast<TuplePattern>(pat);
    for (size_t i = 0; i < tp->Elements.size() && i < t->tupleElements().size();
         ++i)
      emitPatternTest(tp->Elements[i].get(),
                      B->CreateStructGEP(lower(t), addr, static_cast<unsigned>(i)),
                      t->tupleElements()[i], fail);
    return;
  }

  case NodeKind::RefPat:
    emitPatternTest(cast<RefPattern>(pat)->Sub.get(), addr, t, fail);
    return;

  case NodeKind::SlicePat:
    emitSlicePattern(cast<SlicePattern>(pat), addr, t, fail);
    return;

  case NodeKind::EnumPat: {
    auto *ep = cast<EnumPattern>(pat);
    auto *e = reinterpret_cast<EnumDecl *>(ep->ResolvedDecl);
    if (!e || ep->VariantIndex < 0)
      return;
    StructType *layout = layoutOf(static_cast<NominalDecl *>(e), t);
    Value *tag = B->CreateLoad(B->getInt32Ty(),
                               B->CreateStructGEP(layout, addr, 0));
    auto *variant = e->Variants[static_cast<size_t>(ep->VariantIndex)].get();
    Value *cond = B->CreateICmpEQ(
        tag, B->getInt32(static_cast<uint32_t>(variant->Value)));
    auto *okBB = BasicBlock::Create(*Ctx, "pat.variant", f);
    B->CreateCondBr(cond, okBB, fail);
    B->SetInsertPoint(okBB);

    if (layout->getNumElements() > 1) {
      Value *payload = B->CreateStructGEP(layout, addr, 1);
      llvm::Type *pt =
          variantPayloadType(e, static_cast<unsigned>(ep->VariantIndex));
      for (size_t i = 0;
           i < ep->Elements.size() && i < variant->TupleTypes.size(); ++i)
        emitPatternTest(ep->Elements[i].get(),
                        B->CreateStructGEP(pt, payload, static_cast<unsigned>(i)),
                        variant->TupleTypes[i]->Resolved, fail);
    }
    return;
  }

  case NodeKind::StructPat: {
    auto *sp = cast<StructPattern>(pat);
    if (sp->VariantIndex >= 0) {
      auto *e = reinterpret_cast<EnumDecl *>(sp->ResolvedDecl);
      StructType *layout = layoutOf(static_cast<NominalDecl *>(e), t);
      Value *tag = B->CreateLoad(B->getInt32Ty(),
                                 B->CreateStructGEP(layout, addr, 0));
      auto *variant = e->Variants[static_cast<size_t>(sp->VariantIndex)].get();
      Value *cond = B->CreateICmpEQ(
          tag, B->getInt32(static_cast<uint32_t>(variant->Value)));
      auto *okBB = BasicBlock::Create(*Ctx, "pat.variant", f);
      B->CreateCondBr(cond, okBB, fail);
      B->SetInsertPoint(okBB);

      if (layout->getNumElements() > 1) {
        Value *payload = B->CreateStructGEP(layout, addr, 1);
        llvm::Type *pt =
            variantPayloadType(e, static_cast<unsigned>(sp->VariantIndex));
        for (auto &pf : sp->Fields) {
          if (!pf.Value || pf.FieldIndex >= variant->Fields.size())
            continue;
          emitPatternTest(pf.Value.get(),
                          B->CreateStructGEP(pt, payload, pf.FieldIndex),
                          variant->Fields[pf.FieldIndex]->Ty, fail);
        }
      }
      return;
    }
    StructType *layout = layoutOf(t->nominal(), t);
    auto fields = allFieldsOf(t->nominal());
    unsigned base = t->is(TypeKind::Class) ? 1 : 0;
    for (auto &pf : sp->Fields) {
      if (!pf.Value || pf.FieldIndex >= fields.size())
        continue;
      emitPatternTest(pf.Value.get(),
                      B->CreateStructGEP(layout, addr, base + pf.FieldIndex),
                      fields[pf.FieldIndex]->Ty, fail);
    }
    return;
  }

  default:
    reportUnsupported(pat->Range, "this pattern");
    return;
  }
}

Value *CodeGen::emitMatch(MatchExpr *m, Value *slot, Type *slotType) {
  TempScope ownTemps(*this);
  Function *f = fs().Fn;
  Type *scrutTy = m->Scrutinee->Ty;

  // Matching through a borrow reads the referent in place; matching a value
  // copies it so the arms can hold on to it for as long as they run.
  // The copy of the subject belongs to the match, and every way out of it has
  // to give the copy back — including an arm that `return`s or `break`s, which
  // never reaches the end. A scope of its own is what the existing unwinding
  // already knows how to walk.
  fs().Scopes.push_back(LexicalScope{});
  const size_t matchDepth = fs().Scopes.size();

  Value *addr = nullptr;
  bool ownsSubject = false;
  if (scrutTy->is(TypeKind::Pointer)) {
    addr = emitRValue(m->Scrutinee.get());
    scrutTy = scrutTy->pointee();
    while (scrutTy->is(TypeKind::Pointer)) {
      addr = B->CreateLoad(PtrTy, addr);
      scrutTy = scrutTy->pointee();
    }
  } else {
    addr = createEntryAlloca(lower(scrutTy), "match.subject");
    if (scrutTy->isRefCounted())
      B->CreateStore(Constant::getNullValue(lower(scrutTy)), addr);
    emitInto(m->Scrutinee.get(), addr, scrutTy);
    ownsSubject = true;
    if (scrutTy->isRefCounted())
      fs().Scopes.back().Locals.push_back({addr, scrutTy});
  }
  emitStatementCleanup();

  auto *doneBB = BasicBlock::Create(*Ctx, "match.done", f);
  auto *noMatchBB = BasicBlock::Create(*Ctx, "match.none", f);

  for (size_t i = 0; i < m->Arms.size(); ++i) {
    MatchArm &arm = m->Arms[i];
    auto *nextBB = (i + 1 < m->Arms.size())
                       ? BasicBlock::Create(*Ctx, "match.next", f)
                       : noMatchBB;

    fs().Scopes.push_back(LexicalScope{});
    size_t depth = fs().Scopes.size();
    emitPatternTest(arm.Pat.get(), addr, scrutTy, nextBB);

    if (arm.Guard) {
      Value *g = emitRValue(arm.Guard.get());
      emitStatementCleanup();
      auto *guardOkBB = BasicBlock::Create(*Ctx, "match.guard", f);
      // The pattern already matched, so its bindings are live. A guard that
      // says no has to give them back before moving to the next arm.
      auto *guardFailBB = BasicBlock::Create(*Ctx, "match.guard.no", f);
      B->CreateCondBr(g, guardOkBB, guardFailBB);
      B->SetInsertPoint(guardFailBB);
      emitScopeCleanup(depth - 1);
      ensureTerminated(nextBB);
      B->SetInsertPoint(guardOkBB);
    }

    if (slot && slotType && !slotType->isVoid())
      emitInto(arm.Body.get(), slot, slotType);
    else
      emitRValue(arm.Body.get());
    emitStatementCleanup();
    emitScopeCleanup(depth - 1);
    fs().Scopes.pop_back();
    ensureTerminated(doneBB);

    B->SetInsertPoint(nextBB);
  }

  // Reached only when no arm applied; Sema warns about this, but a guard can
  // still make every arm fail at run time.
  B->CreateCall(runtimeFn("rune_panic_no_match", B->getVoidTy(), {PtrTy}),
                {locationString(m->Range)});
  B->CreateUnreachable();

  B->SetInsertPoint(doneBB);
  // Every arm has finished with the subject. A borrowed one belongs to someone
  // else and was never registered, so this releases only a copy.
  (void)ownsSubject;
  emitScopeCleanup(matchDepth - 1);
  fs().Scopes.pop_back();
  return nullptr;
}

//===----------------------------------------------------------------------===//
// Main dispatch
//===----------------------------------------------------------------------===//

Value *CodeGen::emitRValue(Expr *e) {
  if (!e)
    return nullptr;
  if (!e->Ty) {
    // Every expression reaching CodeGen must have been typed. Report rather
    // than dereference, so a gap in Sema surfaces as a diagnostic.
    Diags.fatal("internal error: expression reached code generation untyped")
        .note("this is a compiler bug, not a problem with your program");
    return nullptr;
  }

  switch (e->Kind) {
  case NodeKind::IntLit: {
    auto *l = cast<IntLitExpr>(e);
    if (e->Ty->isFloat())
      return ConstantFP::get(lower(e->Ty), static_cast<double>(l->Value));
    return ConstantInt::get(lower(e->Ty), l->Value);
  }
  case NodeKind::FloatLit:
    return ConstantFP::get(lower(e->Ty), cast<FloatLitExpr>(e)->Value);
  case NodeKind::BoolLit:
    return B->getInt1(cast<BoolLitExpr>(e)->Value);
  case NodeKind::CharLit:
    return ConstantInt::get(lower(e->Ty), cast<CharLitExpr>(e)->Value);
  case NodeKind::StringLit: {
    auto *l = cast<StringLitExpr>(e);
    return emitStringLiteral(l->Value, l->AsCString || e->Ty->is(TypeKind::CString));
  }
  case NodeKind::NilLit:
    return Constant::getNullValue(lower(e->Ty));

  case NodeKind::DeclRef: {
    auto *r = cast<DeclRefExpr>(e);
    // A unit enum variant used as a value.
    if (r->VariantIndex >= 0) {
      auto *en = dyn_cast<EnumDecl>(r->Resolved);
      if (en) {
        StructType *layout = layoutOf(static_cast<NominalDecl *>(en), e->Ty);
        Value *slot = createEntryAlloca(layout, "variant");
        B->CreateStore(Constant::getNullValue(layout), slot);
        int64_t v = en->Variants[static_cast<size_t>(r->VariantIndex)]->Value;
        B->CreateStore(B->getInt32(static_cast<uint32_t>(v)),
                       B->CreateStructGEP(layout, slot, 0));
        return B->CreateLoad(layout, slot);
      }
    }
    // A function used as a value becomes a closure with no environment —
    // unless C is on the other side, where the bare address is the value.
    if (auto *fd = dyn_cast<FunctionDecl>(r->Resolved)) {
      Function *target = declareFunction(fd);
      if (e->Ty && e->Ty->is(TypeKind::CFunction))
        return target;
      Function *thunk = thunkFor(target, e->Ty);
      Value *v = UndefValue::get(lower(e->Ty));
      v = B->CreateInsertValue(v, thunk, 0);
      v = B->CreateInsertValue(v, ConstantPointerNull::get(PtrTy), 1);
      return v;
    }
    Value *addr = emitLValue(e);
    if (!addr)
      return Constant::getNullValue(lower(e->Ty));
    Value *loaded =
        B->CreateLoad(lower(e->Ty), addr, r->Path.empty() ? "" : r->Path.back());
    // This mention hands the value away — an assignment into another `Unique`
    // slot, or a return. Empty the slot and give the reference it held to the
    // statement's temp stack: whatever receives the value retains it, the
    // cleanup drops the slot's old claim, and the count comes out unchanged.
    // Under Zombie the consumer empties the place it moves from
    // (`takeOwnership`); the balancing trick below is ARC's.
    if (r->MovedOut && !zombie()) {
      // A value with a destructor is copied out bit for bit and the binding
      // simply stops owning it: whatever receives it will destroy it, and the
      // flag stops this scope from doing so as well.
      if (Value *flag = liveFlagFor(dyn_cast<VarDecl>(r->Resolved))) {
        B->CreateStore(B->getFalse(), flag);
        return loaded;
      }
      B->CreateStore(Constant::getNullValue(lower(e->Ty)), addr);
      return track(loaded, e->Ty);
    }
    return loaded;
  }

  case NodeKind::SelfRef: {
    Value *addr = emitLValue(e);
    if (!addr)
      return fs().SelfValue;
    return B->CreateLoad(lower(e->Ty), addr, "self");
  }

  case NodeKind::SuperRef:
    return fs().SelfValue;

  case NodeKind::Member: {
    auto *m = cast<MemberExpr>(e);
    Value *addr = emitMemberAddress(m);
    if (!addr)
      return Constant::getNullValue(lower(e->Ty));

    Type *base = m->Base->Ty;
    while (base && base->is(TypeKind::Pointer))
      base = base->pointee();
    if (FieldDecl *weak = weakFieldOf(m, base)) {
      // Reading a weak reference hands back a strong Option: either the target
      // is still alive, in which case the caller now holds a reference to it,
      // or the slot has already been emptied.
      Value *target = B->CreateCall(
          runtimeFn("rune_weak_load", PtrTy, {PtrTy}), {addr}, "weak");
      Function *f = fs().Fn;
      Value *slot = createEntryAlloca(lower(e->Ty), "weak.opt");
      B->CreateStore(Constant::getNullValue(lower(e->Ty)), slot);
      auto *liveBB = BasicBlock::Create(*Ctx, "weak.live", f);
      auto *goneBB = BasicBlock::Create(*Ctx, "weak.gone", f);
      auto *doneBB = BasicBlock::Create(*Ctx, "weak.done", f);
      B->CreateCondBr(B->CreateICmpNE(target, ConstantPointerNull::get(PtrTy)),
                      liveBB, goneBB);

      B->SetInsertPoint(liveBB);
      emitRetain(target, weak->WeakTarget);
      B->CreateStore(
          emitEnumVariant(e->Ty, variantIndexNamed(e->Ty, "Some"), {target}),
          slot);
      B->CreateBr(doneBB);

      B->SetInsertPoint(goneBB);
      B->CreateStore(emitEnumVariant(e->Ty, variantIndexNamed(e->Ty, "None"), {}),
                     slot);
      B->CreateBr(doneBB);

      B->SetInsertPoint(doneBB);
      return track(B->CreateLoad(lower(e->Ty), slot), e->Ty);
    }
    return B->CreateLoad(lower(e->Ty), addr, m->Name);
  }

  case NodeKind::Index: {
    auto *i = cast<IndexExpr>(e);
    if (auto *impl = i->OverloadResolved) {
      Function *f = declareFunction(impl);
      Type *selfParam = nullptr, *idxParam = nullptr;
      for (const Param &p : impl->Params) {
        if (p.IsSelf) selfParam = p.Ty;
        else if (!idxParam) idxParam = p.Ty;
      }
      Value *self = selfParam && selfParam->is(TypeKind::Pointer)
                        ? emitLValue(i->Base.get())
                        : emitRValue(i->Base.get());
      Value *idx = emitRValue(i->Index.get());
      if (idxParam)
        idx = coerce(idx, i->Index->Ty, idxParam);
      return track(B->CreateCall(f, {self, idx}), e->Ty);
    }
    // `values[a..b]` produces a slice pointing into the same storage.
    if (auto *range = dyn_cast<RangeExpr>(i->Index.get())) {
      Type *baseTy = i->Base->Ty;
      Type *eff = baseTy;
      Value *base = nullptr;
      if (eff->is(TypeKind::Pointer)) {
        base = emitRValue(i->Base.get());
        eff = eff->pointee();
      } else {
        base = emitLValue(i->Base.get());
      }

      Value *data = base;
      Value *length = nullptr;
      if (eff->is(TypeKind::Array)) {
        data = B->CreateInBoundsGEP(lower(eff), base,
                                    {B->getInt64(0), B->getInt64(0)});
        length = ConstantInt::get(B->getInt64Ty(), eff->arraySize());
      } else {
        StructType *sliceTy = cast<StructType>(lower(eff));
        data = B->CreateLoad(PtrTy, B->CreateStructGEP(sliceTy, base, 0));
        length = B->CreateLoad(B->getInt64Ty(),
                               B->CreateStructGEP(sliceTy, base, 1));
      }

      Value *lo = range->Lo ? coerce(emitRValue(range->Lo.get()),
                                     range->Lo->Ty, Types.i64())
                            : B->getInt64(0);
      Value *hi = range->Hi ? coerce(emitRValue(range->Hi.get()),
                                     range->Hi->Ty, Types.i64())
                            : length;
      if (range->Inclusive)
        hi = B->CreateAdd(hi, B->getInt64(1));

      if (Opts.Safety == SafetyLevel::Full) {
        // The whole window has to sit inside the original run.
        Function *f = fs().Fn;
        auto *okBB = BasicBlock::Create(*Ctx, "slice.ok", f);
        auto *failBB = BasicBlock::Create(*Ctx, "slice.fail", f);
        Value *bad = B->CreateOr(
            B->CreateICmpSLT(lo, B->getInt64(0)),
            B->CreateOr(B->CreateICmpSGT(hi, length), B->CreateICmpSGT(lo, hi)));
        B->CreateCondBr(bad, failBB, okBB);
        B->SetInsertPoint(failBB);
        B->CreateCall(runtimeFn("rune_panic_bounds", B->getVoidTy(),
                                {B->getInt64Ty(), B->getInt64Ty(), PtrTy}),
                      {hi, length, locationString(i->Range)});
        B->CreateUnreachable();
        B->SetInsertPoint(okBB);
      }

      Type *elem = e->Ty->element();
      Value *start = B->CreateInBoundsGEP(lower(elem), data, lo);
      Value *slice = UndefValue::get(lower(e->Ty));
      slice = B->CreateInsertValue(slice, start, 0);
      slice = B->CreateInsertValue(slice, B->CreateSub(hi, lo), 1);
      return slice;
    }

    Value *addr = emitIndexAddress(i);
    if (!addr)
      return Constant::getNullValue(lower(e->Ty));
    return B->CreateLoad(lower(e->Ty), addr, "elem");
  }

  case NodeKind::Call:
    return emitCall(cast<CallExpr>(e));
  case NodeKind::Binary:
    return emitBinary(cast<BinaryExpr>(e));
  case NodeKind::Unary:
    return emitUnary(cast<UnaryExpr>(e));
  case NodeKind::Assign:
    return emitAssign(cast<AssignExpr>(e));
  case NodeKind::Cast:
    return emitCast(cast<CastExpr>(e));
  case NodeKind::Into:
    return emitInto(cast<IntoExpr>(e));
  case NodeKind::Move: {
    // A move hands the one reference on without any counting. The source slot
    // is blanked so the scope will not release it, and the reference it held
    // is passed to the statement's temp stack instead: whatever the value is
    // being stored into retains it, and the statement's cleanup then drops the
    // reference the slot used to own. The count comes out where it started.
    //
    // A `move` that feeds nothing is therefore a destruction, which is right —
    // the last owner gave the value away to no one.
    auto *m = cast<MoveExpr>(e);
    Value *v = emitRValue(m->Operand.get());
    // Under Zombie the consumer empties the source (`movedPlaceOf` looks
    // through the `move`); a `move` that feeds nothing is dropped as a
    // discarded value.
    if (zombie())
      return v;
    if (m->MovedFrom && m->Operand->Ty) {
      auto it = fs().Slots.find(m->MovedFrom);
      if (it != fs().Slots.end()) {
        B->CreateStore(Constant::getNullValue(lower(m->Operand->Ty)),
                       it->second);
        return track(v, m->Operand->Ty);
      }
    }
    return v;
  }
  case NodeKind::Try:
    return emitTry(cast<TryExpr>(e));
  case NodeKind::Closure:
    return emitClosureValue(cast<ClosureExpr>(e));
  case NodeKind::StructLit:
    return emitStructLit(cast<StructLitExpr>(e));
  case NodeKind::ArrayLit:
    return emitArrayLit(cast<ArrayLitExpr>(e));

  case NodeKind::TupleLit: {
    auto *t = cast<TupleLitExpr>(e);
    if (e->Ty->isVoid())
      return nullptr;
    Value *slot = createEntryAlloca(lower(e->Ty), "tuple");
    if (e->Ty->isRefCounted())
      B->CreateStore(Constant::getNullValue(lower(e->Ty)), slot);
    for (size_t i = 0; i < t->Elements.size(); ++i)
      emitInto(t->Elements[i].get(),
               B->CreateStructGEP(lower(e->Ty), slot, static_cast<unsigned>(i)),
               e->Ty->tupleElements()[i]);
    return track(B->CreateLoad(lower(e->Ty), slot), e->Ty);
  }

  case NodeKind::Borrow: {
    auto *b = cast<BorrowExpr>(e);
    // Under Zombie a shared borrow of a class, a `String`, a closure or a
    // mark object is the handle: the object is what is borrowed, and it
    // stays where it is however the handle moves.
    if (handleBorrow(e->Ty))
      return emitRValue(b->Operand.get());
    return emitLValue(b->Operand.get());
  }

  case NodeKind::Deref: {
    auto *deref = cast<DerefExpr>(e);
    // `*value` on a type that overloads it is just a call.
    if (deref->OverloadResolved) {
      FunctionDecl *impl = deref->OverloadResolved;
      Function *fn = declareFunction(impl);
      Type *selfParam = nullptr;
      for (const Param &p : impl->Params)
        if (p.IsSelf)
          selfParam = p.Ty;
      Value *self = selfParam && selfParam->is(TypeKind::Pointer)
                        ? emitLValue(deref->Operand.get())
                        : emitRValue(deref->Operand.get());
      if (!self)
        return Constant::getNullValue(lower(e->Ty));
      return track(B->CreateCall(fn, {self}), e->Ty);
    }
    Value *ptr = emitRValue(deref->Operand.get());
    // A shared `&Class` under Zombie is the object: `*r` is `r`.
    if (handleBorrow(deref->Operand->Ty))
      return ptr;
    if (Opts.Safety != SafetyLevel::None) {
      Function *f = fs().Fn;
      auto *okBB = BasicBlock::Create(*Ctx, "deref.ok", f);
      auto *failBB = BasicBlock::Create(*Ctx, "deref.nil", f);
      B->CreateCondBr(B->CreateICmpEQ(ptr, ConstantPointerNull::get(PtrTy)),
                      failBB, okBB);
      B->SetInsertPoint(failBB);
      B->CreateCall(runtimeFn("rune_panic_nil", B->getVoidTy(), {PtrTy}),
                    {locationString(e->Range)});
      B->CreateUnreachable();
      B->SetInsertPoint(okBB);
    }
    return B->CreateLoad(lower(e->Ty), ptr);
  }

  case NodeKind::TypeTest: {
    auto *tt = cast<TypeTestExpr>(e);
    Value *obj = emitRValue(tt->Operand.get());
    Type *target = tt->TargetType->Resolved;
    // An `Any` answers for every type, not just the nominal ones, so it goes
    // through the descriptor rather than the class hierarchy alone.
    if (tt->Operand->Ty && tt->Operand->Ty->isAny())
      return emitAnyIs(obj, target);
    if (!target || !target->isNominal())
      return B->getInt1(false);
    GlobalVariable *info = emitTypeInfo(target->nominal());
    Value *r = B->CreateCall(
        runtimeFn("rune_is_kind_of", B->getInt32Ty(), {PtrTy, PtrTy}),
        {obj, info});
    return B->CreateICmpNE(r, B->getInt32(0));
  }

  case NodeKind::Block: {
    if (e->Ty->isVoid() || e->Ty->isNever()) {
      emitBlock(cast<BlockExpr>(e), nullptr, nullptr);
      return nullptr;
    }
    Value *slot = createEntryAlloca(lower(e->Ty), "block.value");
    if (e->Ty->isRefCounted())
      B->CreateStore(Constant::getNullValue(lower(e->Ty)), slot);
    emitBlock(cast<BlockExpr>(e), slot, e->Ty);
    return track(B->CreateLoad(lower(e->Ty), slot), e->Ty);
  }

  case NodeKind::UnsafeBlock: {
    auto *u = cast<UnsafeBlockExpr>(e);
    if (e->Ty->isVoid() || e->Ty->isNever()) {
      emitBlock(u->Body.get(), nullptr, nullptr);
      return nullptr;
    }
    Value *slot = createEntryAlloca(lower(e->Ty), "unsafe.value");
    if (e->Ty->isRefCounted())
      B->CreateStore(Constant::getNullValue(lower(e->Ty)), slot);
    emitBlock(u->Body.get(), slot, e->Ty);
    Value *result = B->CreateLoad(lower(e->Ty), slot);
    // `unsafe { slot[i] }` reads a borrow out of raw memory: the storage keeps
    // owning it, so it must not join the statement's drop list.
    if (zombie() && readsUntrackedMemory(e))
      return result;
    return track(result, e->Ty);
  }

  case NodeKind::If: {
    if (e->Ty->isVoid() || e->Ty->isNever()) {
      emitIf(cast<IfExpr>(e), nullptr, nullptr);
      return nullptr;
    }
    Value *slot = createEntryAlloca(lower(e->Ty), "if.value");
    if (e->Ty->isRefCounted())
      B->CreateStore(Constant::getNullValue(lower(e->Ty)), slot);
    emitIf(cast<IfExpr>(e), slot, e->Ty);
    return track(B->CreateLoad(lower(e->Ty), slot), e->Ty);
  }

  case NodeKind::Match: {
    if (e->Ty->isVoid() || e->Ty->isNever()) {
      emitMatch(cast<MatchExpr>(e), nullptr, nullptr);
      return nullptr;
    }
    Value *slot = createEntryAlloca(lower(e->Ty), "match.value");
    if (e->Ty->isRefCounted())
      B->CreateStore(Constant::getNullValue(lower(e->Ty)), slot);
    emitMatch(cast<MatchExpr>(e), slot, e->Ty);
    return track(B->CreateLoad(lower(e->Ty), slot), e->Ty);
  }

  case NodeKind::While:
    return emitWhile(cast<WhileExpr>(e));
  case NodeKind::For:
    return emitFor(cast<ForExpr>(e));
  case NodeKind::Loop: {
    if (e->Ty->isVoid() || e->Ty->isNever()) {
      emitLoop(cast<LoopExpr>(e), nullptr, nullptr);
      return nullptr;
    }
    Value *slot = createEntryAlloca(lower(e->Ty), "loop.value");
    if (e->Ty->isRefCounted())
      B->CreateStore(Constant::getNullValue(lower(e->Ty)), slot);
    emitLoop(cast<LoopExpr>(e), slot, e->Ty);
    return track(B->CreateLoad(lower(e->Ty), slot), e->Ty);
  }

  case NodeKind::Return: {
    auto *r = cast<ReturnExpr>(e);
    if (r->Value && fs().ReturnSlot)
      emitInto(r->Value.get(), fs().ReturnSlot, fs().ReturnType);
    else if (r->Value)
      emitRValue(r->Value.get());
    // A `return` may sit inside a larger statement, whose other branches still
    // need their temporaries released.
    emitStatementCleanup(/*consume=*/false);
    emitAllScopeCleanups(0);
    B->CreateBr(fs().ReturnBlock);
    return nullptr;
  }

  case NodeKind::Break: {
    auto *b = cast<BreakExpr>(e);
    LoopFrame *target = nullptr;
    for (auto it = fs().Loops.rbegin(); it != fs().Loops.rend(); ++it) {
      if (b->Label.empty() || it->Label == b->Label) {
        target = &*it;
        break;
      }
    }
    if (!target)
      return nullptr;
    if (b->Value && target->ResultSlot)
      emitInto(b->Value.get(), target->ResultSlot, target->ResultType);
    else if (b->Value)
      emitRValue(b->Value.get());
    emitStatementCleanup(/*consume=*/false);
    emitAllScopeCleanups(target->ScopeDepth);
    B->CreateBr(target->Break);
    return nullptr;
  }

  case NodeKind::Continue: {
    auto *c = cast<ContinueExpr>(e);
    LoopFrame *target = nullptr;
    for (auto it = fs().Loops.rbegin(); it != fs().Loops.rend(); ++it) {
      if (c->Label.empty() || it->Label == c->Label) {
        target = &*it;
        break;
      }
    }
    if (!target)
      return nullptr;
    emitStatementCleanup(/*consume=*/false);
    emitAllScopeCleanups(target->ScopeDepth);
    B->CreateBr(target->Continue);
    return nullptr;
  }

  case NodeKind::Range: {
    // A bare range outside `for` is a pair of bounds.
    auto *r = cast<RangeExpr>(e);
    Value *slot = createEntryAlloca(lower(e->Ty), "range");
    if (r->Lo)
      emitInto(r->Lo.get(), B->CreateStructGEP(lower(e->Ty), slot, 0),
               e->Ty->tupleElements()[0]);
    if (r->Hi)
      emitInto(r->Hi.get(), B->CreateStructGEP(lower(e->Ty), slot, 1),
               e->Ty->tupleElements()[1]);
    return B->CreateLoad(lower(e->Ty), slot);
  }

  case NodeKind::Error:
    return Constant::getNullValue(lower(e->Ty));

  default:
    reportUnsupported(e->Range, "this expression");
    return Constant::getNullValue(lower(e->Ty));
  }
}

} // namespace rune
