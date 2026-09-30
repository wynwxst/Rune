//===- SemaExpr.cpp - Expression type checking -----------------*- C++ -*-===//
//
// The second half of Sema. Expressions are checked with an optional `expected`
// type that steers literal typing and closure parameter inference; it is a
// hint, never a constraint, so a mismatch is still reported at the point where
// the value is used.
//
//===----------------------------------------------------------------------===//

#include "rune/ASTClone.h"
#include "rune/Sema.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <set>

namespace rune {

/// A top-level function has no environment, so its address alone is a valid C
/// function pointer. That is only worth producing where one is wanted —
/// everywhere else a function value is a closure, and carries one.
Type *Sema::asCFunctionIfWanted(FunctionDecl *fn, Type *expected,
                                SourceRange at) {
  if (!fn->Ty)
    return Types.errorType();
  if (!expected || !expected->is(TypeKind::CFunction))
    return fn->Ty;
  if (fn->Ty->params() != expected->params() ||
      fn->Ty->result() != expected->result() ||
      fn->Ty->isVariadicFunction() != expected->isVariadicFunction())
    return fn->Ty;
  if (fn->SourceClosure) {
    Diags.error(at, "a closure cannot be passed to C as a function pointer")
        .note("it carries captured state, which a C function pointer has "
              "nowhere to put")
        .note("pass a top-level `fn` instead")
        .code(502);
    return Types.errorType();
  }
  return expected;
}

/// Checks what a `std::reflect` intrinsic asks for, while the question is
/// still a question about types.
///
/// The *answers* need the target's layout and so are worked out in the code
/// generator; the mistakes do not, and catching them here is what makes
/// `rune check` agree with `rune build`.
void Sema::checkReflectionCall(CallExpr *c, FunctionDecl *fn) {
  const Attribute *a = fn->findAttr("intrinsic");
  if (!a || a->Args.empty())
    return;
  const auto *lit = dyn_cast<StringLitExpr>(a->Args[0].get());
  if (!lit)
    return;
  const std::string &which = lit->Value;

  // Inline assembly is compiled, not called: the instructions and the
  // constraints are part of the program's text and have to be there when it is
  // built. A computed string would have nothing to hand the assembler.
  if (which == "asm" || which == "asm_value") {
    static const char *kWhich[] = {"instructions", "constraints"};
    for (size_t i = 0; i < 2; ++i) {
      if (i >= c->Args.size())
        break;
      if (isa<StringLitExpr>(c->Args[i].Value.get()))
        continue;
      Diags.error(c->Args[i].Value->Range,
                  "the {} for inline assembly have to be written out here",
                  kWhich[i])
          .note("they are assembled with the program, so they cannot be "
                "computed while it runs")
          .note("a value that varies belongs in an operand, not in the text")
          .code(508);
    }
    return;
  }

  if (which == "offset_of") {
    Type *t = fn->TypeArguments.empty() ? nullptr : fn->TypeArguments[0];
    if (!t || t->isError())
      return;
    if (!t->is(TypeKind::Struct) && !t->is(TypeKind::Class)) {
      Diags.error(c->Range, "'{}' has no named fields to take an offset in",
                  t->toString())
          .note("`offset_of!` applies to a struct or a class")
          .code(506);
      return;
    }
    if (c->Args.empty())
      return;
    const auto *nameLit = dyn_cast<StringLitExpr>(c->Args[0].Value.get());
    if (!nameLit)
      return;   // not written through `offset_of!`; the code generator copes
    std::vector<FieldDecl *> fields;
    std::vector<NominalDecl *> chain;
    for (NominalDecl *nd = t->nominal(); nd;) {
      chain.push_back(nd);
      auto *cd = dyn_cast<ClassDecl>(static_cast<Decl *>(nd));
      nd = cd ? cd->Super : nullptr;
    }
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
      for (const auto &f : (*it)->Fields)
        fields.push_back(f.get());
    for (FieldDecl *f : fields)
      if (f->Name == nameLit->Value)
        return;
    auto d = Diags.error(c->Range, "'{}' has no field named '{}'",
                         t->toString(), nameLit->Value);
    std::string names;
    for (FieldDecl *f : fields)
      names += (names.empty() ? "" : ", ") + f->Name;
    if (!names.empty())
      d.note("its fields are: {}", names);
    d.code(506);
    return;
  }

  if (which == "conforms") {
    Type *m = fn->TypeArguments.size() > 1 ? fn->TypeArguments[1] : nullptr;
    if (!m || m->isError())
      return;
    // The answer is worked out here, where conformance is known — a `bind`,
    // a structural one, an automatic mark — rather than in the code
    // generator, which can only see the bindings written on the type.
    if ((m->is(TypeKind::Mark) || m->is(TypeKind::DynMark)) &&
        !fn->TypeArguments.empty() && fn->TypeArguments[0] &&
        !fn->TypeArguments[0]->isError())
      if (auto *mk = dyn_cast<MarkDecl>(static_cast<Decl *>(m->nominal())))
        c->ReflectAnswer = typeConformsTo(fn->TypeArguments[0], mk) ? 1 : 0;
    if (!m->is(TypeKind::Mark) && !m->is(TypeKind::DynMark))
      Diags.error(c->Range,
                  "the second type argument of `conforms` has to be a mark")
          .note("'{}' is a {}, not a mark", m->toString(),
                m->is(TypeKind::Class) ? "class" : "type")
          .note("write `reflect::conforms<T, io::Display>()`")
          .code(506);
    return;
  }

  if (which == "field_name" || which == "field_type") {
    if (c->Args.empty())
      return;
    if (!isa<IntLitExpr>(c->Args[0].Value.get()))
      Diags.error(c->Args[0].Value->Range,
                  "the index has to be a literal, because the answer is "
                  "chosen while compiling")
          .note("a field's name is not a value the program computes; it is "
                "part of the type")
          .code(506);
    return;
  }
}


namespace {
/// An integer literal with no suffix adapts to whatever the context wants.
bool isUntypedIntLiteral(const Expr *e) {
  const auto *l = dyn_cast<IntLitExpr>(e);
  return l && l->Suffix.empty();
}
bool isUntypedFloatLiteral(const Expr *e) {
  const auto *l = dyn_cast<FloatLitExpr>(e);
  return l && l->Suffix.empty();
}
} // namespace

Type *Sema::promote(Type *a, Type *b) {
  if (!a || a->isError())
    return b;
  if (!b || b->isError())
    return a;
  if (a == b)
    return a;
  if (a->isNever())
    return b;
  if (b->isNever())
    return a;
  if (isImplicitlyConvertible(b, a))
    return a;
  if (isImplicitlyConvertible(a, b))
    return b;
  // Mixed signedness of the same width has no lossless winner; widen instead.
  if (a->isInt() && b->isInt()) {
    unsigned w = std::max(a->intWidth(), b->intWidth());
    return Types.intType(std::min(64u, w * 2), true);
  }
  return nullptr;
}

void Sema::ensureTemplateSignature(FunctionDecl *fn) {
  if (fn->Ty) {
    // Its signature is known, but a deferred body may still be waiting.
    checkDeferredMethod(fn);
    return;
  }
  auto savedGenerics = ActiveGenericParams;
  Type *savedSelf = ActiveSelfType;
  // A template's signature is resolved the first time somebody calls it, which
  // may be from another module entirely. The names in it are the ones visible
  // where it was *written*, so check it in its own scope.
  Scope *savedScope = CurScope;
  Module *savedModule = CurModule;
  auto mit = ModulesByName.find(fn->ModulePath);
  if (mit != ModulesByName.end()) {
    CurModule = mit->second;
    CurScope = scopeForModule(mit->second);
  }
  // The owning type's parameters first, then the method's own, so a generic
  // method on `Holder<i64>` sees both `T = i64` and its own `U`.
  bindOwnerGenerics(fn, ActiveGenericParams);
  for (size_t i = 0; i < fn->Generics.size(); ++i)
    ActiveGenericParams[fn->Generics[i].Name] =
        Types.genericParam(fn->Generics[i].Name, static_cast<unsigned>(i));
  if (fn->Parent)
    if (auto *nd = dyn_cast<NominalDecl>(fn->Parent))
      ActiveSelfType = nd->DeclaredType;

  std::vector<Type *> params;
  for (Param &p : fn->Params) {
    if (p.IsSelf) {
      p.Ty = selfTypeFor(p, ActiveSelfType ? ActiveSelfType
                                           : Types.errorType());
      continue;
    }
    p.Ty = resolveTypeOrError(p.TypeAnnotation.get(), Types.errorType());
    params.push_back(p.Ty);
  }
  Type *ret = resolveReturnType(fn, ActiveSelfType);
  fn->Ty = Types.functionOf(params, ret, fn->IsVariadic);

  CurScope = savedScope;
  CurModule = savedModule;
  ActiveGenericParams = savedGenerics;
  ActiveSelfType = savedSelf;

  // A signature held back because it wraps `Self` was held back with its
  // body. Now that somebody has asked for it, both are due.
  checkDeferredMethod(fn);
}

//===----------------------------------------------------------------------===//
// Dispatch
//===----------------------------------------------------------------------===//

Type *Sema::checkExpr(Expr *e, Type *expected) {
  if (!e)
    return Types.errorType();
  if (e->Ty)
    return e->Ty; // already checked (shared subtrees from desugaring)

  // Whether *this* expression's value is wanted. Cleared straight away, so a
  // subexpression is only ever discarded when the construct checking it says
  // so explicitly.
  const bool discarded = ValueDiscarded;
  ValueDiscarded = false;

  Type *result = Types.errorType();
  switch (e->Kind) {
  case NodeKind::IntLit: {
    auto *l = cast<IntLitExpr>(e);
    if (!l->Suffix.empty()) {
      result = Types.builtinNamed(l->Suffix);
      if (!result)
        result = Types.i64();
    } else if (expected && expected->isInt()) {
      result = expected;
    } else if (expected && expected->isFloat()) {
      result = expected;
    } else if (expected && expected->is(TypeKind::Char)) {
      result = expected;
    } else {
      result = Types.i64();
    }
    break;
  }
  case NodeKind::FloatLit: {
    auto *l = cast<FloatLitExpr>(e);
    if (!l->Suffix.empty()) {
      result = Types.builtinNamed(l->Suffix);
      if (!result)
        result = Types.f64();
    } else if (expected && expected->isFloat()) {
      result = expected;
    } else {
      result = Types.f64();
    }
    break;
  }
  case NodeKind::StringLit: {
    auto *l = cast<StringLitExpr>(e);
    const bool wantsString = expected && expected->is(TypeKind::String);
    const bool ownCode = CurModule && !CurModule->IsStdlib;
    if ((expected && expected->is(TypeKind::CString)) ||
        (CStringLiterals && ownCode && !wantsString)) {
      l->AsCString = true;
      result = Types.cstringType();
    } else {
      result = Types.stringType();
    }
    break;
  }
  case NodeKind::CharLit:
    result = expected && expected->isInt() ? expected : Types.charType();
    break;
  case NodeKind::BoolLit:
    result = Types.boolType();
    break;
  case NodeKind::NilLit: {
    // `nil` is `Option::None`; the context has to say which Option.
    if (expected && isOptionType(expected)) {
      result = expected;
    } else if (expected && !expected->isError()) {
      result = optionOf(expected, e->Range);
    } else {
      Diags.error(e->Range, "cannot tell what `nil` should be here")
          .note("annotate the binding, e.g. `value: i64? = nil`, or write "
                "`Option::<T>::None`")
          .code(300);
    }
    break;
  }

  case NodeKind::SelfRef: {
    auto *s = cast<SelfExpr>(e);
    FunctionContext *f = fn();
    if (!f || !f->SelfVar) {
      Diags.error(e->Range, "`self` is only available inside a method")
          .note("add a `self` parameter, e.g. `fn name(&self)`")
          .code(301);
      break;
    }
    s->Binding = f->SelfVar;
    noteCapture(f->SelfVar, e->Range);
    result = f->SelfVar->Ty;
    e->Category = ValueCategory::LValue;
    break;
  }

  case NodeKind::SuperRef: {
    FunctionContext *f = fn();
    if (!f || !f->SelfClass || !f->SelfClass->Super) {
      Diags.error(e->Range, "`super` needs an enclosing class with a base class")
          .note("declare the base with `class Derived : Base { ... }`")
          .code(302);
      break;
    }
    result = f->SelfClass->Super->DeclaredType;
    break;
  }

  case NodeKind::DeclRef:
    result = checkDeclRef(cast<DeclRefExpr>(e), expected);
    break;
  case NodeKind::Block:
    result = checkBlock(cast<BlockExpr>(e), expected);
    break;
  case NodeKind::Unary:
    result = checkUnary(cast<UnaryExpr>(e), expected);
    break;
  case NodeKind::Binary:
    result = checkBinary(cast<BinaryExpr>(e), expected);
    break;
  case NodeKind::Assign:
    result = checkAssign(cast<AssignExpr>(e));
    break;
  case NodeKind::Call:
    result = checkCall(cast<CallExpr>(e), expected);
    break;
  case NodeKind::Member:
    result = checkMember(cast<MemberExpr>(e), expected, /*forCall=*/false);
    break;
  case NodeKind::Index:
    result = checkIndex(cast<IndexExpr>(e), expected);
    break;
  case NodeKind::Cast:
    result = checkCast(cast<CastExpr>(e));
    break;

  case NodeKind::Into:
    result = checkInto(cast<IntoExpr>(e));
    break;
  case NodeKind::If:
    result = checkIf(cast<IfExpr>(e), expected, discarded);
    break;
  case NodeKind::Match:
    result = checkMatch(cast<MatchExpr>(e), expected, discarded);
    break;
  case NodeKind::For:
    result = checkFor(cast<ForExpr>(e));
    break;
  case NodeKind::Closure:
    result = checkClosure(cast<ClosureExpr>(e), expected);
    break;
  case NodeKind::StructLit:
    result = checkStructLit(cast<StructLitExpr>(e), expected);
    break;
  case NodeKind::ArrayLit:
    result = checkArrayLit(cast<ArrayLitExpr>(e), expected);
    break;
  case NodeKind::Try:
    result = checkTry(cast<TryExpr>(e));
    break;

  case NodeKind::TupleLit: {
    auto *t = cast<TupleLitExpr>(e);
    std::vector<Type *> elems;
    const std::vector<Type *> *hint =
        expected && expected->is(TypeKind::Tuple) &&
                expected->tupleElements().size() == t->Elements.size()
            ? &expected->tupleElements()
            : nullptr;
    for (size_t i = 0; i < t->Elements.size(); ++i)
      elems.push_back(checkExpr(t->Elements[i].get(), hint ? (*hint)[i] : nullptr));
    result = Types.tupleOf(std::move(elems));
    break;
  }

  case NodeKind::TypeTest: {
    auto *tt = cast<TypeTestExpr>(e);
    Type *ot = checkExpr(tt->Operand.get(), nullptr);
    // A borrowed object is tested as the object: `item is i64` on an item a
    // container lends.
    while (ot->is(TypeKind::Pointer) && !ot->isRawPointer() &&
           !ot->isWeakPointer() && ot->pointee()) {
      auto d = std::make_unique<DerefExpr>();
      d->Range = tt->Operand->Range;
      d->Operand = std::move(tt->Operand);
      tt->Operand = std::move(d);
      ot = checkExpr(tt->Operand.get(), nullptr);
    }
    Type *target = resolveTypeOrError(tt->TargetType.get(), Types.errorType());
    if (!ot->isError() && !target->isError()) {
      bool bothClasses = ot->is(TypeKind::Class) && target->is(TypeKind::Class);
      bool dynTest = ot->is(TypeKind::DynMark);
      // An `Any` remembers what it was built from, whatever that was, so the
      // question is open to every type a value can have.
      if (ot->isAny()) {
        if (!isStorableInAny(target))
          Diags.error(tt->TargetType->Range,
                      "an `Any` can never hold a '{}'", target->toString())
              .note("the test would always be false")
              .code(245);
      } else if (!bothClasses && !dynTest) {
        Diags.error(e->Range,
                    "`is` tests the dynamic type of a class, a `dyn` value or "
                    "an `Any`, but '{}' is none of those",
                    ot->toString())
            .note("use `match` to inspect an enum, or a comparison for values")
            .code(303);
      }
    }
    result = Types.boolType();
    break;
  }

  case NodeKind::Borrow: {
    auto *b = cast<BorrowExpr>(e);
    Type *inner = checkExpr(b->Operand.get(), nullptr);
    // A string literal is interned once and never freed, so `&"text"` is a
    // borrow of something that outlives everything — as safe as it reads.
    bool literal = isa<StringLitExpr>(b->Operand.get()) && !b->IsMutable;
    if (!isLValue(b->Operand.get()) && !literal) {
      Diags.error(b->Operand->Range, "cannot borrow a temporary value")
          .note("bind it to a name first, then borrow that")
          .code(304);
    } else if (b->IsMutable) {
      requireMutable(b->Operand.get(), "mutably borrow");
    }
    // `&value` on a `uniq` is an ordinary borrow of the class: the point of
    // taking one is to reach the object without becoming its second owner.
    result = Types.pointerTo(TypeContext::stripUniq(inner), b->IsMutable,
                             /*raw=*/false);
    break;
  }

  case NodeKind::Move: {
    auto *m = cast<MoveExpr>(e);
    result = checkMove(m);
    break;
  }

  case NodeKind::Deref: {
    auto *d = cast<DerefExpr>(e);
    Type *inner = checkExpr(d->Operand.get(), nullptr);
    if (inner->is(TypeKind::Pointer)) {
      if (inner->isRawPointer())
        reportUnsafe(e->Range, "dereference of a raw pointer",
                     "wrap it in `unsafe { ... }`, or mark the function "
                     "@unsafe, or justify it with @safe(\"reason\")");
      result = inner->pointee();
      e->Category = ValueCategory::LValue;
    } else if (FunctionDecl *impl = lookupOperator(inner, "deref")) {
      // `bind operator::deref to Handle<T>` makes `*handle` mean what the
      // type says it means.
      d->OverloadResolved = impl;
      ensureTemplateSignature(impl);
      result = impl->Ty ? impl->Ty->result() : Types.errorType();
      // Writable only if the type also says how to write.
      if (lookupOperator(inner, "deref_set"))
        e->Category = ValueCategory::LValue;
    } else if (!inner->isError()) {
      auto diag = Diags.error(e->Range,
                              "cannot dereference a value of type '{}'",
                              inner->toString());
      diag.note("only `&T` and `*T` can be dereferenced")
          .code(305);
      if (inner->isNominal())
        diag.note(fmt("give it a meaning with `bind operator::deref to {}`",
                         inner->toString())
                      .c_str());
    }
    break;
  }

  case NodeKind::Range: {
    auto *r = cast<RangeExpr>(e);
    Type *lo = r->Lo ? checkExpr(r->Lo.get(), nullptr) : nullptr;
    Type *hi = r->Hi ? checkExpr(r->Hi.get(), lo) : nullptr;
    Type *elem = lo ? lo : (hi ? hi : Types.i64());
    if (lo && hi) {
      Type *p = promote(lo, hi);
      elem = p ? p : lo;
    }
    if (!elem->isInt() && !elem->is(TypeKind::Char) && !elem->isError())
      Diags.error(e->Range, "ranges need integer or Character bounds, not '{}'",
                  elem->toString())
          .code(306);
    result = Types.tupleOf({elem, elem});
    break;
  }

  case NodeKind::UnsafeBlock: {
    auto *u = cast<UnsafeBlockExpr>(e);
    FunctionContext *f = fn();
    bool saved = f ? f->InUnsafeContext : false;
    if (f)
      f->InUnsafeContext = true;
    result = checkBlock(u->Body.get(), expected);
    if (f)
      f->InUnsafeContext = saved;
    break;
  }

  case NodeKind::Return: {
    auto *r = cast<ReturnExpr>(e);
    FunctionContext *f = fn();
    Type *want = f ? f->ReturnType : Types.voidType();
    if (r->Value) {
      Type *got = checkExpr(r->Value.get(), want);
      if (insertImplicitConversion(r->Value, got, want))
        got = r->Value->Ty;
      requireConvertible(r->Value.get(), got, want, "this `return`");
    } else if (want && !want->isVoid() && !want->isError()) {
      auto d = Diags.error(e->Range, "expected '{}' — got '()'",
                           want->toString());
      d.note("this `return` has no value").code(307);
      if (f && f->Fn && f->Fn->ReturnType)
        d.related(f->Fn->ReturnType->Range, "the declared result type",
                  "return a value of this type, or drop the `->` clause");
    }
    result = Types.neverType();
    break;
  }

  case NodeKind::Break: {
    auto *b = cast<BreakExpr>(e);
    FunctionContext *f = fn();
    if (!f || f->Loops.empty()) {
      Diags.error(e->Range, "`break` is only valid inside a loop").code(308);
      result = Types.neverType();
      break;
    }
    LoopInfo *target = &f->Loops.back();
    if (!b->Label.empty()) {
      target = nullptr;
      for (auto it = f->Loops.rbegin(); it != f->Loops.rend(); ++it)
        if (it->Label == b->Label) {
          target = &*it;
          break;
        }
      if (!target) {
        Diags.error(e->Range, "no enclosing loop is labelled '{}'", b->Label)
            .note("label a loop by writing `name: loop { ... }`")
            .code(309);
        result = Types.neverType();
        break;
      }
    }
    if (b->Value) {
      Type *vt = checkExpr(b->Value.get(), target->BreakType);
      if (!target->IsValueLoop) {
        Diags.error(b->Value->Range,
                    "only `loop` can produce a value with `break`")
            .note("`while` and `for` always evaluate to `()`")
            .code(310);
      } else {
        target->BreakType = target->BreakType
                                ? unifyBranches(target->BreakType, vt,
                                                b->Value.get(), "this loop")
                                : vt;
      }
    } else if (target->IsValueLoop && target->BreakType &&
               !target->BreakType->isVoid()) {
      Diags.error(e->Range, "expected '{}' — got '()'",
                  target->BreakType->toString())
          .note("every `break` out of a value-producing loop must carry a value")
          .code(310);
    } else if (target->IsValueLoop && !target->BreakType) {
      target->BreakType = Types.voidType();
    }
    result = Types.neverType();
    break;
  }

  case NodeKind::Continue: {
    auto *c = cast<ContinueExpr>(e);
    FunctionContext *f = fn();
    if (!f || f->Loops.empty()) {
      Diags.error(e->Range, "`continue` is only valid inside a loop").code(308);
    } else if (!c->Label.empty()) {
      bool found = false;
      for (const LoopInfo &li : f->Loops)
        if (li.Label == c->Label)
          found = true;
      if (!found)
        Diags.error(e->Range, "no enclosing loop is labelled '{}'", c->Label)
            .code(309);
    }
    result = Types.neverType();
    break;
  }

  case NodeKind::While: {
    auto *w = cast<WhileExpr>(e);
    Type *ct = checkExpr(w->Cond.get(),
                         w->BindingPat ? nullptr : Types.boolType());
    if (rewriteAnyTypeTest(w->Cond, w->BindingPat, ct))
      ct = Types.boolType();
    FunctionContext *f = fn();
    if (f)
      f->Loops.push_back(LoopInfo{w->Label, nullptr, false, w->Range});
    pushScope(ScopeKind::Loop);
    if (w->BindingPat) {
      // `while value is Some(v)`: the loop keeps going while the pattern
      // matches, and its bindings are fresh on every turn.
      Type *subject = ct;
      while (subject->is(TypeKind::Pointer))
        subject = subject->pointee();
      checkPattern(w->BindingPat.get(), subject, /*declaresBindings=*/false,
                   /*isMutable=*/false);
    } else if (!ct->isBool() && !ct->isError()) {
      auto d = Diags.error(w->Cond->Range, "expected 'bool' — got '{}'",
                           ct->toString());
      d.note("a `while` condition must be a boolean").code(311);
      if (isOptionType(ct))
        d.note("loop while it holds a value with `while value is Some(v)`");
    }
    ValueDiscarded = true;
    checkBlock(w->Body.get(), nullptr);
    popScope();
    if (f)
      f->Loops.pop_back();
    result = Types.voidType();
    break;
  }

  case NodeKind::Loop: {
    auto *l = cast<LoopExpr>(e);
    FunctionContext *f = fn();
    if (f)
      f->Loops.push_back(LoopInfo{l->Label, nullptr, true, l->Range});
    pushScope(ScopeKind::Loop);
    ValueDiscarded = true;
    checkBlock(l->Body.get(), nullptr);
    popScope();
    Type *breakTy = nullptr;
    if (f) {
      // A bare `break` records `()`, so a null type here means no `break`
      // targeted this loop at all.
      breakTy = f->Loops.back().BreakType;
      f->Loops.pop_back();
    }
    // A `loop` with no `break` never finishes, so it types as `!` — which is
    // what lets one stand where a value is wanted, in a body that only leaves
    // through `return`.
    result = breakTy ? breakTy : Types.neverType();
    break;
  }

  case NodeKind::Error:
    result = Types.errorType();
    break;

  default:
    Diags.error(e->Range, "this expression is not supported yet").code(399);
    break;
  }

  e->Ty = result ? result : Types.errorType();
  // A `some` whose owner has not been checked yet is checked now, so that
  // whatever this value is asked next — a method, a field, a conversion —
  // has an answer. Inside the owner itself the type stays open until the
  // body fixes it.
  if (e->Ty->isOpaque() && !e->Ty->opaqueUnderlying() &&
      !(fn() && fn()->Fn == e->Ty->opaqueOwner()))
    resolveOpaqueNow(e->Ty, e->Range);
  return e->Ty;
}

//===----------------------------------------------------------------------===//
// Blocks
//===----------------------------------------------------------------------===//

Type *Sema::checkBlock(BlockExpr *b, Type *expected) {
  if (!b)
    return Types.voidType();
  // A block's value is its tail, so a block nobody wants the value of does not
  // want its tail's value either.
  const bool discarded = ValueDiscarded;
  ValueDiscarded = false;
  pushScope(ScopeKind::Block);
  for (auto &s : b->Stmts)
    checkStmt(s.get());
  Type *result = Types.voidType();
  if (b->Tail) {
    ValueDiscarded = discarded;
    result = checkExpr(b->Tail.get(), expected);
  }
  popScope();
  b->Ty = result;
  return result;
}

//===----------------------------------------------------------------------===//
// Names
//===----------------------------------------------------------------------===//

/// `.name` where the context says nothing, or says a type with no such
/// member. Both are worth different words.
Type *Sema::reportInferredPathFailure(DeclRefExpr *r, Type *expected) {
  const std::string &name = r->Path.empty() ? std::string() : r->Path[0];
  if (!expected || expected->isError()) {
    auto d = Diags.error(r->Range,
                         "nothing here says which type '.{}' belongs to",
                         name);
    d.note("a leading `.` names something on the type the context expects, "
           "and this place expects nothing in particular");
    d.note(fmt("write the type out: `Type::{}`", name).c_str());
    d.code(204);
    return Types.errorType();
  }
  auto d = Diags.error(r->Range, "'{}' has no '{}'", expected->toString(),
                       name);
  d.note(fmt("`.{}` names something on '{}', which is the type wanted here",
             name, expected->toString())
             .c_str());
  d.code(204);
  if (expected->isNominal() && expected->nominal())
    noteDeclaredAt(d, static_cast<Decl *>(expected->nominal()),
                   "declared here", "no member of that name");
  return Types.errorType();
}

Type *Sema::checkDeclRef(DeclRefExpr *r, Type *expected) {
  Symbol *sym = resolveInferredPath(r, expected);
  if (!sym && r->FromInferredType)
    return reportInferredPathFailure(r, expected);
  if (!sym)
    sym = lookupPath(r->Path, r->Range, /*quiet=*/true);
  // A bare name two enums both claim is settled by what the context wants,
  // which is the usual case once one enum inherits from another.
  if (!sym && r->Path.size() == 1)
    sym = variantOfExpected(r->Path[0], expected);
  if (!sym)
    sym = lookupPath(r->Path, r->Range, /*quiet=*/false);
  if (!sym)
    return Types.errorType();

  switch (sym->Kind) {
  case SymbolKind::Value: {
    r->Resolved = sym->D;
    r->Category = ValueCategory::LValue;
    checkAvailableUnderZombie(sym->D, r->Range);
    if (auto *v = dyn_cast<VarDecl>(sym->D)) {
      noteCapture(v, r->Range);
      // Once a `uniq` local has been moved out of, the name no longer refers
      // to anything: the value it held is somewhere else now.
      auto moved = MovedFrom.find(v);
      if (moved != MovedFrom.end()) {
        auto d = Diags.error(r->Range, "'{}' has been moved out of", v->Name);
        if (v->Ty && v->Ty->isUniq())
          d.note("a `Unique` reference has one owner, so handing it over "
                 "leaves the source empty");
        else
          d.note("this value owns something a reference count cannot see, so "
                 "handing it on gives it away rather than making a second "
                 "owner of it");
        if (moved->second)
          d.related(moved->second->Range, "moved here");
        d.code(239);
        return v->Ty ? v->Ty : Types.errorType();
      }
      return v->Ty ? v->Ty : Types.errorType();
    }
    if (auto *g = dyn_cast<GlobalVarDecl>(sym->D))
      return g->Ty ? g->Ty : Types.errorType();
    return sym->Ty ? sym->Ty : Types.errorType();
  }

  case SymbolKind::Function: {
    auto *f = cast<FunctionDecl>(sym->D);
    r->Resolved = f;
    if (!f->Generics.empty()) {
      // A generic function used as a value needs its arguments spelled out.
      std::vector<Type *> args;
      for (const auto &ga : r->GenericArgs)
        args.push_back(resolveTypeOrError(ga.get(), Types.errorType()));
      if (args.size() != f->Generics.size()) {
        auto d = Diags.error(r->Range,
                             "'{}' is generic; give its type arguments with "
                             "`{}::<...>`",
                             f->Name, f->Name);
        d.code(312);
        noteDeclaredAt(d, f, "declared here",
                       "generic parameters cannot be inferred here");
        return Types.errorType();
      }
      FunctionDecl *inst = instantiate(f, args, r->Range);
      if (!inst)
        return Types.errorType();
      r->Resolved = inst;
      return asCFunctionIfWanted(inst, expected, r->Range);
    }
    ensureTemplateSignature(f);
    if (!f->Ty)
      return Types.errorType();
    return asCFunctionIfWanted(f, expected, r->Range);
  }

  case SymbolKind::Variant: {
    auto *e = sym->Owner;
    const int variantIndex = sym->VariantIndex;
    if (!e->Generics.empty()) {
      e = instantiateVariantOwner(e, static_cast<unsigned>(variantIndex),
                                  r->GenericArgs, /*argTypes=*/{}, expected,
                                  r->Range);
      if (!e)
        return Types.errorType();
    }
    auto *variant = e->Variants[static_cast<size_t>(variantIndex)].get();
    r->Resolved = e;
    r->VariantIndex = variantIndex;
    if (variant->Shape != VariantShape::Unit) {
      auto d = Diags.error(r->Range, "variant '{}' needs its payload",
                           variant->Name);
      d.note(variant->Shape == VariantShape::Tuple
                 ? "construct it with arguments, e.g. `Name(value)`"
                 : "construct it with fields, e.g. `Name { field: value }`")
          .code(313);
      noteDeclaredAt(d, variant, "declared here", "this variant carries data");
      return Types.errorType();
    }
    return e->DeclaredType ? e->DeclaredType : Types.errorType();
  }

  case SymbolKind::TypeName:
  case SymbolKind::MarkName: {
    r->Resolved = sym->D;
    // Bare type names are only meaningful as the callee of a constructor call;
    // checkCall inspects the DeclRef before asking for its type.
    auto d = Diags.error(r->Range, "'{}' is a type, not a value", sym->Name);
    if (isa<StructDecl>(sym->D))
      d.note(fmt("build one with `{} {{ field: value }}`", sym->Name).c_str());
    else if (isa<ClassDecl>(sym->D))
      d.note(fmt("build one by calling it, e.g. `{}(...)`", sym->Name).c_str());
    d.code(314);
    return Types.errorType();
  }

  case SymbolKind::ModuleName: {
    Diags.error(r->Range, "'{}' names a module, not a value", sym->Name)
        .note("qualify the item you want, e.g. `module::item`")
        .code(315);
    return Types.errorType();
  }
  }
  return Types.errorType();
}

//===----------------------------------------------------------------------===//
// Operators
//===----------------------------------------------------------------------===//

Type *Sema::checkUnary(UnaryExpr *u, Type *expected) {
  Type *ot = checkExpr(u->Operand.get(), expected);
  if (ot->isError())
    return ot;
  if (rejectOpaqueUse(ot, u->OpRange, fmt("`{}`", unaryOpSpelling(u->Op))))
    return Types.errorType();

  switch (u->Op) {
  case UnaryOp::Neg:
    if (ot->isNumeric())
      return ot;
    break;
  case UnaryOp::Not:
    if (ot->isBool())
      return ot;
    break;
  case UnaryOp::BitNot:
    if (ot->isInt())
      return ot;
    break;
  }

  if (const char *method = unaryOpMarkMethod(u->Op)) {
    if (FunctionDecl *impl = lookupOperator(ot, method)) {
      u->OverloadResolved = impl;
      ensureTemplateSignature(impl);
      return impl->Ty ? impl->Ty->result() : Types.errorType();
    }
  }

  auto d = Diags.error(u->Range, "'{}' does not apply to '{}'",
                       unaryOpSpelling(u->Op), ot->toString());
  d.code(320);
  if (const char *method = unaryOpMarkMethod(u->Op))
    d.note(fmt("overload it with `bind operator::{} to {}`", method,
                  ot->toString())
               .c_str());
  return Types.errorType();
}

Type *Sema::checkBinary(BinaryExpr *b, Type *expected) {
  // Logical operators are short-circuiting and always boolean.
  if (b->Op == BinaryOp::LogicalAnd || b->Op == BinaryOp::LogicalOr) {
    Type *lt = checkExpr(b->LHS.get(), Types.boolType());
    Type *rt = checkExpr(b->RHS.get(), Types.boolType());
    if (!lt->isBool() && !lt->isError())
      Diags.error(b->LHS->Range, "expected 'bool' — got '{}'", lt->toString())
          .note(fmt("the left side of `{}` must be a boolean",
                       binaryOpSpelling(b->Op))
                    .c_str())
          .code(321);
    if (!rt->isBool() && !rt->isError())
      Diags.error(b->RHS->Range, "expected 'bool' — got '{}'", rt->toString())
          .note(fmt("the right side of `{}` must be a boolean",
                       binaryOpSpelling(b->Op))
                    .c_str())
          .code(321);
    return Types.boolType();
  }

  // `a ?? b` yields the element type of the optional on the left.
  if (b->Op == BinaryOp::Coalesce) {
    Type *lt = checkExpr(b->LHS.get(), nullptr);
    if (!isOptionType(lt)) {
      if (!lt->isError())
        Diags.error(b->LHS->Range,
                    "`??` needs an Option on the left, but got '{}'",
                    lt->toString())
            .note("only `T?` (that is, `Option<T>`) can be defaulted with `??`")
            .code(322);
      checkExpr(b->RHS.get(), nullptr);
      return lt;
    }
    Type *elem = optionPayload(lt);
    // `v.peek(i) ?? -1`: a lent number defaults to a number. The borrow is
    // read through, as `let n: i64 = r` would, when the default is plain
    // data of the type it points at.
    if (elem && elem->is(TypeKind::Pointer) && !elem->isRawPointer() &&
        !elem->isMutablePointer() && elem->pointee() &&
        isImplicitlyConvertible(elem, elem->pointee())) {
      Diags.beginSpeculation();
      Type *probe = checkExpr(b->RHS.get(), elem->pointee());
      Diags.endSpeculation();
      if (!probe->isError() && !isImplicitlyConvertible(probe, elem) &&
          isImplicitlyConvertible(probe, elem->pointee()))
        elem = elem->pointee();
    }
    Type *rt = checkExpr(b->RHS.get(), elem);
    requireConvertible(b->RHS.get(), rt, elem, "the default of `??`");
    return elem;
  }

  // Check the side that cannot adapt first, so literals take the other's type.
  Type *lt, *rt;
  if ((isUntypedIntLiteral(b->LHS.get()) || isUntypedFloatLiteral(b->LHS.get())) &&
      !isUntypedIntLiteral(b->RHS.get())) {
    rt = checkExpr(b->RHS.get(), expected);
    lt = checkExpr(b->LHS.get(), rt);
  } else {
    lt = checkExpr(b->LHS.get(), isComparison(b->Op) ? nullptr : expected);
    rt = checkExpr(b->RHS.get(), lt);
  }
  if (lt->isError() || rt->isError())
    return Types.errorType();

  // A borrowed number, boolean, character or string takes part in `+`, `==`
  // and the rest as the value it points at: `*` is read for the program,
  // since nothing else could be meant. (An overloaded operator on a `&T`
  // is looked for first — below — so this only applies to the builtins.)
  // A borrow of a borrow — an item of a borrowing iterator handed to a
  // predicate, `&&i64` — reads through as many as there are.
  std::function<void(ExprPtr &, Type *&)> autoDeref = [&](ExprPtr &side,
                                                         Type *&t) {
    if (!t->is(TypeKind::Pointer) || t->isRawPointer() || t->isWeakPointer())
      return;
    Type *inner = t->pointee();
    if (inner && inner->is(TypeKind::Pointer) && !inner->isRawPointer() &&
        !inner->isWeakPointer()) {
      Type *base = inner;
      while (base->is(TypeKind::Pointer) && !base->isRawPointer() &&
             !base->isWeakPointer() && base->pointee())
        base = base->pointee();
      if (!(base->isNumeric() || base->isBool() || base->is(TypeKind::Char) ||
            base->is(TypeKind::String)))
        return;
      auto d = std::make_unique<DerefExpr>();
      d->Range = side->Range;
      d->Ty = inner;
      d->Category = ValueCategory::LValue;
      d->Operand = std::move(side);
      side = std::move(d);
      t = inner;
      autoDeref(side, t);
      return;
    }
    if (!inner || !(inner->isNumeric() || inner->isBool() ||
                    inner->is(TypeKind::Char) || inner->is(TypeKind::String)))
      return;
    // A nominal type might overload the operator on the reference itself;
    // a builtin — String, a number, a bool, a character — never does, so
    // its borrow always reads through to the value.
    if (inner->isNominal() && lookupMethod(t, binaryOpMarkMethod(b->Op)))
      return;
    auto d = std::make_unique<DerefExpr>();
    d->Range = side->Range;
    d->Ty = inner;
    d->Category = ValueCategory::LValue;
    d->Operand = std::move(side);
    side = std::move(d);
    t = inner;
  };
  autoDeref(b->LHS, lt);
  autoDeref(b->RHS, rt);

  // Whatever is behind a `some` may well add or compare; the caller is not
  // allowed to know that.
  if (rejectOpaqueUse(lt, b->OpRange, fmt("`{}`", binaryOpSpelling(b->Op))) ||
      rejectOpaqueUse(rt, b->OpRange, fmt("`{}`", binaryOpSpelling(b->Op))))
    return Types.errorType();

  const bool comparison = isComparison(b->Op);

  // String concatenation and comparison are builtin.
  if (lt->is(TypeKind::String) && rt->is(TypeKind::String)) {
    if (b->Op == BinaryOp::Add)
      return Types.stringType();
    if (comparison)
      return Types.boolType();
  }

  if (comparison) {
    // A class instance compares by identity — two names for one object — but
    // a class that says what comparing it means gets the last word. The
    // overload is looked for before the reference comparison below, so
    // `bind operator::eq to Handle<T>` is what `h == g` runs.
    if (lt->is(TypeKind::Class) || rt->is(TypeKind::Class)) {
      if (const char *m = binaryOpMarkMethod(b->Op)) {
        if (FunctionDecl *impl = lookupOperator(lt, m, rt)) {
          b->OverloadResolved = impl;
          ensureTemplateSignature(impl);
          return Types.boolType();
        }
      }
    }
    bool comparable = (lt->isNumeric() && rt->isNumeric()) ||
                      (lt->isBool() && rt->isBool()) ||
                      (lt->is(TypeKind::Char) && rt->is(TypeKind::Char)) ||
                      (lt->is(TypeKind::CString) && rt->is(TypeKind::CString)) ||
                      (lt->isPointerLike() && rt->isPointerLike() &&
                       !lt->isAny() && !rt->isAny()) ||
                      (lt->is(TypeKind::Enum) && lt == rt &&
                       reinterpret_cast<EnumDecl *>(lt->nominal())->IsSimple) ||
                      (isOptionType(lt) && lt == rt);
    if (comparable) {
      if (lt->isNumeric() && rt->isNumeric() && !promote(lt, rt)) {
        Diags.error(b->OpRange, "cannot compare '{}' with '{}'", lt->toString(),
                    rt->toString())
            .note("cast one side so both have the same type")
            .code(323);
      }
      return Types.boolType();
    }
    const char *method = binaryOpMarkMethod(b->Op);
    if (method) {
      if (FunctionDecl *impl = lookupOperator(lt, method, rt)) {
        b->OverloadResolved = impl;
        ensureTemplateSignature(impl);
        // `eq` returns bool; `cmp` returns an ordering integer.
        return Types.boolType();
      }
    }
    auto d = Diags.error(b->OpRange, "cannot compare '{}' with '{}'",
                         lt->toString(), rt->toString());
    d.code(323);
    if (method)
      d.note(fmt("implement it with `bind operator::{} to {}`", method,
                    lt->toString())
                 .c_str());
    return Types.boolType();
  }

  // Bitwise and shifts are integer-only.
  bool bitwise = b->Op == BinaryOp::BitAnd || b->Op == BinaryOp::BitOr ||
                 b->Op == BinaryOp::BitXor || b->Op == BinaryOp::Shl ||
                 b->Op == BinaryOp::Shr;
  if (bitwise) {
    if (lt->isInt() && rt->isInt()) {
      if (b->Op == BinaryOp::Shl || b->Op == BinaryOp::Shr)
        return lt;
      Type *p = promote(lt, rt);
      if (p)
        return p;
    }
    if (lt->isBool() && rt->isBool() && !(b->Op == BinaryOp::Shl ||
                                          b->Op == BinaryOp::Shr))
      return Types.boolType();
  } else if (lt->isNumeric() && rt->isNumeric()) {
    Type *p = promote(lt, rt);
    if (p)
      return p;
    Diags.error(b->OpRange, "cannot apply '{}' to '{}' and '{}'",
                binaryOpSpelling(b->Op), lt->toString(), rt->toString())
        .note("cast one side so both operands have the same type")
        .code(324);
    return lt;
  }

  if (const char *method = binaryOpMarkMethod(b->Op)) {
    // Pick the overload by what is on the right, so a type may define `+`
    // once per right-hand type.
    if (FunctionDecl *impl = lookupOperator(lt, method, rt)) {
      b->OverloadResolved = impl;
      ensureTemplateSignature(impl);
      // Verify the right-hand operand suits the overload's parameter.
      size_t paramIdx = 0;
      for (const Param &p : impl->Params)
        if (!p.IsSelf) {
          if (paramIdx == 0 && p.Ty) {
            Type *want = p.Ty;
            if (want->is(TypeKind::Pointer))
              want = want->pointee();
            if (!isImplicitlyConvertible(rt, want))
              requireConvertible(b->RHS.get(), rt, want,
                                 fmt("the right operand of `{}`",
                                        binaryOpSpelling(b->Op))
                                     .c_str());
          }
          ++paramIdx;
        }
      return impl->Ty ? impl->Ty->result() : Types.errorType();
    }
  }

  auto d = Diags.error(b->OpRange, "cannot apply '{}' to '{}' and '{}'",
                       binaryOpSpelling(b->Op), lt->toString(), rt->toString());
  d.code(324);
  if (const char *method = binaryOpMarkMethod(b->Op))
    d.note(fmt("overload it with `bind operator::{} to {}`", method,
                  lt->toString())
               .c_str());
  return Types.errorType();
}

Type *Sema::checkAssign(AssignExpr *a) {
  // Anything on the way down to the place being written may have to be
  // borrowed mutably rather than read: a `Box` in the middle of the chain
  // lends with `touch` rather than `look`. `onWriteSpine` is how the member
  // check asks whether it is on that path.
  struct SpineGuard {
    const Expr *&Slot;
    const Expr *Saved;
    ~SpineGuard() { Slot = Saved; }
  } spine{WriteSpine, WriteSpine};
  WriteSpine = a->LHS.get();

  // Assigning to a local that was moved out of gives it something to hold
  // again, so the name goes back into service. This happens before the left
  // side is looked at, since looking at it is what would complain — and
  // again *after* the whole assignment, because the right-hand side may be
  // what moved out of it: `running = combine(running, value)` hands the old
  // value on and puts a new one back, and the name holds that one.
  VarDecl *restored = nullptr;
  if (a->Op == AssignOp::Assign)
    if (auto *ref = dyn_cast<DeclRefExpr>(a->LHS.get()))
      if (ref->Path.size() == 1)
        if (Symbol *sym = lookupPath(ref->Path, ref->Range, /*quiet=*/true))
          if (auto *v = sym->D ? dyn_cast<VarDecl>(sym->D) : nullptr) {
            MovedFrom.erase(v);
            restored = v;
          }
  struct RestoreGuard {
    std::map<VarDecl *, Expr *> &Moved;
    VarDecl *V;
    ~RestoreGuard() {
      if (V)
        Moved.erase(V);
    }
  } restoreGuard{MovedFrom, restored};

  // `x = value` introduces `x` when nothing of that name is in scope.
  if (a->Op == AssignOp::Assign) {
    if (auto *ref = dyn_cast<DeclRefExpr>(a->LHS.get())) {
      if (ref->Path.size() == 1 && !CurScope->find(ref->Path[0])) {
        Type *rt = checkExpr(a->RHS.get(), nullptr);
        if (rt->isVoid()) {
          Diags.error(a->RHS->Range, "cannot bind a value of type '()'")
              .note("this expression produces no value")
              .code(251);
          rt = Types.errorType();
        }
        VarDecl *v = declareLocal(ref->Path[0], rt, /*isMutable=*/false,
                                  ref->Range);
        a->DeclaresBinding = true;
        a->DeclaredVar = v;
        ref->Resolved = v;
        ref->Ty = rt;
        return Types.voidType();
      }
    }
  }

  Type *lt = checkExpr(a->LHS.get(), nullptr);

  // `*handle = value` on a type that overloads `deref` writes through its
  // `deref_set`: there is no storage here the compiler can see. Everything
  // after this — plain stores, `+=`, the lot — then follows the ordinary
  // rules against the type `*handle` produces.
  if (auto *deref = dyn_cast<DerefExpr>(a->LHS.get())) {
    if (deref->OverloadResolved) {
      Type *inner = deref->Operand->Ty;
      FunctionDecl *setter = lookupOperator(inner, "deref_set");
      if (!setter) {
        auto d = Diags.error(a->Range, "'{}' can be read through `*` but not "
                                       "written through it",
                             inner ? inner->toString() : "?");
        d.note(fmt("add `fn derefSet(&var self, value: {})` beside its "
                      "`deref`", lt ? lt->toString() : "T")
                   .c_str())
            .code(331);
        checkExpr(a->RHS.get(), lt);
        return Types.voidType();
      }
      ensureTemplateSignature(setter);
      Type *want = nullptr;
      for (const Param &p : setter->Params)
        if (!p.IsSelf && !want)
          want = p.Ty;
      if (want && lt && want != lt && !want->isError() && !lt->isError()) {
        auto d = Diags.error(a->Range,
                             "`*` reads '{}' here but writes '{}'",
                             lt->toString(), want->toString());
        d.note("`deref` and `derefSet` must agree on the value's type")
            .code(331);
        noteDeclaredAt(d, setter, "the setter", "its parameter decides");
        checkExpr(a->RHS.get(), lt);
        return Types.voidType();
      }
      a->DerefSetImpl = setter;
      // No mutability check on the receiver: `*h = v` is `h.derefSet(v)`, and
      // a `&var self` method call is not checked either.
    }
  }

  // `buffer[i] = value` on a type that overloads `index` is a call to its
  // `indexSet`. Without one the write would have nowhere to go, so say so
  // rather than letting it look as though it worked.
  if (auto *idx = dyn_cast<IndexExpr>(a->LHS.get())) {
    if (idx->OverloadResolved) {
      Type *recv = idx->Base->Ty;
      while (recv && recv->is(TypeKind::Pointer))
        recv = recv->pointee();
      // The write has to go through the setter that takes the same kind of
      // subscript the read did: a type indexed by both a name and a number
      // has one `indexSet` for each.
      FunctionDecl *setter = nullptr;
      std::vector<FunctionDecl *> setters =
          operatorOverloads(recv, "index_set");
      if (setters.size() > 1) {
        Type *keyTy = nullptr;
        for (const Param &p : idx->OverloadResolved->Params)
          if (!p.IsSelf && !keyTy)
            keyTy = p.Ty;
        std::vector<Type *> args{keyTy, lt};
        bool ambiguous = false;
        setter = pickOverload(setters, args, ambiguous);
        if (!setter) {
          reportNoOverload(setters, args, "indexSet", a->Range, ambiguous);
          checkExpr(a->RHS.get(), lt);
          return Types.voidType();
        }
      } else {
        setter = lookupOperator(recv, "index_set");
      }
      if (!setter) {
        auto d = Diags.error(a->Range,
                             "'{}' can be read with `[]` but not written "
                             "through it",
                             recv ? recv->toString() : "?");
        d.note(fmt("add `fn indexSet(&var self, index: i64, value: {})` "
                      "beside its `index`", lt ? lt->toString() : "T")
                   .c_str())
            .code(332);
        checkExpr(a->RHS.get(), lt);
        return Types.voidType();
      }
      ensureTemplateSignature(setter);
      Type *want = nullptr;
      size_t seen = 0;
      for (const Param &p : setter->Params) {
        if (p.IsSelf)
          continue;
        if (++seen == 2)
          want = p.Ty;
      }
      // A container whose `index` lends the element — `-> &T from self` —
      // and whose `indexSet` takes one by value agrees on the element: the
      // read is a borrow of what the write replaces.
      if (want && lt && lt->is(TypeKind::Pointer) && !lt->isRawPointer() &&
          !lt->isMutablePointer() && lt->pointee() == want)
        lt = want;
      if (want && lt && want != lt && !want->isError() && !lt->isError()) {
        auto d = Diags.error(a->Range, "`[]` reads '{}' here but writes '{}'",
                             lt->toString(), want->toString());
        d.note("`index` and `indexSet` must agree on the element type")
            .code(332);
        noteDeclaredAt(d, setter, "the setter", "its value parameter decides");
        checkExpr(a->RHS.get(), lt);
        return Types.voidType();
      }
      a->IndexSetImpl = setter;
    }
  }

  if (!a->DerefSetImpl && !a->IndexSetImpl && !isLValue(a->LHS.get())) {
    auto *ix = dyn_cast<IndexExpr>(a->LHS.get());
    if (ix && ix->StringChar) {
      Diags.error(a->LHS->Range, "cannot assign into a String")
          .note("`text[i]` reads a character out of the UTF-8; the "
                "characters are not slots. Build a new String instead, "
                "walking the old one with `for c in text`")
          .code(330);
    } else {
      Diags.error(a->LHS->Range, "cannot assign to this expression")
          .note("the left side of an assignment must be a variable, field, "
                "element or dereference")
          .code(330);
    }
    checkExpr(a->RHS.get(), lt);
    return Types.voidType();
  }
  if (!a->DerefSetImpl && !a->IndexSetImpl)
    requireMutable(a->LHS.get(), "assign to");

  // Writing to a captured variable writes to the closure's own copy, so the
  // enclosing scope never sees it. That is easy to write by accident and
  // impossible to notice at run time, so say so here.
  if (const auto *ref = dyn_cast<DeclRefExpr>(a->LHS.get())) {
    if (auto *v = ref->Resolved ? dyn_cast<VarDecl>(ref->Resolved) : nullptr) {
      FunctionContext *fc = fn();
      if (fc && fc->Closure) {
        bool captured = false;
        for (const Capture &cap : fc->Closure->Captures)
          if (cap.Var == v)
            captured = true;
        // An `async fn`'s parameters are captured into its task, and the
        // task is the only place they are ever read: assigning to one is
        // the ordinary thing, not a copy going astray.
        bool asyncParam = fc->Closure->IsAsyncBody && v->IsParam;
        if (captured && !asyncParam) {
          bool task = fc->Closure->IsAsyncBody;
          auto w = Diags.warn(a->Range,
                              "assigning to '{}' only changes the {}'s "
                              "own copy",
                              v->Name, task ? "task" : "closure");
          w.note(task ? "captures are copied when the task is started, so "
                        "the enclosing scope will not see this"
                      : "captures are copied when the closure is made, so "
                        "the enclosing scope will not see this")
              .note("to share one value, capture a class — "
                    "`let total = mem::of(0)` then `*total += v`")
              .code(234);
          w.related(v->NameRange, fmt("'{}' is captured from here", v->Name),
                    "this is the binding the copy was taken from");
        }
      }
    }
  }

  if (a->Op == AssignOp::Assign) {
    Type *rt = checkExpr(a->RHS.get(), lt);
    if (insertImplicitConversion(a->RHS, rt, lt))
      rt = a->RHS->Ty;
    requireConvertible(a->RHS.get(), rt, lt, "this assignment");
    return Types.voidType();
  }

  // Compound assignment: `a += b` follows the rules of `a + b`.
  BinaryOp op = assignOpToBinary(a->Op);
  Type *rt = checkExpr(a->RHS.get(), lt);
  if (lt->isError() || rt->isError())
    return Types.voidType();

  // A borrowed number or string on the right reads through to its value,
  // the same courtesy `a + b` gets: `total += *r` is meant by `total += r`.
  if (rt->is(TypeKind::Pointer) && !rt->isRawPointer() && !rt->isWeakPointer() &&
      rt->pointee() && (rt->pointee()->isNumeric() ||
                        rt->pointee()->is(TypeKind::String))) {
    Type *inner = rt->pointee();
    auto d = std::make_unique<DerefExpr>();
    d->Range = a->RHS->Range;
    d->Ty = inner;
    d->Category = ValueCategory::LValue;
    d->Operand = std::move(a->RHS);
    a->RHS = std::move(d);
    rt = inner;
  }

  bool ok = false;
  if (lt->is(TypeKind::String) && rt->is(TypeKind::String) && op == BinaryOp::Add)
    ok = true;
  else if (op == BinaryOp::Shl || op == BinaryOp::Shr)
    ok = lt->isInt() && rt->isInt();
  else if (op == BinaryOp::BitAnd || op == BinaryOp::BitOr ||
           op == BinaryOp::BitXor)
    ok = (lt->isInt() && rt->isInt()) || (lt->isBool() && rt->isBool());
  else
    ok = lt->isNumeric() && rt->isNumeric() && isImplicitlyConvertible(rt, lt);

  if (!ok) {
    if (const char *method = binaryOpMarkMethod(op)) {
      if (FunctionDecl *impl = lookupOperator(lt, method, rt)) {
        a->OperatorImpl = impl;
        ensureTemplateSignature(impl);
        Type *res = impl->Ty ? impl->Ty->result() : Types.errorType();
        requireConvertible(a->RHS.get(), res, lt,
                           fmt("`{}`", assignOpSpelling(a->Op)).c_str());
        return Types.voidType();
      }
    }
    auto d = Diags.error(a->OpRange, "cannot apply '{}' to '{}' and '{}'",
                         assignOpSpelling(a->Op), lt->toString(), rt->toString());
    d.code(324);
    if (const char *method = binaryOpMarkMethod(op))
      d.note(fmt("overload it with `bind operator::{} to {}`", method,
                    lt->toString())
                 .c_str());
  }
  return Types.voidType();
}

//===----------------------------------------------------------------------===//
// Member access and indexing
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Reaching through a stand-in
//
// A `Handle`, a `Box`, an `Rc` — anything that holds a value and lends it —
// answers `.` with the value's own members. What makes a type one of these is
// that it lends: a `look(&self) -> &T from self` to read the value where it
// lies, and a `touch(&var self) -> &var T from self` to write it. Those two
// names are the standard library's own convention, and this is where the
// language takes them at their word.
//
// It can never hide anything. The reach-through only runs once the member has
// *not* been found on the stand-in itself, so a `Box` with a `length` of its
// own keeps it, and only a name it does not have goes through to the value.
//===----------------------------------------------------------------------===//

FunctionDecl *Sema::pointeeAccessor(Type *receiver, bool wantMutable) {
  if (!receiver || !receiver->isNominal())
    return nullptr;
  FunctionDecl *fn = lookupMethod(receiver, wantMutable ? "touch" : "look");
  if (!fn || !fn->Body)
    return nullptr;
  ensureTemplateSignature(fn);
  if (!fn->Ty)
    return nullptr;
  // No arguments, a `self` to lend from, and a borrow of something else as
  // the result. A `look` that takes an index or hands back a value is some
  // other method that happens to share the name.
  bool hasSelf = false;
  for (const Param &p : fn->Params) {
    if (p.IsSelf) {
      hasSelf = true;
      continue;
    }
    if (!p.DefaultValue)
      return nullptr;
  }
  if (!hasSelf)
    return nullptr;
  Type *result = fn->Ty->result();
  if (!result || !result->is(TypeKind::Pointer) || result->isRawPointer() ||
      result->isWeakPointer())
    return nullptr;
  if (result->isMutablePointer() != wantMutable)
    return nullptr;
  Type *target = result->pointee();
  if (!target || target->isError() || target == receiver)
    return nullptr;
  return fn;
}

bool Sema::onWriteSpine(const Expr *e) const {
  const Expr *cur = WriteSpine;
  for (unsigned steps = 0; cur && steps < 64; ++steps) {
    if (cur == e)
      return true;
    if (const auto *mem = dyn_cast<MemberExpr>(cur)) {
      cur = mem->Base.get();
      continue;
    }
    if (const auto *idx = dyn_cast<IndexExpr>(cur)) {
      cur = idx->Base.get();
      continue;
    }
    if (const auto *deref = dyn_cast<DerefExpr>(cur)) {
      cur = deref->Operand.get();
      continue;
    }
    return false;
  }
  return false;
}

Type *Sema::reachThroughPointee(MemberExpr *m, Type *expected, bool forCall) {
  if (PointeeDepth > 8 || !m->Base || !m->Base->Ty)
    return nullptr;
  Type *recv = m->Base->Ty;
  while (recv && recv->is(TypeKind::Pointer) && !recv->isRawPointer() &&
         recv->pointee())
    recv = recv->pointee();

  // Reading is the default; writing is asked for by the two things that
  // actually write — an assignment whose left-hand side runs through here,
  // and a method that takes `&var self`.
  bool wantMutable = onWriteSpine(m);
  FunctionDecl *acc = pointeeAccessor(recv, /*wantMutable=*/false);
  if (!acc)
    return nullptr;
  if (!wantMutable) {
    Type *target = acc->Ty->result()->pointee();
    if (FunctionDecl *cand = lookupMethod(target, m->Name)) {
      ensureTemplateSignature(cand);
      for (const Param &p : cand->Params) {
        if (!p.IsSelf)
          continue;
        // `&var self` is a mutable borrow however it is represented: a
        // pointer for a value type, and for a class the written form is what
        // says so, since a class reference is already a pointer.
        if (p.SelfMutable ||
            (p.Ty && p.Ty->is(TypeKind::Pointer) && p.Ty->isMutablePointer()))
          wantMutable = true;
      }
    }
  }
  if (wantMutable) {
    if (FunctionDecl *mut = pointeeAccessor(recv, /*wantMutable=*/true))
      acc = mut;
    else
      wantMutable = false;
  }

  // `base` becomes `base.look()` (or `base.touch()`), and the member is
  // looked for again on what that lends.
  auto through = std::make_unique<MemberExpr>();
  through->Range = m->Base->Range;
  through->NameRange = m->Base->Range;
  through->Name = wantMutable ? "touch" : "look";
  through->Base = std::move(m->Base);
  auto call = std::make_unique<CallExpr>();
  call->Range = through->Range;
  call->ParenRange = through->Range;
  call->Callee = std::move(through);
  call->PointeeAccess = true;
  m->Base = std::move(call);

  ++PointeeDepth;
  Type *result = checkMember(m, expected, forCall);
  --PointeeDepth;
  return result;
}

Type *Sema::checkMember(MemberExpr *m, Type *expected, bool forCall) {
  const bool isSuper = isa<SuperExpr>(m->Base.get());
  // `value::Mark.name()` names the mark to take `name` from, which matters
  // when the type has its own method of that name. It arrives here as a path
  // whose head is a value rather than a module, so split it apart before the
  // ordinary path lookup gets a chance to fail on it.
  if (auto *path = dyn_cast<DeclRefExpr>(m->Base.get())) {
    if (!m->QualifiedMark && path->Path.size() >= 2) {
      if (Symbol *head = CurScope->find(path->Path[0])) {
        if (head->Kind == SymbolKind::Value) {
          std::vector<std::string> rest(path->Path.begin() + 1,
                                        path->Path.end());
          Symbol *msym = lookupPath(rest, m->Range, /*quiet=*/true);
          MarkDecl *mark =
              msym && msym->D ? dyn_cast<MarkDecl>(msym->D) : nullptr;
          if (mark) {
            m->QualifiedMark = mark;
            path->Path.resize(1);
          } else {
            std::string joined;
            for (size_t i = 0; i < rest.size(); ++i) {
              if (i) joined += "::";
              joined += rest[i];
            }
            Diags.error(m->Range, "'{}' is not a mark", joined)
                .note(fmt("`value::Mark.method()` calls the method mark "
                             "`Mark` binds to the value's type")
                          .c_str())
                .code(227);
            return Types.errorType();
          }
        }
      }
    }
  }
  Type *base = checkExpr(m->Base.get(), nullptr);
  if (base->isError())
    return base;

  if (m->QualifiedMark) {
    Type *recvTy = base;
    while (recvTy->is(TypeKind::Pointer)) {
      recvTy = recvTy->pointee();
      ++m->AutoDerefs;
    }
    FunctionDecl *impl = lookupMarkMethod(recvTy, m->QualifiedMark, m->Name);
    if (!impl) {
      bool bound = typeConformsTo(recvTy, m->QualifiedMark);
      auto d = Diags.error(
          m->NameRange,
          bound ? "mark '{}' has no member named '{}'"
                : "'{}' is not bound to mark '{}'",
          bound ? m->QualifiedMark->Name : recvTy->toString(),
          bound ? m->Name : m->QualifiedMark->Name);
      d.code(228);
      noteDeclaredAt(d, m->QualifiedMark, "the mark declared here",
                     bound ? "only its members can be reached this way"
                           : "bind it to this type first");
      return Types.errorType();
    }
    m->ResolvedMethod = impl;
    ensureTemplateSignature(impl);
    if (!forCall) {
      auto d = Diags.error(m->NameRange, "'{}' is a method; call it", m->Name);
      d.note(fmt("write `{}()` to invoke it", m->Name).c_str()).code(343);
      return Types.errorType();
    }
    for (const Param &p : impl->Params)
      if (p.IsSelf && p.SelfByRef && !recvTy->isPointerLike() &&
          !base->is(TypeKind::Pointer))
        m->NeedsAddressOfBase = true;
    return impl->Ty ? impl->Ty : Types.errorType();
  }

  if (isSuper) {
    m->NonVirtual = true;
    FunctionContext *f = fn();
    ClassDecl *super = f && f->SelfClass ? f->SelfClass->Super : nullptr;
    if (super) {
      NominalDecl *owner = nullptr;
      if (FunctionDecl *method =
              lookupMethod(super->DeclaredType, m->Name, &owner)) {
        m->ResolvedMethod = method;
        ensureTemplateSignature(method);
        return method->Ty ? method->Ty : Types.errorType();
      }
      auto d = Diags.error(m->NameRange, "'{}' has no method named '{}'",
                           super->Name, m->Name);
      d.code(340);
      noteDeclaredAt(d, super, "the base class",
                     "only methods declared on the base can be reached with "
                     "`super`");
      return Types.errorType();
    }
    return Types.errorType();
  }

  // `$clone()` copies a value of the type it is written against. Inside an
  // instantiation that type may itself be a borrow — `Option<&T>` cloning its
  // payload, a `T` that is `&Box<i64>` — and then the copy is of the borrow,
  // not of what it points at. So the receiver is looked through only as far
  // as the borrow it arrived in: a value whose type is exactly what a type
  // parameter stands for is not looked through at all, and a borrow of a
  // shared borrow is looked through once.
  auto sharedBorrow = [](Type *t) {
    return t->is(TypeKind::Pointer) && !t->isRawPointer() &&
           !t->isWeakPointer() && !t->isMutablePointer();
  };
  bool cloneOfBorrow = false;
  if (m->IsIntrinsic && m->Name == "clone" && sharedBorrow(base))
    for (const auto &bound : ActiveGenericParams)
      cloneOfBorrow = cloneOfBorrow || bound.second == base;

  // Look through borrows so `(&point).x` and `point.x` behave the same.
  Type *recv = base;
  while (recv->is(TypeKind::Pointer) && !cloneOfBorrow) {
    if (m->IsIntrinsic && m->Name == "clone" && recv != base &&
        sharedBorrow(recv))
      break;
    if (recv->isRawPointer())
      reportUnsafe(m->Range, "field access through a raw pointer",
                   "wrap it in `unsafe { ... }`, or mark the function @unsafe");
    recv = recv->pointee();
    ++m->AutoDerefs;
  }

  // `value.$name()` — a member the compiler provides. The sigil says so, and
  // nothing the type declares is considered.
  if (m->IsIntrinsic) {
    std::vector<Type *> params;
    Type *ret = nullptr;
    BuiltinMethod bm = lookupBuiltinMethod(recv, m->Name, params, ret);
    if (bm == BuiltinMethod::None) {
      auto d = Diags.error(m->NameRange,
                           "'{}' has no builtin member named '{}'",
                           recv->toString(), m->Name);
      d.code(345);
      if (lookupMethod(recv, m->Name))
        d.note(fmt("'{}' declares '{}' itself — drop the `$` to call it",
                      recv->toString(), m->Name)
                   .c_str());
      else
        d.note("`$` names the members the compiler provides: `$length`, "
               "`$isEmpty`, `$at`, `$str` and the rest");
      return Types.errorType();
    }
    m->Builtin = bm;
    if (!forCall) {
      Diags.error(m->NameRange, "'${}' is a method; call it with `${}()`",
                  m->Name, m->Name)
          .code(343);
      return Types.errorType();
    }
    return Types.functionOf(params, ret);
  }

  // Tuple element: `pair.0`
  if (m->IsTupleIndex) {
    if (!recv->is(TypeKind::Tuple)) {
      Diags.error(m->NameRange, "'{}' is not a tuple, so `.{}` does not apply",
                  recv->toString(), m->TupleIndex)
          .code(341);
      return Types.errorType();
    }
    const auto &elems = recv->tupleElements();
    if (m->TupleIndex >= elems.size()) {
      Diags.error(m->NameRange,
                  "tuple '{}' has {} element(s), so `.{}` is out of range",
                  recv->toString(), elems.size(), m->TupleIndex)
          .code(342);
      return Types.errorType();
    }
    m->FieldIndex = static_cast<int>(m->TupleIndex);
    m->Category = ValueCategory::LValue;
    return elems[m->TupleIndex];
  }

  // Fields on structs and classes. A `some` has none anyone may name.
  if (recv->isNominal() && !recv->isOpaque()) {
    NominalDecl *nd = recv->nominal();
    std::vector<NominalDecl *> chain{nd};
    if (auto *c = dyn_cast<ClassDecl>(static_cast<Decl *>(nd)))
      for (ClassDecl *s = c->Super; s; s = s->Super)
        chain.push_back(s);
    for (NominalDecl *n : chain)
      for (const auto &f : n->Fields)
        if (f->Name == m->Name) {
          // A type may hold a field and a method of the same name — `Sheep`
          // with a `name` field and a mark that requires `name()`. The
          // syntax decides: `self.name` is the field, `self.name()` the
          // method, unless the field itself holds something callable.
          bool callableField = f->Ty && f->Ty->is(TypeKind::Function);
          if (forCall && !callableField && lookupMethod(recv, m->Name))
            break;
          m->FieldIndex = static_cast<int>(f->Index);
          m->Category = ValueCategory::LValue;
          return f->Ty ? f->Ty : Types.errorType();
        }
  }

  // Methods. Inside a method a `bind` supplied — including a default copied in
  // from the mark — `self.other()` means that mark's `other`, even where the
  // type declares one of its own: a default has to be able to rely on the
  // requirements it was written against. Fields still come first, above.
  if (!isSuper && isa<SelfExpr>(m->Base.get())) {
    FunctionContext *ctx = fn();
    MarkDecl *from = ctx && ctx->Fn ? ctx->Fn->FromMark : nullptr;
    if (from) {
      if (FunctionDecl *own = lookupMarkMethod(recv, from, m->Name)) {
        m->ResolvedMethod = own;
        m->QualifiedMark = from;
        ensureTemplateSignature(own);
        if (!forCall) {
          auto d = Diags.error(m->NameRange, "'{}' is a method; call it",
                               m->Name);
          d.note(fmt("write `{}()` to invoke it", m->Name).c_str()).code(343);
          return Types.errorType();
        }
        for (const Param &p : own->Params)
          if (p.IsSelf && p.SelfByRef && !recv->isPointerLike() &&
              !base->is(TypeKind::Pointer))
            m->NeedsAddressOfBase = true;
        return own->Ty ? own->Ty : Types.errorType();
      }
    }
  }

  NominalDecl *owner = nullptr;
  if (FunctionDecl *method = lookupMethod(recv, m->Name, &owner)) {
    // A requirement with no `self` — `fn new(...) -> Self` — has no receiver
    // to dispatch on, so it is not reachable through a `dyn` value.
    if (recv->is(TypeKind::DynMark)) {
      bool hasSelf = false;
      for (const Param &p : method->Params)
        hasSelf = hasSelf || p.IsSelf;
      if (!hasSelf) {
        auto d = Diags.error(m->NameRange,
                             "'{}' takes no `self`, so it cannot be called "
                             "through '{}'",
                             m->Name, recv->toString());
        d.note("a `dyn` value carries one implementation chosen at runtime; a "
               "requirement without a receiver has nothing to choose from")
            .note(fmt("call it on a concrete type instead, e.g. "
                         "`let v: T = {}::{}(...)`",
                         recv->mark() ? static_cast<Decl *>(recv->mark())->Name
                                      : "Mark",
                         m->Name)
                      .c_str())
            .code(229);
        noteDeclaredAt(d, method, "declared here", "this requirement is static");
        return Types.errorType();
      }
    }
    m->ResolvedMethod = method;
    ensureTemplateSignature(method);
    if (!forCall) {
      auto d = Diags.error(m->NameRange, "'{}' is a method; call it",
                           m->Name);
      d.note(fmt("write `{}()` to invoke it", m->Name).c_str()).code(343);
      noteDeclaredAt(d, method, "declared here", "methods are not values yet");
      return Types.errorType();
    }
    // A method with a `where` of its own exists only for the instantiations
    // that meet it; the bound that is not met is the error.
    if (!methodWhereHolds(method, m->NameRange, /*report=*/false)) {
      methodWhereHolds(method, m->NameRange, /*report=*/true);
      return Types.errorType();
    }
    // A method taking `&self` needs the receiver's address.
    for (const Param &p : method->Params)
      if (p.IsSelf && p.SelfByRef && !recv->isPointerLike() &&
          !base->is(TypeKind::Pointer))
        m->NeedsAddressOfBase = true;
    return method->Ty ? method->Ty : Types.errorType();
  }

  // `value.await` on something that is not a future. The `await` method is
  // `Future`'s alone, so this is the one way to reach here with the flag.
  if (m->IsAwait) {
    auto d = Diags.error(m->NameRange, "`.await` needs a `Future`, and this "
                                       "is a '{}'",
                         recv->toString());
    d.note("only calling an `async fn`, `task::spawn`, `task::sleep`, "
           "`task::blocking` or `task::pending` makes one");
    if (recv->is(TypeKind::Function) || recv->is(TypeKind::CFunction))
      d.note("this is a function, not the result of calling it — write "
             "`f().await`, not `f.await`");
    d.code(285);
    return Types.errorType();
  }

  // Nothing of that name on this type — but the type may be standing in for
  // another, in which case the member belongs to what it holds.
  if (Type *through = reachThroughPointee(m, expected, forCall))
    return through;

  // A field holding a function can be called too.
  auto d = Diags.error(m->NameRange, "'{}' has no member named '{}'",
                       recv->toString(), m->Name);
  d.code(344);
  {
    // The compiler provides one of that name; it just needs the sigil.
    std::vector<Type *> params;
    Type *ret = nullptr;
    if (lookupBuiltinMethod(recv, m->Name, params, ret) != BuiltinMethod::None)
      d.note(fmt("the compiler provides this one — write `.${}()`", m->Name)
                 .c_str());
  }
  if (isOptionType(recv))
    d.note(fmt("'{}' may be empty; reach the value with `?`, `.unwrap()`, "
               "`??` or a `match`",
               recv->toString())
               .c_str());
  else if (recv->isNominal())
    noteDeclaredAt(d, static_cast<Decl *>(recv->nominal()), "declared here",
                   "add the member here, or in an `extend` block");
  return Types.errorType();
}

Type *Sema::checkIndex(IndexExpr *i, Type *expected) {
  Type *base = checkExpr(i->Base.get(), nullptr);
  if (base->isError()) {
    checkExpr(i->Index.get(), nullptr);
    return base;
  }
  if (rejectOpaqueUse(base, i->BracketRange.isValid() ? i->BracketRange
                                                        : i->Range,
                      "`[]`")) {
    checkExpr(i->Index.get(), nullptr);
    return Types.errorType();
  }
  // `p[n]` on a raw pointer is the nth element from it — the same arithmetic C
  // does, with no length to check against, so it is an unsafe operation.
  if (base->is(TypeKind::Pointer) && base->isRawPointer() &&
      !isa<RangeExpr>(i->Index.get())) {
    reportUnsafe(i->Range, "indexing through a raw pointer",
                 "wrap it in `unsafe { ... }`, or mark the function @unsafe");
    Type *idx = checkExpr(i->Index.get(), Types.i64());
    if (!idx->isInt() && !idx->isError())
      Diags.error(i->Index->Range, "expected an integer index — got '{}'",
                  idx->toString())
          .code(350);
    i->Category = ValueCategory::LValue;
    i->ThroughRawPointer = true;
    return base->pointee();
  }

  Type *recv = base;
  while (recv->is(TypeKind::Pointer)) {
    if (recv->isRawPointer())
      reportUnsafe(i->Range, "indexing through a raw pointer",
                   "wrap it in `unsafe { ... }`, or mark the function @unsafe");
    recv = recv->pointee();
  }

  if (recv->is(TypeKind::Array) || recv->is(TypeKind::Slice)) {
    // `values[a..b]` takes a slice of the run rather than one element.
    if (auto *range = dyn_cast<RangeExpr>(i->Index.get())) {
      checkExpr(range, nullptr);
      // An omitted bound means the natural one: 0 on the left, the length on
      // the right, so `values[2..]`, `values[..3]` and `values[..]` all work.
      return Types.sliceOf(recv->element());
    }
    Type *idx = checkExpr(i->Index.get(), Types.i64());
    if (!idx->isInt() && !idx->isError())
      Diags.error(i->Index->Range, "expected an integer index — got '{}'",
                  idx->toString())
          .code(350);
    // An index the compiler can work out is never worth deferring to a runtime
    // check: if it is out of range it is out of range on every run.
    int64_t folded = 0;
    if (evalConstInt(i->Index.get(), folded)) {
      int64_t count =
          recv->is(TypeKind::Array) ? static_cast<int64_t>(recv->arraySize())
                                    : -1;
      if (folded < 0) {
        Diags.error(i->Index->Range, "index {} is negative", folded)
            .note("indices start at 0")
            .code(353);
      } else if (count >= 0 && folded >= count) {
        Diags.error(i->Index->Range,
                    "index {} is out of bounds for '{}'", folded,
                    recv->toString())
            .note("valid indices are 0 to {}", count - 1)
            .code(353);
      }
    }
    i->Category = ValueCategory::LValue;
    return recv->element();
  }

  if (recv->is(TypeKind::String)) {
    // `text[i]` is the i-th *character*, however wide the ones before it
    // were — the read `text.$at(i)` makes, spelled as a subscript. It is a
    // value: a String's characters are not slots, so there is nothing to
    // assign into, and `text[i] = c` is refused where assignments are.
    if (isa<RangeExpr>(i->Index.get())) {
      checkExpr(i->Index.get(), nullptr);
      Diags.error(i->Range, "a String cannot be sliced with `[a..b]`")
          .note("use `text.$substring(a, b)`, which copies the characters "
                "between the two")
          .code(351);
      return Types.errorType();
    }
    Type *idx = checkExpr(i->Index.get(), Types.i64());
    if (!idx->isInt() && !idx->isError())
      Diags.error(i->Index->Range, "expected an integer index — got '{}'",
                  idx->toString())
          .code(350);
    i->StringChar = true;
    i->Category = ValueCategory::RValue;
    return Types.charType();
  }

  std::vector<FunctionDecl *> indexers = operatorOverloads(recv, "index");
  if (indexers.size() > 1) {
    // A type may be indexed more than one way — `value["name"]` and
    // `value[0]` on the same JSON node — so what is between the brackets is
    // what says which. It is typed first, expecting nothing, and the answer
    // chooses the implementation.
    Diags.beginSpeculation();
    Type *probe = checkExpr(i->Index.get(), nullptr);
    Diags.endSpeculation();
    std::vector<Type *> args{probe};
    bool ambiguous = false;
    FunctionDecl *impl = pickOverload(indexers, args, ambiguous);
    if (!impl) {
      reportNoOverload(indexers, args, "index", i->Index->Range, ambiguous);
      return Types.errorType();
    }
    i->OverloadResolved = impl;
    Type *want = nullptr;
    for (const Param &p : impl->Params)
      if (!p.IsSelf && !want)
        want = p.Ty;
    Type *idx = checkExpr(i->Index.get(), want);
    if (want)
      requireConvertible(i->Index.get(), idx, want, "this index");
    return indexResult(i, impl);
  }

  if (FunctionDecl *impl = lookupOperator(recv, "index")) {
    i->OverloadResolved = impl;
    ensureTemplateSignature(impl);
    Type *want = nullptr;
    for (const Param &p : impl->Params)
      if (!p.IsSelf && !want)
        want = p.Ty;
    Type *idx = checkExpr(i->Index.get(), want);
    if (want)
      requireConvertible(i->Index.get(), idx, want, "this index");
    return indexResult(i, impl);
  }

  checkExpr(i->Index.get(), nullptr);
  auto d = Diags.error(i->Range, "'{}' cannot be indexed", recv->toString());
  d.code(352);
  if (recv->is(TypeKind::Tuple))
    d.note("tuple elements are reached by position, e.g. `value.0`");
  else if (recv->isNominal())
    d.note(fmt("overload it with `bind operator::index to {}`",
                  recv->toString())
               .c_str());
  return Types.errorType();
}

//===----------------------------------------------------------------------===//
// Calls
//===----------------------------------------------------------------------===//

/// A closure handed to a `@sendable` function runs on another thread, so
/// what it captured crosses with it. A closure's *type* says nothing about
/// its captures — two closures of one type may capture different things —
/// so the check is made here, on the closure as written, and only a closure
/// written at the call can be checked.
void Sema::checkSendableClosure(Expr *arg, const std::string &calleeName) {
  auto *closure = dyn_cast<ClosureExpr>(arg);
  if (!closure) {
    Diags.error(arg->Range,
                "'{}' runs its closure on another thread, so the closure "
                "has to be written here",
                calleeName)
        .note("what a closure captures is only known where it is written; "
              "write `||(...) { ... }` in the call so its captures can be "
              "checked for `Send`")
        .code(295);
    return;
  }
  for (const Capture &cap : closure->Captures) {
    if (!cap.Ty || cap.Ty->isError() || typeIsThreadSafe(cap.Ty, false))
      continue;
    auto d = Diags.error(closure->Range,
                         "this closure captures '{}', a '{}', which is not "
                         "`Send` — and it will run on another thread",
                         cap.Name, cap.Ty->toString());
    d.note("{}", whyNotThreadSafe(cap.Ty)).code(295);
    if (cap.Var)
      d.related(cap.Var->NameRange, fmt("'{}' is captured from here", cap.Name),
                "give the closure a value it may take with it instead");
  }
}

bool Sema::matchCallArguments(CallExpr *c, const std::vector<Param> &params,
                              const std::vector<Type *> &paramTypes,
                              bool variadic, const std::string &calleeName,
                              SourceRange range, const Decl *calleeDecl) {
  // Parameters excluding `self`, in declaration order.
  std::vector<const Param *> formals;
  for (const Param &p : params)
    if (!p.IsSelf)
      formals.push_back(&p);

  const size_t formalCount =
      formals.empty() ? paramTypes.size() : formals.size();

  std::vector<Expr *> slots(formalCount, nullptr);
  std::vector<Expr *> extras; // variadic tail
  c->ArgOrder.assign(formalCount, static_cast<unsigned>(-1));
  c->ParamLabels.assign(c->Args.size(), std::string());
  // A place handed to a `&T` parameter is borrowed for the call without an
  // `&` at the call site — the same courtesy a method receiver gets. The
  // argument is rewritten into the borrow it means, so nothing downstream
  // has to know.
  auto autoBorrow = [&](size_t fi, Type *got, Type *want) -> Type * {
    if (!got || !want || got->isError() || !want->is(TypeKind::Pointer) ||
        want->isRawPointer() || want->isMutablePointer() ||
        want->isWeakPointer() || got->is(TypeKind::Pointer))
      return got;
    if (!isImplicitlyConvertible(got, want->pointee()) &&
        got != want->pointee())
      return got;
    unsigned ai = fi < c->ArgOrder.size() ? c->ArgOrder[fi]
                                          : static_cast<unsigned>(-1);
    if (ai == static_cast<unsigned>(-1) || ai >= c->Args.size() ||
        c->Args[ai].Value.get() != slots[fi])
      return got;
    auto borrow = std::make_unique<BorrowExpr>();
    borrow->Range = slots[fi]->Range;
    borrow->IsMutable = false;
    borrow->Operand = std::move(c->Args[ai].Value);
    borrow->Ty = Types.pointerTo(got, false, false);
    borrow->Category = ValueCategory::RValue;
    c->Args[ai].Value = std::move(borrow);
    c->Args[ai].AutoBorrow = true;
    slots[fi] = c->Args[ai].Value.get();
    return slots[fi]->Ty;
  };

  size_t positional = 0;
  bool sawLabel = false;
  bool ok = true;

  for (size_t ai = 0; ai < c->Args.size(); ++ai) {
    Argument &a = c->Args[ai];
    if (!a.Label.empty()) {
      sawLabel = true;
      size_t found = formalCount;
      for (size_t fi = 0; fi < formals.size(); ++fi)
        if (formals[fi]->Name == a.Label)
          found = fi;
      if (found == formalCount) {
        auto d = Diags.error(a.LabelRange.isValid() ? a.LabelRange
                                                    : a.Value->Range,
                             "'{}' has no parameter named '{}'", calleeName,
                             a.Label);
        d.code(360);
        if (!formals.empty()) {
          std::string names;
          for (size_t fi = 0; fi < formals.size(); ++fi) {
            if (fi) names += ", ";
            names += formals[fi]->Name;
          }
          d.note(fmt("its parameters are: {}", names).c_str());
        }
        if (calleeDecl)
          noteDeclaredAt(d, calleeDecl, fmt("'{}' declared here", calleeName),
                         "labels must match the parameter names exactly");
        ok = false;
        continue;
      }
      if (slots[found]) {
        Diags.error(a.Value->Range, "argument '{}' is given twice", a.Label)
            .code(361);
        ok = false;
        continue;
      }
      slots[found] = a.Value.get();
      c->ArgOrder[found] = static_cast<unsigned>(ai);
      continue;
    }

    if (sawLabel && positional < formalCount && slots[positional]) {
      // Positional after labelled: find the next free slot.
      while (positional < formalCount && slots[positional])
        ++positional;
    }
    if (positional < formalCount) {
      slots[positional] = a.Value.get();
      c->ArgOrder[positional] = static_cast<unsigned>(ai);
      if (positional < formals.size())
        c->ParamLabels[ai] = formals[positional]->Name;
      ++positional;
    } else if (variadic) {
      extras.push_back(a.Value.get());
    } else {
      auto d = Diags.error(a.Value->Range,
                           "'{}' takes {} argument(s) — {} given", calleeName,
                           formalCount, c->Args.size());
      d.code(362);
      if (calleeDecl)
        noteDeclaredAt(d, calleeDecl, fmt("'{}' declared here", calleeName),
                       "the extra arguments have nowhere to go");
      ok = false;
      break;
    }
  }

  // Fill defaults and report anything still missing.
  for (size_t fi = 0; fi < formalCount; ++fi) {
    Type *want = fi < paramTypes.size() ? paramTypes[fi] : Types.errorType();
    if (slots[fi]) {
      Type *got = checkExpr(slots[fi], want);
      got = autoBorrow(fi, got, want);
      // The parameter says what it wants, so a type that knows how to become
      // it may do so here without the call site spelling `into`.
      if (!isImplicitlyConvertible(got, want)) {
        unsigned ai = fi < c->ArgOrder.size() ? c->ArgOrder[fi]
                                              : static_cast<unsigned>(-1);
        if (ai != static_cast<unsigned>(-1) && ai < c->Args.size() &&
            c->Args[ai].Value.get() == slots[fi] &&
            insertImplicitConversion(c->Args[ai].Value, got, want)) {
          slots[fi] = c->Args[ai].Value.get();
          got = slots[fi]->Ty;
        }
      }
      // C++ wrote `T&` or `Base*`; Rune holds a `*var T` or a `*var Derived`.
      // Both are the one address C++ expects, so the pointer goes as it is.
      const auto *calleeFn =
          calleeDecl ? dyn_cast<FunctionDecl>(calleeDecl) : nullptr;
      if (isCxxExtern(calleeFn) && cxxPointerConvertible(got, want))
        continue;
      requireConvertible(slots[fi], got, want,
                         fmt("argument '{}' of '{}'",
                                fi < formals.size() ? formals[fi]->Name
                                                    : std::to_string(fi),
                                calleeName)
                             .c_str());
      if (calleeDecl && calleeDecl->hasAttr("sendable") && want &&
          want->is(TypeKind::Function))
        checkSendableClosure(slots[fi], calleeName);
      continue;
    }
    if (fi < formals.size() && formals[fi]->DefaultValue) {
      // The default is checked once at the declaration; nothing to do here.
      continue;
    }
    auto d = Diags.error(range, "missing argument '{}' for '{}'",
                         fi < formals.size() ? formals[fi]->Name
                                             : std::to_string(fi),
                         calleeName);
    d.note(fmt("it has type '{}'", want->toString()).c_str()).code(363);
    if (calleeDecl)
      noteDeclaredAt(d, calleeDecl, fmt("'{}' declared here", calleeName),
                     "supply this argument, or give the parameter a default");
    ok = false;
  }

  // Variadic tail: no declared type, so just check the expressions.
  for (Expr *e : extras)
    checkExpr(e, nullptr);

  return ok;
}

namespace {
/// The expected type for an argument whose formal is written in terms of the
/// parameters being inferred: what is known of it so far, or nothing.
///
/// Inference reads left to right, so by the time a later argument is checked
/// an earlier one may already have said what a parameter is —
/// `mem::replace(&var self.head, Link::Empty)` cannot type its second
/// argument until the first has pinned `T` down to `Link<Item>`. Handing that
/// type down is what lets a bare variant, an empty collection or an untyped
/// literal be written there.
Type *expectedForArgument(TypeContext &types, Type *formal,
                          const std::map<std::string, Type *> &bindings) {
  if (!formal)
    return nullptr;
  Type *want = types.substitute(formal, bindings);
  if (!want)
    return nullptr;
  if (!want->containsGenericParam())
    return want;
  // A function parameter is worth handing down even when its *result* is
  // still being worked out: `map`'s `@function(Item) -> B` says what the
  // closure is handed, which is what lets it be written `||(x) { ... }`.
  // Whoever reads this has to leave the result alone — `checkClosure` does.
  if (want->is(TypeKind::Function) || want->is(TypeKind::CFunction)) {
    bool argumentsKnown = !want->params().empty();
    for (Type *p : want->params())
      if (!p || p->containsGenericParam())
        argumentsKnown = false;
    if (argumentsKnown)
      return want;
  }
  return nullptr;
}
} // namespace

/// True when a set of type-parameter bindings makes a call work: every
/// parameter is answered, every argument converts to what its formal becomes,
/// and the result converts to where it is going. This is what decides whether
/// the destination's answer may stand or the arguments' has to.
bool Sema::fitsCall(FunctionDecl *f, const std::vector<Type *> &formals,
                    const std::vector<Type *> &argTypes,
                    const std::map<std::string, Type *> &bindings,
                    Type *expected) {
  if (!f || !f->Ty)
    return false;
  for (const auto &g : f->Generics)
    if (!bindings.count(g.Name))
      return false;
  for (size_t i = 0; i < formals.size() && i < argTypes.size(); ++i) {
    if (!argTypes[i] || argTypes[i]->isError())
      continue;
    Type *want = Types.substitute(formals[i], bindings);
    if (want && !want->isError() && !want->containsGenericParam() &&
        isImplicitlyConvertible(argTypes[i], want))
      continue;
    // Substituting into a *named* type rebuilds it, and the rebuilt one is
    // not the same object as the instantiation the program will use, so a
    // pointer comparison says no where the shapes agree. Structural
    // agreement is the honest question, and unification is how it is asked.
    std::map<std::string, Type *> probe = bindings;
    if (Types.unify(formals[i], argTypes[i], probe))
      continue;
    return false;
  }
  if (expected && !expected->isError() && !expected->containsGenericParam()) {
    // The same question about the result: does what the function says it
    // hands back agree with where it is going, under these bindings?
    std::map<std::string, Type *> probe = bindings;
    if (!Types.unify(f->Ty->result(), expected, probe))
      return false;
  }
  return true;
}

Type *Sema::checkCall(CallExpr *c, Type *expected) {
  // A builder block — `Body { Text("hi"); Button {} }` — stands for an
  // `empty()` and one `add` per item. When the type is not a builder there is
  // no `empty` to find, and the message should say what was actually written
  // rather than name a method nobody meant to call.
  if (c->BuilderSeed) {
    if (auto *ref = dyn_cast<DeclRefExpr>(c->Callee.get())) {
      if (!lookupPath(ref->Path, ref->Range, /*quiet=*/true)) {
        std::string name = ref->Path.size() > 1
                               ? ref->Path[ref->Path.size() - 2]
                               : std::string("this");
        auto d = Diags.error(c->Range,
                             "'{}' is not a builder, so it cannot be written "
                             "as a block of values",
                             name);
        d.note("a block of values stands for `empty()` and one `add` per "
               "item, which is what `std::builder::Builder` asks for");
        d.note(fmt("bind it: `bind builder::Builder to {} {{ type Child = "
                   "...; fn empty() -> Self {{ ... }} fn add(&var self, "
                   "child: ...) {{ ... }} }}`",
                   name)
                   .c_str());
        d.note("or, for a struct literal, give each field a name: "
               "`Name { field: value }`");
        d.code(367);
        return Types.errorType();
      }
    }
  }

  // --- Method call -------------------------------------------------------
  if (auto *member = dyn_cast<MemberExpr>(c->Callee.get())) {
    Type *ft = checkMember(member, nullptr, /*forCall=*/true);
    c->IsMethodCall = true;
    if (ft->isError())
      return ft;

    if (member->Builtin != BuiltinMethod::None) {
      c->Builtin = member->Builtin;
      // A copy the compiler makes itself is only sound when nothing in the
      // value owns something a second owner would destroy again.
      if (c->Builtin == BuiltinMethod::Clone)
        checkClonable(ft->result(),
                      member->NameRange.isValid() ? member->NameRange
                                                  : c->Range);
      std::vector<Param> none;
      matchCallArguments(c, none, ft->params(), false, member->Name, c->Range,
                         nullptr);
      member->Ty = ft;
      return ft->result();
    }

    auto *method = dyn_cast<FunctionDecl>(member->ResolvedMethod);
    // A `bind` may supply one name several times over, told apart by what
    // each version takes. Which one this call means is a question about the
    // arguments, so they are typed once with nothing expected of them and
    // the answer picks the body.
    if (method && method->Bind) {
      Type *recv = member->Base ? member->Base->Ty : nullptr;
      // `value::Mark.name(...)` chooses among what that mark supplies; a
      // plain `value.name(...)` among everything bound to the type.
      std::vector<FunctionDecl *> cands =
          member->QualifiedMark
              ? markImplSet(recv, member->QualifiedMark, member->Name)
              : lookupOverloads(recv, member->Name);
      if (cands.size() > 1) {
        std::vector<Type *> args;
        std::vector<std::string> labels;
        Diags.beginSpeculation();
        for (auto &arg : c->Args) {
          args.push_back(checkExpr(arg.Value.get(), nullptr));
          labels.push_back(arg.Label);
        }
        Diags.endSpeculation();
        bool ambiguous = false;
        FunctionDecl *chosen = pickOverload(cands, args, ambiguous, &labels);
        if (!chosen) {
          reportNoOverload(cands, args, member->Name, c->Range, ambiguous);
          return Types.errorType();
        }
        if (chosen != method) {
          method = chosen;
          member->ResolvedMethod = chosen;
          ensureTemplateSignature(chosen);
          member->NeedsAddressOfBase = false;
          Type *base = member->Base ? member->Base->Ty : nullptr;
          for (const Param &p : chosen->Params)
            if (p.IsSelf && p.SelfByRef && recv && !recv->isPointerLike() &&
                base && !base->is(TypeKind::Pointer))
              member->NeedsAddressOfBase = true;
        }
      }
    }
    if (!method) {
      // Calling a field that holds a function value.
      if (!ft->is(TypeKind::Function) && !ft->is(TypeKind::CFunction)) {
        Diags.error(c->Range, "'{}' is not callable", ft->toString()).code(364);
        return Types.errorType();
      }
      // The callee is the field, not a method, so the call is indirect. Its
      // type has to be recorded: it is what tells CodeGen which shape of call
      // to emit.
      member->Ty = ft;
      c->IsMethodCall = false;
      std::vector<Param> none;
      matchCallArguments(c, none, ft->params(), ft->isVariadicFunction(),
                         member->Name, c->Range, nullptr);
      return ft->result();
    }

    if (!method->Generics.empty()) {
      // `value.method::<T>()` says which; otherwise infer from the arguments.
      ensureTemplateSignature(method);
      std::vector<Type *> targs;
      if (!member->GenericArgs.empty()) {
        if (member->GenericArgs.size() != method->Generics.size()) {
          Diags.error(c->Range,
                      "'{}' takes {} type argument(s) — {} given", member->Name,
                      method->Generics.size(), member->GenericArgs.size())
              .code(268);
          return Types.errorType();
        }
        for (const auto &ga : member->GenericArgs)
          targs.push_back(resolveTypeOrError(ga.get(), Types.errorType()));
      } else {
        // Same as a free call: where the result is going seeds the
        // bindings, the arguments are checked against that, and the
        // arguments win if the two cannot be reconciled.
        std::map<std::string, Type *> bindings;
        std::vector<Type *> formalTypes = method->Ty->params();
        if (expected && !expected->isError() &&
            !expected->containsGenericParam())
          Types.unify(method->Ty->result(), expected, bindings);
        std::vector<Type *> argTypes(formalTypes.size(), nullptr);
        for (size_t i = 0; i < c->Args.size() && i < formalTypes.size(); ++i) {
          Type *at = checkExpr(
              c->Args[i].Value.get(),
              expectedForArgument(Types, formalTypes[i], bindings));
          argTypes[i] = at;
          Types.unify(formalTypes[i], at, bindings);
        }
        if (!fitsCall(method, formalTypes, argTypes, bindings, expected)) {
          std::map<std::string, Type *> fromArgs;
          for (size_t i = 0; i < formalTypes.size() && i < argTypes.size(); ++i)
            if (argTypes[i])
              Types.unify(formalTypes[i], argTypes[i], fromArgs);
          if (expected && !expected->isError() && method->Ty)
            Types.unify(method->Ty->result(), expected, fromArgs);
          bindings = std::move(fromArgs);
        }
        for (const auto &g : method->Generics) {
          auto it = bindings.find(g.Name);
          if (it == bindings.end()) {
            // Nothing in the arguments mentions this parameter, and nothing
            // about where the result goes settles it either. The call site
            // has to say.
            Diags.error(c->Range,
                        "cannot infer type argument '{}' for '{}'", g.Name,
                        member->Name)
                .note(fmt("write it out: `{}::<{}>(...)`", member->Name,
                          g.Name)
                          .c_str())
                .code(269);
            return Types.errorType();
          }
          targs.push_back(it->second);
        }
      }
      for (Type *t : targs)
        if (t->isError())
          return Types.errorType();
      FunctionDecl *inst = instantiate(method, targs, c->Range);
      if (!inst)
        return Types.errorType();
      member->ResolvedMethod = inst;
      c->Target = inst;
      matchCallArguments(c, inst->Params, inst->Ty->params(), inst->IsVariadic,
                         member->Name, c->Range, inst);
      return inst->Ty->result();
    }

    c->Target = method;
    if (method->IsExtern || method->IsUnsafe)
      reportUnsafe(c->Range,
                   fmt("call to {} '{}'",
                          method->IsExtern ? "foreign function" : "unsafe "
                                                                  "function",
                          method->Name),
                   "wrap it in `unsafe { ... }`, mark the caller @unsafe, or "
                   "justify it with @safe(\"reason\")");
    matchCallArguments(c, method->Params, method->Ty->params(),
                       method->IsVariadic, method->Name, c->Range, method);
    return method->Ty->result();
  }

  // --- Named callee ------------------------------------------------------
  if (auto *ref = dyn_cast<DeclRefExpr>(c->Callee.get())) {
    // `Queue<String>::new(...)` names a method of an instantiation, which
    // has to exist before it can be looked up.
    Symbol *sym = resolveInferredPath(ref, expected);
    if (!sym && ref->FromInferredType)
      return reportInferredPathFailure(ref, expected);
    if (!sym)
      sym = lookupStaticOnInstantiation(ref);
    if (!sym)
      sym = lookupPath(ref->Path, ref->Range, /*quiet=*/true);
    if (!sym && ref->Path.size() == 1)
      sym = variantOfExpected(ref->Path[0], expected);
    if (!sym)
      sym = lookupPath(ref->Path, ref->Range, /*quiet=*/false);
    if (!sym)
      return Types.errorType();

    // Class construction: `Dog("rex")`
    if (sym->Kind == SymbolKind::TypeName && sym->D) {
      // Through an alias: `type Table<T> = Vector<T>` then `Table<i64>()`
      // builds the `Vector<i64>` the alias stands for. The alias is resolved
      // the way it would be in a type position — arguments and all — and the
      // construction goes on with what it named.
      if (isa<TypeAliasDecl>(sym->D)) {
        NamedTypeRepr named;
        named.Range = ref->Range;
        named.NameRange = ref->Range;
        named.Path = ref->Path;
        for (const auto &ga : ref->GenericArgs)
          named.GenericArgs.push_back(cloneTypeRepr(ga.get()));
        Type *behind = resolveTypeOrError(&named, Types.errorType());
        named.GenericArgs.clear();   // the clones are owned by this scope
        if (behind->isError())
          return behind;
        if (!behind->isNominal()) {
          auto d = Diags.error(c->Range, "'{}' is not something to construct",
                               sym->Name);
          d.note("it stands for '{}', which has no initialiser",
                 behind->toString())
              .code(314);
          return Types.errorType();
        }
        NominalDecl *nd = behind->nominal();
        Symbol through;
        through.Kind = SymbolKind::TypeName;
        through.Name = static_cast<Decl *>(nd)->Name;
        through.D = static_cast<Decl *>(nd);
        through.IsPublic = static_cast<Decl *>(nd)->IsPublic;
        SyntheticSymbols.push_back(through);
        sym = &SyntheticSymbols.back();
        // An instantiation is what the alias named; its arguments are
        // already fixed, so nothing here should look for more.
        ref->GenericArgs.clear();
      }
      if (auto *cls = dyn_cast<ClassDecl>(sym->D)) {
        // `Handle<i64>(...)` constructs an instantiation, not the template.
        // The arguments may be written out, read off the expected type, or
        // inferred from what the initialiser is being given.
        if (!cls->Generics.empty()) {
          if (cls->GenericTemplate)
            cls = cast<ClassDecl>(static_cast<Decl *>(cls->GenericTemplate));
          std::vector<Type *> targs;
          for (const auto &ga : ref->GenericArgs)
            targs.push_back(resolveTypeOrError(ga.get(), Types.errorType()));
          if (targs.empty() && expected && expected->isNominal() &&
              expected->nominal()->GenericTemplate == cls)
            targs = expected->nominal()->TypeArguments;
          if (targs.empty()) {
            // Infer from the initialiser's parameters.
            FunctionDecl *tmplInit = nullptr;
            for (auto &m : cls->Methods)
              if (m->Flavour == FunctionFlavour::Initialiser)
                tmplInit = m.get();
            if (tmplInit) {
              auto savedGenerics = ActiveGenericParams;
              Type *savedSelf = ActiveSelfType;
              bindGenerics(cls->Generics, Types, ActiveGenericParams);
              ActiveSelfType = nullptr;
              std::map<std::string, Type *> bindings;
              size_t argIndex = 0;
              for (const Param &p : tmplInit->Params) {
                if (p.IsSelf)
                  continue;
                if (argIndex >= c->Args.size())
                  break;
                Type *formal = p.TypeAnnotation
                                   ? resolveType(p.TypeAnnotation.get())
                                   : nullptr;
                Type *actual = checkExpr(
                    c->Args[argIndex].Value.get(),
                    expectedForArgument(Types, formal, bindings));
                if (formal && actual)
                  Types.unify(formal, actual, bindings);
                ++argIndex;
              }
              ActiveGenericParams = savedGenerics;
              ActiveSelfType = savedSelf;
              for (const auto &g : cls->Generics) {
                auto it = bindings.find(g.Name);
                if (it == bindings.end()) {
                  targs.clear();
                  break;
                }
                targs.push_back(it->second);
              }
            }
          }
          if (targs.size() != cls->Generics.size()) {
            auto d = Diags.error(c->Range,
                                 "'{}' needs its type argument(s)", cls->Name);
            d.note(fmt("write them out, e.g. `{}<...>(...)`, or annotate "
                          "the destination", cls->Name)
                       .c_str())
                .code(371);
            noteDeclaredAt(d, cls, "declared here",
                           "the parameters cannot be inferred here");
            return Types.errorType();
          }
          NominalDecl *inst = instantiateNominal(cls, targs, c->Range);
          if (!inst)
            return Types.errorType();
          cls = cast<ClassDecl>(static_cast<Decl *>(inst));
        }
        ref->Resolved = cls;
        c->ConstructsClass = cls;
        checkAvailableUnderZombie(cls, c->Callee->Range);
        FunctionDecl *init = nullptr;
        for (ClassDecl *k = cls; k && !init; k = k->Super)
          init = k->Init;
        if (init) {
          ensureTemplateSignature(init);
          matchCallArguments(c, init->Params, init->Ty->params(), false,
                             cls->Name, c->Range, init);
          c->Target = init;
        } else if (!c->Args.empty()) {
          auto d = Diags.error(c->Range, "'{}' has no `init`, so it takes no "
                                         "arguments",
                               cls->Name);
          d.note("add `fn init(self, ...)` to give the class a constructor")
              .code(365);
          noteDeclaredAt(d, cls, "declared here", "no initialiser is defined");
        }
        return cls->DeclaredType ? cls->DeclaredType : Types.errorType();
      }
      if (auto *st = dyn_cast<StructDecl>(sym->D)) {
        auto d = Diags.error(c->Range, "'{}' is a struct; build it with braces",
                             st->Name);
        std::string fields;
        for (size_t i = 0; i < st->Fields.size(); ++i) {
          if (i) fields += ", ";
          fields += st->Fields[i]->Name + ": ...";
        }
        d.note(fmt("write `{} {{ {} }}`", st->Name, fields).c_str())
            .code(366);
        return Types.errorType();
      }
    }

    // Enum variant with a payload: `Shape::Circle(2.0)`
    if (sym->Kind == SymbolKind::Variant) {
      auto *e = sym->Owner;
      const int variantIndex = sym->VariantIndex;
      if (!e->Generics.empty()) {
        // Type the arguments first: they are usually what determines the
        // instantiation, with the contextual type as a fallback.
        std::vector<Type *> argTypes;
        for (auto &a : c->Args)
          argTypes.push_back(checkExpr(a.Value.get(), nullptr));
        e = instantiateVariantOwner(e, static_cast<unsigned>(variantIndex),
                                    ref->GenericArgs, argTypes, expected,
                                    c->Range);
        if (!e)
          return Types.errorType();
      }
      auto *variant = e->Variants[static_cast<size_t>(variantIndex)].get();
      ref->Resolved = e;
      ref->VariantIndex = variantIndex;
      c->ConstructsEnum = e;
      c->ConstructsVariant = variantIndex;
      if (variant->Shape != VariantShape::Tuple) {
        auto d = Diags.error(c->Range, "variant '{}' is not called with "
                                       "parentheses",
                             variant->Name);
        d.note(variant->Shape == VariantShape::Unit
                   ? "write it as a plain name"
                   : "write it with field syntax, e.g. `Name { field: value }`")
            .code(367);
        noteDeclaredAt(d, variant, "declared here", "this is its shape");
        return e->DeclaredType ? e->DeclaredType : Types.errorType();
      }
      if (variant->TupleTypes.size() != c->Args.size()) {
        auto d = Diags.error(c->Range, "variant '{}' takes {} value(s) — {} "
                                       "given",
                             variant->Name, variant->TupleTypes.size(),
                             c->Args.size());
        d.code(368);
        noteDeclaredAt(d, variant, "declared here", "the arity must match");
      }
      for (size_t i = 0; i < c->Args.size(); ++i) {
        Type *want = i < variant->TupleTypes.size()
                         ? variant->TupleTypes[i]->Resolved
                         : nullptr;
        Type *got = checkExpr(c->Args[i].Value.get(), want);
        if (want)
          requireConvertible(c->Args[i].Value.get(), got, want,
                             fmt("field {} of '{}'", i, variant->Name).c_str());
      }
      return e->DeclaredType ? e->DeclaredType : Types.errorType();
    }

    // `Mark::method(...)` — a requirement reached with no receiver to dispatch
    // on, so the implementing type has to come from the context the result
    // flows into. This is what makes `fn new(...) -> Self` usable as a
    // constructor: `var s: Sheep = Animal::new("Dolly")`.
    if (sym->Kind == SymbolKind::Function && ref->Path.size() >= 2) {
      auto *req = cast<FunctionDecl>(sym->D);
      MarkDecl *mark = req->Parent ? dyn_cast<MarkDecl>(req->Parent) : nullptr;
      if (mark) {
        Type *target = expected;
        while (target && target->is(TypeKind::Pointer))
          target = target->pointee();
        if (!target || target->isError()) {
          auto d = Diags.error(c->Range,
                               "cannot tell which type's '{}' to call",
                               req->Name);
          d.note(fmt("'{}' is a mark, so the implementation is chosen by the "
                        "type this produces", mark->Name)
                     .c_str())
              .note(fmt("annotate the destination, e.g. `var x: T = {}::{}"
                        "(...)`", mark->Name, req->Name)
                        .c_str())
              .code(225);
          return Types.errorType();
        }
        FunctionDecl *impl = lookupMarkMethod(target, mark, req->Name);
        if (!impl) {
          auto d = Diags.error(c->Range, "'{}' is not bound to mark '{}'",
                               target->toString(), mark->Name);
          d.note(fmt("write `bind {} to {}` to give it one", mark->Name,
                        target->toString())
                     .c_str())
              .code(226);
          noteDeclaredAt(d, mark, "the mark declared here",
                         "this is the requirement being called");
          return Types.errorType();
        }
        Symbol &implSym = SyntheticSymbols.emplace_back();
        implSym.Kind = SymbolKind::Function;
        implSym.Name = impl->Name;
        implSym.D = impl;
        implSym.IsPublic = impl->IsPublic;
        sym = &implSym;
      }
    }

    if (sym->Kind == SymbolKind::Function) {
      auto *f = cast<FunctionDecl>(sym->D);
      ref->Resolved = f;

      if (!f->Generics.empty()) {
        ensureTemplateSignature(f);
        std::vector<Type *> targs;
        if (!ref->GenericArgs.empty()) {
          for (const auto &ga : ref->GenericArgs)
            targs.push_back(resolveTypeOrError(ga.get(), Types.errorType()));
          if (targs.size() != f->Generics.size()) {
            auto d = Diags.error(ref->Range,
                                 "'{}' expects {} type argument(s) — {} given",
                                 f->Name, f->Generics.size(), targs.size());
            d.code(369);
            noteDeclaredAt(d, f, "declared here", "the counts must match");
            return Types.errorType();
          }
        } else {
          // Where the result is going is part of the answer, and it comes
          // first. `boxed(Dog {})` going into a `Box<dyn Speaker>` is meant
          // to box a `dyn Speaker`; read off the argument alone it would box
          // a `Dog`, and the annotation would then be wrong about its own
          // value. So the destination seeds the bindings, the arguments are
          // checked against what that says, and if the two cannot be
          // reconciled the arguments have the last word — so nothing that
          // used to infer stops doing so.
          std::map<std::string, Type *> bindings;
          const std::vector<Type *> &formals = f->Ty->params();
          if (expected && !expected->isError() &&
              !expected->containsGenericParam())
            Types.unify(f->Ty->result(), expected, bindings);
          std::vector<Type *> argTypes(formals.size(), nullptr);
          size_t positional = 0;
          for (Argument &a : c->Args) {
            size_t idx = positional;
            if (!a.Label.empty()) {
              size_t fi = 0, found = formals.size();
              for (const Param &p : f->Params) {
                if (p.IsSelf) continue;
                if (p.Name == a.Label) found = fi;
                ++fi;
              }
              idx = found;
            } else {
              ++positional;
            }
            if (idx >= formals.size())
              continue;
            Type *at = checkExpr(
                a.Value.get(),
                expectedForArgument(Types, formals[idx], bindings));
            argTypes[idx] = at;
            Types.unify(formals[idx], at, bindings);
          }
          if (!fitsCall(f, formals, argTypes, bindings, expected)) {
            std::map<std::string, Type *> fromArgs;
            for (size_t i = 0; i < formals.size() && i < argTypes.size(); ++i)
              if (argTypes[i])
                Types.unify(formals[i], argTypes[i], fromArgs);
            if (expected && !expected->isError() && f->Ty)
              Types.unify(f->Ty->result(), expected, fromArgs);
            bindings = std::move(fromArgs);
          }
          bool complete = true;
          for (const auto &g : f->Generics) {
            auto it = bindings.find(g.Name);
            if (it == bindings.end()) {
              complete = false;
              auto d = Diags.error(c->Range,
                                   "cannot infer type parameter '{}' of '{}'",
                                   g.Name, f->Name);
              d.note(fmt("give it explicitly, e.g. `{}::<...>(...)`", f->Name)
                         .c_str())
                  .code(370);
              d.related(g.Range, fmt("'{}' is declared here", g.Name),
                        "no argument mentions this parameter");
              targs.push_back(Types.errorType());
            } else {
              targs.push_back(it->second);
            }
          }
          if (!complete)
            return Types.errorType();
        }
        FunctionDecl *inst = instantiate(f, targs, c->Range);
        if (!inst)
          return Types.errorType();
        ref->Resolved = inst;
        c->Target = inst;
        matchCallArguments(c, inst->Params, inst->Ty->params(), inst->IsVariadic,
                           f->Name, c->Range, f);
        if (inst->hasAttr("intrinsic"))
          checkReflectionCall(c, inst);
        return inst->Ty->result();
      }

      ensureTemplateSignature(f);
      c->Target = f;
      if (f->IsExtern || f->IsUnsafe)
        reportUnsafe(c->Range,
                     fmt("call to {} '{}'",
                            f->IsExtern ? "foreign function" : "unsafe function",
                            f->Name),
                     "wrap it in `unsafe { ... }`, mark the caller @unsafe, or "
                     "justify it with @safe(\"reason\")");
      matchCallArguments(c, f->Params, f->Ty->params(), f->IsVariadic, f->Name,
                         c->Range, f);
      if (f->hasAttr("intrinsic"))
        checkReflectionCall(c, f);
      return f->Ty->result();
    }
  }

  // --- Indirect call through a value ------------------------------------
  Type *ft = checkExpr(c->Callee.get(), nullptr);
  if (ft->isError())
    return ft;
  // A function value reached through a borrow — `handlers[0]()`, where the
  // container lends its element — is called through it: `(*f)()`.
  while (ft->is(TypeKind::Pointer) && !ft->isRawPointer() &&
         !ft->isWeakPointer() && ft->pointee() &&
         (ft->pointee()->is(TypeKind::Function) ||
          ft->pointee()->is(TypeKind::Pointer))) {
    auto d = std::make_unique<DerefExpr>();
    d->Range = c->Callee->Range;
    d->Operand = std::move(c->Callee);
    c->Callee = std::move(d);
    ft = checkExpr(c->Callee.get(), nullptr);
    if (ft->isError())
      return ft;
  }
  if (ft->is(TypeKind::CFunction)) {
    // A bare pointer: the compiler cannot know it points at anything, so
    // calling one is an unsafe operation.
    reportUnsafe(c->Range, "call through a function pointer",
                 "wrap it in `unsafe { ... }`, mark the caller @unsafe, or "
                 "justify it with @safe(\"reason\")");
    std::vector<Param> noParams;
    matchCallArguments(c, noParams, ft->params(), ft->isVariadicFunction(),
                       "this function pointer", c->Range, nullptr);
    return ft->result();
  }
  if (!ft->is(TypeKind::Function)) {
    auto d = Diags.error(c->Callee->Range, "'{}' is not callable",
                         ft->toString());
    d.note("only functions, closures and class constructors can be called")
        .code(364);
    if (ft->isNominal())
      d.note(fmt("give it a call operator with `bind operator::call to {}`",
                    ft->toString())
                 .c_str());
    for (Argument &a : c->Args)
      checkExpr(a.Value.get(), nullptr);
    return Types.errorType();
  }
  std::vector<Param> none;
  matchCallArguments(c, none, ft->params(), ft->isVariadicFunction(),
                     "this function value", c->Range, nullptr);
  return ft->result();
}

//===----------------------------------------------------------------------===//
// Aggregates
//===----------------------------------------------------------------------===//

Type *Sema::checkStructLit(StructLitExpr *s, Type *expected) {
  Symbol *sym = lookupPath(s->Path, s->PathRange.isValid() ? s->PathRange
                                                           : s->Range,
                           /*quiet=*/false);
  if (!sym)
    return Types.errorType();

  std::vector<FieldDecl *> fields;
  Type *resultType = Types.errorType();

  if (sym->Kind == SymbolKind::Variant) {
    auto *e = sym->Owner;
    auto *variant = e->Variants[static_cast<size_t>(sym->VariantIndex)].get();
    s->ResolvedDecl = e;
    s->VariantIndex = sym->VariantIndex;
    if (variant->Shape != VariantShape::Struct) {
      auto d = Diags.error(s->Range, "variant '{}' has no named fields",
                           variant->Name);
      d.code(371);
      noteDeclaredAt(d, variant, "declared here", "this is its shape");
      return e->DeclaredType ? e->DeclaredType : Types.errorType();
    }
    for (auto &f : variant->Fields)
      fields.push_back(f.get());
    resultType = e->DeclaredType;
  } else if (auto *aliasDecl = sym->D ? dyn_cast<TypeAliasDecl>(sym->D)
                                      : nullptr) {
    // Through an alias: `type Spot<T> = Point<T>` then `Spot<i64> { ... }`
    // builds the `Point<i64>` it stands for. Resolved exactly as it would be
    // in a type position, arguments and all.
    NamedTypeRepr named;
    named.Range = s->PathRange.isValid() ? s->PathRange : s->Range;
    named.NameRange = named.Range;
    named.Path = s->Path;
    for (const auto &ga : s->GenericArgs)
      named.GenericArgs.push_back(cloneTypeRepr(ga.get()));
    Type *behind = resolveTypeOrError(&named, Types.errorType());
    named.GenericArgs.clear();
    if (behind->isError())
      return behind;
    NominalDecl *nd = behind->isNominal() ? behind->nominal() : nullptr;
    if (!nd || !isa<StructDecl>(static_cast<Decl *>(nd))) {
      auto d = Diags.error(s->Range, "'{}' cannot be built with braces",
                           sym->Name);
      d.note("it stands for '{}'", behind->toString())
          .note("only structs and struct-shaped enum variants use this form")
          .code(373);
      noteDeclaredAt(d, aliasDecl, "declared here", "this is what it names");
      return Types.errorType();
    }
    s->ResolvedDecl = static_cast<Decl *>(nd);
    for (auto &f : nd->Fields)
      fields.push_back(f.get());
    resultType = nd->DeclaredType;
  } else if (auto *nd = sym->D ? dyn_cast<NominalDecl>(sym->D) : nullptr) {
    // As in a type position: explicit arguments mean the template, even when
    // the bare name currently refers to an instantiation of it.
    if (nd->GenericTemplate && !s->GenericArgs.empty())
      nd = nd->GenericTemplate;
    if (isa<ClassDecl>(static_cast<Decl *>(nd))) {
      auto d = Diags.error(s->Range,
                           "'{}' is a class; construct it by calling it",
                           static_cast<Decl *>(nd)->Name);
      d.note(fmt("write `{}(...)` so its `init` runs", sym->Name).c_str())
          .code(372);
      return Types.errorType();
    }
    if (nd->Cxx && nd->Cxx->IsClass) {
      auto d = Diags.error(s->Range,
                           "'{}' is a C++ class; Rune cannot build one",
                           static_cast<Decl *>(nd)->Name);
      d.note(fmt("allocate it with `cxx::alloc<{}>()` and construct it with "
                 "`init`, or take one from a function that makes it",
                 sym->Name)
                 .c_str())
          .code(525);
      return Types.errorType();
    }
    // Resolve generic arguments, inferring them from the field values when
    // they are not written out.
    if (!nd->Generics.empty()) {
      std::vector<Type *> targs;
      if (!s->GenericArgs.empty()) {
        for (const auto &ga : s->GenericArgs)
          targs.push_back(resolveTypeOrError(ga.get(), Types.errorType()));
      } else {
        std::map<std::string, Type *> bindings;
        auto saved = ActiveGenericParams;
        for (size_t i = 0; i < nd->Generics.size(); ++i)
          ActiveGenericParams[nd->Generics[i].Name] =
              Types.genericParam(nd->Generics[i].Name, static_cast<unsigned>(i));
        for (auto &lf : s->Fields) {
          for (auto &f : nd->Fields) {
            if (f->Name != lf.Name || !lf.Value)
              continue;
            Type *want = resolveTypeOrError(f->TypeAnnotation.get(), nullptr);
            Type *got = checkExpr(lf.Value.get(), nullptr);
            if (want)
              Types.unify(want, got, bindings);
          }
        }
        ActiveGenericParams = saved;
        for (const auto &g : nd->Generics) {
          auto it = bindings.find(g.Name);
          if (it == bindings.end()) {
            auto d = Diags.error(s->Range,
                                 "cannot infer type parameter '{}' of '{}'",
                                 g.Name, static_cast<Decl *>(nd)->Name);
            d.note(fmt("write it out, e.g. `{}<...> {{ ... }}`",
                          static_cast<Decl *>(nd)->Name)
                       .c_str())
                .code(370);
            return Types.errorType();
          }
          targs.push_back(it->second);
        }
      }
      NominalDecl *inst = instantiateNominal(nd, targs, s->Range);
      if (!inst)
        return Types.errorType();
      nd = inst;
      s->ResolvedDecl = static_cast<Decl *>(inst);
    } else {
      s->ResolvedDecl = static_cast<Decl *>(nd);
    }
    for (auto &f : nd->Fields)
      fields.push_back(f.get());
    resultType = nd->DeclaredType;
  } else {
    Diags.error(s->Range, "'{}' cannot be built with braces", sym->Name)
        .note("only structs and struct-shaped enum variants use this form")
        .code(373);
    return Types.errorType();
  }

  std::vector<bool> provided(fields.size(), false);
  for (auto &lf : s->Fields) {
    FieldDecl *found = nullptr;
    size_t idx = 0;
    for (size_t i = 0; i < fields.size(); ++i)
      if (fields[i]->Name == lf.Name) {
        found = fields[i];
        idx = i;
      }
    if (!found) {
      auto d = Diags.error(lf.Range, "no field named '{}'", lf.Name);
      std::string names;
      for (size_t i = 0; i < fields.size(); ++i) {
        if (i) names += ", ";
        names += fields[i]->Name;
      }
      if (!names.empty())
        d.note(fmt("the fields are: {}", names).c_str());
      d.code(374);
      if (lf.Value)
        checkExpr(lf.Value.get(), nullptr);
      continue;
    }
    if (provided[idx])
      Diags.error(lf.Range, "field '{}' is given twice", lf.Name).code(375);
    provided[idx] = true;
    lf.FieldIndex = found->Index;
    Type *want = found->Ty ? found->Ty : Types.errorType();
    if (!lf.Value) {
      // `Point { x, y }` shorthand takes the value from a binding of the
      // same name.
      auto ref = std::make_unique<DeclRefExpr>();
      ref->Range = lf.Range;
      ref->Path = {lf.Name};
      lf.Value = std::move(ref);
    }
    Type *got = checkExpr(lf.Value.get(), want);
    if (insertImplicitConversion(lf.Value, got, want))
      got = lf.Value->Ty;
    requireConvertible(lf.Value.get(), got, want,
                       fmt("field '{}'", lf.Name).c_str());
  }

  if (s->Base) {
    Type *bt = checkExpr(s->Base.get(), resultType);
    requireConvertible(s->Base.get(), bt, resultType, "this `..` base value");
  } else {
    for (size_t i = 0; i < fields.size(); ++i) {
      if (provided[i] || fields[i]->DefaultValue)
        continue;
      auto d = Diags.error(s->Range, "field '{}' is missing", fields[i]->Name);
      d.note(fmt("it has type '{}'",
                    fields[i]->Ty ? fields[i]->Ty->toString() : "?")
                 .c_str())
          .code(376);
      noteDeclaredAt(d, fields[i], "declared here",
                     "give it a value, a default, or fill it in with `..`");
    }
  }
  return resultType;
}

Type *Sema::checkArrayLit(ArrayLitExpr *a, Type *expected) {
  Type *elemHint = nullptr;
  if (expected && (expected->is(TypeKind::Array) || expected->is(TypeKind::Slice)))
    elemHint = expected->element();

  if (a->RepeatCount) {
    Type *value = a->Elements.empty()
                      ? Types.errorType()
                      : checkExpr(a->Elements[0].get(), elemHint);
    Type *count = checkExpr(a->RepeatCount.get(), Types.i64());
    if (!count->isInt() && !count->isError())
      Diags.error(a->RepeatCount->Range, "expected an integer count — got '{}'",
                  count->toString())
          .code(380);
    uint64_t n = 0;
    int64_t folded = 0;
    if (evalConstInt(a->RepeatCount.get(), folded) && folded >= 0) {
      n = static_cast<uint64_t>(folded);
    } else {
      Diags.error(a->RepeatCount->Range,
                  "the length of `[value; count]` must be a non-negative "
                  "constant expression")
          .note("use a slice `[T]` when the length is only known at run time")
          .code(381);
    }
    return Types.arrayOf(value, n);
  }

  Type *elem = elemHint;
  for (auto &e : a->Elements) {
    Type *et = checkExpr(e.get(), elem);
    if (!elem) {
      elem = et;
    } else if (!isImplicitlyConvertible(et, elem)) {
      Type *p = promote(elem, et);
      if (p) {
        elem = p;
      } else {
        Diags.error(e->Range, "expected '{}' — got '{}'", elem->toString(),
                    et->toString())
            .note("every element of an array literal must have the same type")
            .code(382);
      }
    }
  }
  if (!elem) {
    if (expected && expected->is(TypeKind::Array))
      return expected;
    Diags.error(a->Range, "cannot infer the element type of an empty array")
        .note("annotate the binding, e.g. `values: [0:i64] = []`")
        .code(383);
    return Types.errorType();
  }
  return Types.arrayOf(elem, a->Elements.size());
}

//===----------------------------------------------------------------------===//
// Control flow
//===----------------------------------------------------------------------===//

Type *Sema::checkIf(IfExpr *i, Type *expected, bool discardBranches) {
  // `value is SomeClass` reads as a pattern, but a bare name that turns out to
  // be a class is a dynamic type test. Rewrite it before checking anything, so
  // the condition ends up an ordinary boolean.
  if (i->BindingPat) {
    std::vector<std::string> path;
    if (auto *bp = dyn_cast<BindingPattern>(i->BindingPat.get())) {
      if (!bp->Sub)
        path = {bp->Name};
    } else if (auto *pp = dyn_cast<PathPattern>(i->BindingPat.get())) {
      path = pp->Path;
    }
    if (!path.empty()) {
      Symbol *sym = lookupPath(path, i->BindingPat->Range, /*quiet=*/true);
      if (sym && sym->Kind == SymbolKind::TypeName && sym->D &&
          isa<ClassDecl>(sym->D)) {
        auto named = std::make_unique<NamedTypeRepr>();
        named->Range = i->BindingPat->Range;
        named->NameRange = i->BindingPat->Range;
        named->Path = path;
        auto test = std::make_unique<TypeTestExpr>();
        test->Range = i->Cond->Range.merge(i->BindingPat->Range);
        test->Operand = std::move(i->Cond);
        test->TargetType = std::move(named);
        i->Cond = std::move(test);
        i->BindingPat = nullptr;
      }
    }
  }

  Type *ct = checkExpr(i->Cond.get(), i->BindingPat ? nullptr : Types.boolType());
  if (rewriteAnyTypeTest(i->Cond, i->BindingPat, ct))
    ct = Types.boolType();

  pushScope(ScopeKind::Block);
  if (i->BindingPat) {
    Type *subject = ct;
    while (subject->is(TypeKind::Pointer))
      subject = subject->pointee();
    checkPattern(i->BindingPat.get(), subject, /*declaresBindings=*/false,
                 /*isMutable=*/false);
  } else if (!ct->isBool() && !ct->isError()) {
    auto d = Diags.error(i->Cond->Range, "expected 'bool' — got '{}'",
                         ct->toString());
    d.note("an `if` condition must be a boolean").code(311);
    if (isOptionType(ct))
      d.note("test an Option with `.hasValue()`, `is Option::Some(v)`, or a "
             "`match`");
    if (ct->isInt())
      d.note("Rune has no implicit truthiness; compare explicitly, e.g. "
             "`value != 0`");
  }

  ValueDiscarded = discardBranches;
  Type *thenTy = checkBlock(i->Then.get(), expected);
  popScope();

  if (!i->Else) {
    // Without an `else` there is no value: when the condition is false there
    // is nothing to produce. That is fine where the value is thrown away, and
    // is the whole problem where it is not — so say so here, at the `if`,
    // rather than letting whatever wanted the value complain about `()`.
    if (expected && !expected->isVoid() && !expected->isError() &&
        !discardBranches) {
      auto d = Diags.error(i->Range,
                           "this `if` has no `else`, so it produces nothing "
                           "when the condition is false");
      d.note(fmt("something here wants a '{}', and one branch cannot supply "
                 "it on its own", expected->toString())
                 .c_str());
      d.note("add an `else` with the value to use instead, or use a `match` "
             "when the cases are an enum's");
      if (thenTy && !thenTy->isVoid() && !thenTy->isError())
        d.related(i->Then->Range,
                  fmt("this branch produces '{}'", thenTy->toString()),
                  "the other case has nothing to produce");
      d.code(233);
      return expected;
    }
    return Types.voidType();
  }
  ValueDiscarded = discardBranches;
  Type *elseTy = checkExpr(i->Else.get(), expected ? expected : thenTy);
  // Nobody wants this value, so there is nothing for the branches to agree
  // on. Each was checked on its own terms; the `if` produces `()`.
  if (discardBranches)
    return Types.voidType();
  // As in a `match`: when the context asked for a type and both branches can
  // produce it, that is the answer. Unifying the branches with each other
  // first would reject `if c { "yes" } else { code }` for a function returning
  // `Result<String, i64>`, where each branch is a different half of the same
  // type.
  if (expected && !expected->isError()) {
    const bool thenOk = !thenTy->isError() &&
                        isImplicitlyConvertible(thenTy, expected);
    const bool elseOk = !elseTy->isError() &&
                        isImplicitlyConvertible(elseTy, expected);
    if (thenOk && elseOk)
      return expected;
  }
  Type *unified = unifyBranches(thenTy, elseTy, i->Else.get(), "an `if`");
  return unified;
}

void Sema::checkExhaustive(MatchExpr *m, Type *scrutinee) {
  if (!scrutinee || scrutinee->isError())
    return;
  // A wildcard or plain binding arm covers everything.
  for (const auto &arm : m->Arms) {
    if (arm.Guard)
      continue;
    if (patternIsIrrefutable(arm.Pat.get()))
      return;
  }
  if (scrutinee->is(TypeKind::Enum) && !scrutinee->isOpaque()) {
    auto *e = reinterpret_cast<EnumDecl *>(scrutinee->nominal());
    std::vector<bool> covered(e->Variants.size(), false);
    std::function<void(const Pattern *)> mark = [&](const Pattern *p) {
      if (!p)
        return;
      int idx = -1;
      if (const auto *b = dyn_cast<BindingPattern>(p))
        idx = b->VariantIndex;
      else if (const auto *pp = dyn_cast<PathPattern>(p))
        idx = pp->VariantIndex;
      else if (const auto *ep = dyn_cast<EnumPattern>(p))
        idx = ep->VariantIndex;
      else if (const auto *sp = dyn_cast<StructPattern>(p))
        idx = sp->VariantIndex;
      else if (const auto *op = dyn_cast<OrPattern>(p)) {
        for (const auto &alt : op->Alternatives)
          mark(alt.get());
        return;
      }
      if (idx >= 0 && static_cast<size_t>(idx) < covered.size())
        covered[static_cast<size_t>(idx)] = true;
    };
    for (const auto &arm : m->Arms)
      if (!arm.Guard)
        mark(arm.Pat.get());

    std::string missing;
    unsigned count = 0;
    for (size_t i = 0; i < covered.size(); ++i)
      if (!covered[i]) {
        if (count++)
          missing += ", ";
        if (count <= 4)
          missing += e->Variants[i]->Name;
      }
    if (count) {
      if (count > 4)
        missing += fmt(" and {} more", count - 4);
      auto d = Diags.error(m->Range, "this `match` does not cover every case");
      d.note(fmt("missing: {}", missing).c_str())
          .note("add the remaining arms, or a `_ => ...` catch-all")
          .code(390);
      noteDeclaredAt(d, e, fmt("'{}' declared here", e->Name),
                     "every variant must be handled");
    }
    return;
  }
  // A slice is matched by length. An arm whose elements all match covers one
  // length exactly, or — with `..` — every length from a minimum upwards.
  // The arms are exhaustive when some open-ended arm exists and every length
  // below its minimum is covered by an arm of its own.
  if (scrutinee->is(TypeKind::Slice) && !scrutinee->isOpaque()) {
    std::set<size_t> exact;
    size_t openFrom = SIZE_MAX;
    std::function<void(const Pattern *)> mark = [&](const Pattern *p) {
      if (const auto *op = dyn_cast<OrPattern>(p)) {
        for (const auto &alt : op->Alternatives)
          mark(alt.get());
        return;
      }
      const auto *sp = dyn_cast<SlicePattern>(p);
      if (!sp)
        return;
      for (const auto &e : sp->Prefix)
        if (!patternIsIrrefutable(e.get()))
          return;
      for (const auto &e : sp->Suffix)
        if (!patternIsIrrefutable(e.get()))
          return;
      const size_t named = sp->Prefix.size() + sp->Suffix.size();
      if (sp->HasRest)
        openFrom = std::min(openFrom, named);
      else
        exact.insert(named);
    };
    for (const auto &arm : m->Arms)
      if (!arm.Guard)
        mark(arm.Pat.get());
    if (openFrom != SIZE_MAX) {
      size_t missing = SIZE_MAX;
      for (size_t n = 0; n < openFrom; ++n)
        if (!exact.count(n)) {
          missing = n;
          break;
        }
      if (missing == SIZE_MAX)
        return;
      auto d = Diags.error(m->Range, "this `match` does not cover every case");
      d.note(fmt("a slice of {} element(s) matches no arm", missing).c_str())
          .note("add an arm for it, or a `[..]` or `_ => ...` catch-all")
          .code(390);
      return;
    }
    Diags.error(m->Range, "this `match` does not cover every case")
        .note("no arm accepts a slice of every length: add one with `..`, "
              "or a `_ => ...` catch-all")
        .code(390);
    return;
  }
  if (scrutinee->isBool() && !scrutinee->isOpaque()) {
    bool sawTrue = false, sawFalse = false;
    for (const auto &arm : m->Arms) {
      if (arm.Guard)
        continue;
      if (const auto *lp = dyn_cast<LiteralPattern>(arm.Pat.get()))
        if (const auto *bl = dyn_cast<BoolLitExpr>(lp->Value.get()))
          (bl->Value ? sawTrue : sawFalse) = true;
    }
    if (sawTrue && sawFalse)
      return;
  }
  Diags.error(m->Range, "this `match` does not cover every case")
      .note(fmt("values of type '{}' need a `_ => ...` catch-all arm",
                   scrutinee->toString())
                .c_str())
      .code(390);
}

Type *Sema::checkMatch(MatchExpr *m, Type *expected, bool discardBranches) {
  Type *st = checkExpr(m->Scrutinee.get(), nullptr);
  // Matching through a borrow is the common case inside a `&self` method, so
  // look past pointers and match the value they refer to.
  while (st->is(TypeKind::Pointer))
    st = st->pointee();
  Type *result = nullptr;

  for (auto &arm : m->Arms) {
    pushScope(ScopeKind::Block);
    checkPattern(arm.Pat.get(), st, /*declaresBindings=*/false,
                 /*isMutable=*/false);
    if (arm.Guard) {
      Type *gt = checkExpr(arm.Guard.get(), Types.boolType());
      if (!gt->isBool() && !gt->isError())
        Diags.error(arm.Guard->Range, "expected 'bool' — got '{}'",
                    gt->toString())
            .note("a match guard must be a boolean")
            .code(311);
    }
    ValueDiscarded = discardBranches;
    Type *bt = checkExpr(arm.Body.get(), expected ? expected : result);
    popScope();
    if (discardBranches) {
      // Nothing wants the value, so the arms have nothing to agree on.
      result = Types.voidType();
      continue;
    }
    // When the context already said what it wants, an arm that can produce it
    // does. Unifying the arms against each other first would compare a
    // `Vector<String>` with a `jsonError` and give up, even though both are
    // perfectly good halves of the `Result` this match is being asked for.
    if (expected && !expected->isError() && !bt->isError() &&
        isImplicitlyConvertible(bt, expected))
      bt = expected;
    result = result ? unifyBranches(result, bt, arm.Body.get(), "a `match`") : bt;
  }

  if (m->Arms.empty()) {
    Diags.error(m->Range, "a `match` needs at least one arm").code(391);
    return Types.errorType();
  }
  checkExhaustive(m, st);
  return result ? result : Types.voidType();
}

/// Works out what `for` should drive when the sequence is not one of the
/// three shapes the loop knows natively, and records it on the node. Returns
/// the element type, or the error type when nothing can be iterated.
Type *Sema::resolveIteration(ForExpr *f, Type *seq) {
  Type *bare = TypeContext::stripUniq(seq);
  while (bare->is(TypeKind::Pointer))
    bare = bare->pointee();

  auto cannotIterate = [&]() -> Type * {
    auto d = Diags.error(f->Sequence->Range, "cannot iterate over '{}'",
                         seq->toString());
    d.note("`for` works over ranges (`0..n`), arrays, slices, and anything "
           "bound to `Iterator` or `Sequence`")
        .code(393);
    if (bare->is(TypeKind::String))
      d.note("iterate a String by index with `.length()` and `.at(i)`");
    else if (bare->is(TypeKind::DynMark) && bare->mark() == IteratorDecl)
      d.note("a `dyn Iterator` does not carry its `Item` type, so there is "
             "nothing for the loop variable to be — iterate the concrete "
             "type, or give the mark object's element type a name of its own");
    else if (bare->isAny())
      d.note("ask what it holds first: `.get::<[i64]>()` and the rest");
    else if (!IteratorDecl)
      d.note("`Iterator` is unavailable; this build was compiled with "
             "--no-stdlib");
    else
      d.note(fmt("add `bind Iterator to {}` — `type Item` and "
                 "`fn next(&var self) -> Self::Item?` — or a `Sequence` "
                 "binding that hands one over",
                 bare->toString())
                 .c_str());
    return Types.errorType();
  };

  if (!IteratorDecl)
    return cannotIterate();

  // A container is asked for a cursor; a cursor is driven directly. Asking
  // `Iterator` first means a type that is both stays its own iterator.
  Type *iterTy = bare;
  if (!typeConformsTo(bare, IteratorDecl) && SequenceDecl &&
      typeConformsTo(bare, SequenceDecl)) {
    FunctionDecl *iterate = lookupMarkMethod(bare, SequenceDecl, "iterate");
    if (!iterate || !iterate->Ty)
      return cannotIterate();
    f->IterateMethod = iterate;
    iterTy = iterate->Ty->result();
  }

  FunctionDecl *next = lookupMarkMethod(iterTy, IteratorDecl, "next");
  if (!next || !next->Ty)
    return cannotIterate();

  Type *result = next->Ty->result();
  if (!isOptionType(result)) {
    // The bind checker already reports a signature that disagrees with the
    // requirement; stop rather than lower something that has no shape.
    return Types.errorType();
  }
  f->NextMethod = next;
  f->IterType = iterTy;
  f->NextResult = result;
  return optionPayload(result);
}

Type *Sema::checkFor(ForExpr *f) {
  Type *seq = checkExpr(f->Sequence.get(), nullptr);
  Type *elem = Types.errorType();

  if (isa<RangeExpr>(f->Sequence.get())) {
    auto *r = cast<RangeExpr>(f->Sequence.get());
    if (!r->Lo || !r->Hi) {
      Diags.error(f->Sequence->Range, "a `for` range needs both bounds")
          .note("write `for i in 0..n { ... }`")
          .code(392);
    }
    elem = seq->is(TypeKind::Tuple) && !seq->tupleElements().empty()
               ? seq->tupleElements()[0]
               : Types.i64();
  } else if (!seq->isOpaque() &&
             (seq->is(TypeKind::Array) || seq->is(TypeKind::Slice))) {
    elem = seq->element();
  } else if (seq->is(TypeKind::Pointer) && seq->pointee() &&
             (seq->pointee()->is(TypeKind::Array) ||
              seq->pointee()->is(TypeKind::Slice))) {
    elem = seq->pointee()->element();
  } else if (!seq->isError()) {
    // A `some Iterator` takes this route too: what it walks as is a
    // question for the mark, not for the type behind it.
    elem = resolveIteration(f, seq);
  }

  // `for (key, value) in map` where each item is a borrow of a pair: the
  // pattern takes the pair apart where it lies, as a `match` on the borrow
  // would. So that is what the loop becomes — one name for the item, and a
  // `match` on it around the body — which every later pass already treats
  // as matching through a reference: aliases under single ownership, in
  // place under counting.
  if (f->Binding && elem && elem->is(TypeKind::Pointer) &&
      !elem->isRawPointer() && !elem->isWeakPointer() &&
      f->Binding->Kind != NodeKind::BindingPat &&
      f->Binding->Kind != NodeKind::WildcardPat &&
      f->Binding->Kind != NodeKind::RefPat && f->Body) {
    std::string name = fmt("item${}", ForItemCounter++);
    SourceRange at = f->Binding->Range;
    auto item = std::make_unique<BindingPattern>();
    item->Name = name;
    item->Range = at;
    auto scrut = std::make_unique<DeclRefExpr>();
    scrut->Path = {name};
    scrut->Range = at;
    auto m = std::make_unique<MatchExpr>();
    m->Range = f->Body->Range;
    m->Scrutinee = std::move(scrut);
    MatchArm arm;
    arm.Pat = std::move(f->Binding);
    arm.Range = at;
    arm.Body = std::move(f->Body);
    m->Arms.push_back(std::move(arm));
    auto body = std::make_unique<BlockExpr>();
    body->Range = m->Range;
    auto stmt = std::make_unique<ExprStmt>();
    stmt->Range = m->Range;
    stmt->Value = std::move(m);
    body->Stmts.push_back(std::move(stmt));
    f->Body = std::move(body);
    f->Binding = std::move(item);
  }

  pushScope(ScopeKind::Block);
  checkPattern(f->Binding.get(), elem, /*declaresBindings=*/true,
               /*isMutable=*/false);
  // Every turn has to bind. A pattern that can fail would silently skip
  // values, or — worse — match everything and bind nothing. Asked after the
  // pattern is checked, because whether `[a, b]` can fail depends on what it
  // was matched against.
  if (!patternIsIrrefutable(f->Binding.get())) {
    Diags.error(f->Binding->Range,
                "a `for` pattern has to match every value")
        .note("this one can fail, and a loop has nowhere to put the values it "
              "would skip")
        .note("bind the value and take it apart with `match` inside the body")
        .code(394);
  }
  FunctionContext *fc = fn();
  if (fc)
    fc->Loops.push_back(LoopInfo{f->Label, nullptr, false, f->Range});
  ValueDiscarded = true;
  checkBlock(f->Body.get(), nullptr);
  if (fc)
    fc->Loops.pop_back();
  popScope();
  return Types.voidType();
}

//===----------------------------------------------------------------------===//
// Closures
//===----------------------------------------------------------------------===//

/// What `v[i]` is, given the `index` it calls. A lent element that is plain
/// data is read out — the conversion `let n: i64 = r` would make anyway — so
/// `let n = v[i]` is a number and `v[i] + 1` needs nothing; one that owns
/// something stays a borrow, which is all that can be had without a copy.
Type *Sema::indexResult(IndexExpr *i, FunctionDecl *impl) {
  Type *r = impl->Ty ? impl->Ty->result() : Types.errorType();
  if (r->is(TypeKind::Pointer) && !r->isRawPointer() &&
      !r->isMutablePointer() && !r->isWeakPointer() && r->pointee() &&
      !r->pointee()->is(TypeKind::Pointer) &&
      isImplicitlyConvertible(r, r->pointee())) {
    i->ReadsThrough = true;
    return r->pointee();
  }
  i->ReadsThrough = false;
  return r;
}

Type *Sema::checkClosure(ClosureExpr *c, Type *expected) {
  // What the place this closure is going says about it. The parameters and
  // the result are taken separately, because a call being inferred often
  // knows the first and not the second: `map`'s `@function(Item) -> B` says
  // exactly what the closure is handed while `B` is the very thing the call
  // is working out.
  const std::vector<Type *> *paramHint = nullptr;
  Type *returnHint = nullptr;
  if (expected &&
      (expected->is(TypeKind::Function) || expected->is(TypeKind::CFunction))) {
    if (expected->params().size() == c->Params.size()) {
      bool known = true;
      for (Type *p : expected->params())
        if (!p || p->isError() || p->containsGenericParam())
          known = false;
      if (known)
        paramHint = &expected->params();
    }
    returnHint = expected->result();
    if (returnHint &&
        (returnHint->isError() || returnHint->containsGenericParam()))
      returnHint = nullptr;
  }

  std::vector<Type *> paramTypes;
  for (size_t i = 0; i < c->Params.size(); ++i) {
    Param &p = c->Params[i];
    Type *t = p.TypeAnnotation ? resolveTypeOrError(p.TypeAnnotation.get(),
                                                    Types.errorType())
                               : nullptr;
    if (!t && paramHint)
      t = (*paramHint)[i];
    if (!t) {
      auto d = Diags.error(p.Range, "cannot infer the type of parameter '{}'",
                           p.Name);
      d.note("a parameter may be left bare where the closure is going "
             "somewhere that says what it takes — an argument, an annotated "
             "binding — and this place says nothing");
      d.note("annotate it, e.g. `||(value: i64) -> i64 { ... }`").code(394);
      t = Types.errorType();
    }
    // `||(n: i64) { n + 1 }` handed to something that gives it `&i64` — what
    // a borrowing iterator's `map` does — reads the number out on the way
    // in, as `let n: i64 = r` would. Only where that conversion is one the
    // language already makes: plain data, never an owning value, which the
    // closure would otherwise be copying without saying so. The parameter
    // takes the borrow under a name of its own, and the body starts by
    // binding the written name to what it points at.
    if (paramHint && p.TypeAnnotation && c->Body && !t->isError()) {
      Type *hint = (*paramHint)[i];
      // How many shared borrows stand between what is handed over and what
      // was written: `filter` on a vector of numbers gives `&&i64`.
      auto sharedBorrow = [](Type *x) {
        return x && x->is(TypeKind::Pointer) && !x->isRawPointer() &&
               !x->isMutablePointer() && !x->isWeakPointer();
      };
      unsigned levels = 0;
      Type *under = hint;
      while (sharedBorrow(under) && under != t) {
        under = under->pointee();
        ++levels;
      }
      // The last step is the one the language already makes on its own —
      // `&i64` to `i64`, `&&Row` to `&Row`; the ones before it read through
      // borrows, which copy nothing.
      Type *lastStep = levels ? Types.pointerTo(t, false, false) : nullptr;
      if (levels && under == t &&
          (sharedBorrow(t) || isImplicitlyConvertible(lastStep, t))) {
        std::string written = p.Name;
        p.Name = written + "$ref";
        auto binding = std::make_unique<BindingPattern>();
        binding->Name = written;
        binding->Range = p.Range;
        auto ref = std::make_unique<DeclRefExpr>();
        ref->Path = {p.Name};
        ref->Range = p.Range;
        ExprPtr read = std::move(ref);
        for (unsigned k = 0; k + 1 < levels; ++k) {
          auto d = std::make_unique<DerefExpr>();
          d->Operand = std::move(read);
          d->Range = p.Range;
          read = std::move(d);
        }
        if (sharedBorrow(t)) {
          // Handing a borrow on as a borrow: one more read through.
          auto d = std::make_unique<DerefExpr>();
          d->Operand = std::move(read);
          d->Range = p.Range;
          read = std::move(d);
        }
        auto decl = std::make_unique<VarStmtNode>();
        decl->Binding = std::move(binding);
        decl->TypeAnnotation = cloneTypeRepr(p.TypeAnnotation.get());
        decl->Init = std::move(read);
        decl->Range = p.Range;
        c->Body->Stmts.insert(c->Body->Stmts.begin(), std::move(decl));
        p.TypeAnnotation = nullptr;
        t = hint;
      } else if (levels && under == t) {
        // What is handed over is lent, and what was written would be a copy
        // of something that owns what it holds. Say what to write instead of
        // leaving it to the mismatch the call reports.
        auto d = Diags.error(p.Range,
                             "'{}' is lent a '{}' here, not given a '{}'",
                             p.Name, hint->toString(), t->toString());
        d.note(fmt("write `{}: {}`, and `.$clone()` what the closure keeps",
                   p.Name, Types.pointerTo(t, false, false)->toString())
                   .c_str());
        d.note("the items of a container are borrowed as they are walked: "
               "copying each would copy what it owns, so it is asked for");
        d.code(384);
        t = hint;
      }
    }
    p.Ty = t;
    paramTypes.push_back(t);
  }

  Type *declaredReturn =
      c->ReturnType ? resolveTypeOrError(c->ReturnType.get(), Types.errorType())
                    : returnHint;

  // Closures become real functions; CodeGen emits them with an environment
  // parameter holding the captures.
  Synthesised.push_back(std::make_unique<FunctionDecl>());
  auto *lifted = static_cast<FunctionDecl *>(Synthesised.back().get());
  // Numbered by a counter of its own rather than by how many functions have
  // been collected so far: a closure nested directly inside another is
  // checked before the outer one is added, and the two would share a name.
  lifted->Name = fmt("closure#{}", ClosureCounter++);
  // The body of an `async fn` is named after the function, so a traceback
  // through a task reads `fetch#task` rather than `closure#12`. A generic
  // one keeps the number: its instantiations would otherwise share a name.
  if (c->IsAsyncBody) {
    FunctionContext *outer = fn();
    if (outer && outer->Fn && outer->Fn->IsAsync && !outer->Closure &&
        outer->Fn->Generics.empty() && !outer->Fn->GenericTemplate) {
      std::string owner =
          outer->Fn->OwnerType ? outer->Fn->OwnerType->toString() + "::" : "";
      lifted->Name = owner + outer->Fn->Name + "#task";
    }
  }
  lifted->Range = c->Range;
  lifted->NameRange = c->Range;
  lifted->Flavour = FunctionFlavour::Closure;
  lifted->SourceClosure = c;
  lifted->ModulePath = CurModule ? CurModule->Name : "";
  lifted->MangledName = mangleFunction(lifted, {});
  c->Lifted = lifted;

  FunctionContext ctx;
  ctx.Fn = lifted;
  ctx.Closure = c;
  ctx.ReturnType = declaredReturn ? declaredReturn : Types.voidType();
  // A closure inherits the enclosing function's `self` and safety context.
  if (FunctionContext *outer = fn()) {
    ctx.SelfType = outer->SelfType;
    ctx.SelfClass = outer->SelfClass;
    ctx.SelfVar = outer->SelfVar;
    ctx.InUnsafeContext = outer->InUnsafeContext;
  }
  FnStack.push_back(ctx);
  pushScope(ScopeKind::Function);

  unsigned depth = static_cast<unsigned>(FnStack.size());
  for (Param &p : c->Params) {
    VarDecl *v = declareLocal(p.Name, p.Ty, p.IsMutable, p.Range,
                              /*isParam=*/true);
    p.Binding = v;
    VarDepth[v] = depth;
  }

  Type *bodyTy = checkBlock(c->Body.get(), declaredReturn);
  Type *finalReturn = declaredReturn;
  if (!finalReturn)
    finalReturn = c->Body->Tail ? bodyTy : Types.voidType();
  else if (c->Body->Tail && !finalReturn->isVoid())
    requireConvertible(c->Body->Tail.get(), bodyTy, finalReturn,
                       c->IsAsyncBody ? "this async function's result"
                                      : "this closure's result");

  popScope();
  FnStack.pop_back();

  // Mirror the closure onto the lifted function so CodeGen has a signature.
  for (const Param &p : c->Params)
    lifted->Params.push_back(cloneParam(p));
  for (size_t i = 0; i < lifted->Params.size(); ++i) {
    lifted->Params[i].Ty = c->Params[i].Ty;
    lifted->Params[i].Binding = c->Params[i].Binding;
  }
  lifted->Captures = c->Captures;
  lifted->Ty = Types.functionOf(paramTypes, finalReturn);
  lifted->Body = nullptr; // CodeGen walks the ClosureExpr's body directly
  Result.Functions.push_back(lifted);

  return lifted->Ty;
}

//===----------------------------------------------------------------------===//
// Casts and `?`
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Unique ownership
//
// A `uniq` reference is represented exactly like any other class reference.
// What makes it single-owner is that Sema refuses to duplicate one: it may be
// moved with `move`, or borrowed by reading through it, and nothing else. A
// second reference therefore never exists, its count never rises above one,
// and a ring of `uniq` edges cannot be built. That is the whole guarantee —
// there is no runtime machinery behind it.
//===----------------------------------------------------------------------===//

bool Sema::inGenericBody() const {
  for (const auto &f : FnStack) {
    if (f.Fn && (f.Fn->GenericTemplate || !f.Fn->Generics.empty()))
      return true;
    // A method of an instantiated generic type — `Option<uniq Node>::unwrap`
    // — was written against a plain `T` just the same.
    if (f.SelfType && f.SelfType->isNominal()) {
      NominalDecl *nd = f.SelfType->nominal();
      if (nd && (nd->GenericTemplate || !f.SelfType->typeArguments().empty()))
        return true;
    }
  }
  return false;
}

bool Sema::isFreshConstruction(const Expr *e) const {
  // `Node(1)` — the object is brand new, so nothing else can refer to it yet
  // and it may become a `uniq` without anyone having to move it.
  const auto *c = dyn_cast<CallExpr>(e);
  if (!c || !c->Ty || !c->Ty->is(TypeKind::Class))
    return false;
  // A construction call resolves either to the class's `init` or, for a class
  // with none, to the class itself.
  if (const FunctionDecl *fn = c->Target)
    return fn->Flavour == FunctionFlavour::Initialiser;
  return c->Callee && c->Callee->Ty && c->Callee->Ty->is(TypeKind::Class);
}

bool Sema::rejectUniqCopy(Expr *e, Type *t, const char *what) {
  if (!t || !t->isUniq() || !e)
    return false;
  if (isa<MoveExpr>(e) || isFreshConstruction(e))
    return false;
  auto d = Diags.error(e->Range, "cannot copy a `Unique` reference into {}",
                       what);
  d.note("a `Unique` reference has exactly one owner — that is what keeps it "
         "out of a reference cycle");
  d.note("borrow it, or hand it over with `.$move()`");
  if (const auto *m = dyn_cast<MemberExpr>(e))
    d.note(fmt("or read through it in place, e.g. `{}.field`", m->Name).c_str());
  d.code(239);
  return true;
}

void Sema::markMoved(Expr *e) {
  auto *r = e ? dyn_cast<DeclRefExpr>(e) : nullptr;
  if (!r || !r->Resolved)
    return;
  if (auto *v = dyn_cast<VarDecl>(r->Resolved)) {
    MovedFrom[v] = e;
    r->MovedOut = true;
  }
}

Type *Sema::checkMove(MoveExpr *m) {
  Type *t = checkExpr(m->Operand.get(), nullptr);
  if (t->isError())
    return t;
  if (!t->isUniq()) {
    Diags.error(m->Range,
                "`.$move()` applies to a `Unique` reference — '{}' is not one",
                t->toString())
        .note("every other value is either copied or reference counted, so "
              "there is nothing to hand over")
        .code(239);
    return t;
  }

  // Moving out of a local makes it unusable from here on. Moving out of a
  // field empties the field, which only an optional one can express.
  if (auto *r = dyn_cast<DeclRefExpr>(m->Operand.get())) {
    if (auto *v = r->Resolved ? dyn_cast<VarDecl>(r->Resolved) : nullptr) {
      m->MovedFrom = v;
      MovedFrom[v] = m;
    }
  } else if (auto *mem = dyn_cast<MemberExpr>(m->Operand.get())) {
    m->FromField = true;
    (void)mem;
    Diags.error(m->Range, "cannot move out of a field")
        .note("the field would be left with nothing in it, and only an "
              "optional field can say that")
        .note("assign a replacement instead, e.g. `node.next = nil`, or read "
              "through the field without moving")
        .code(239);
  }
  return t;
}

bool Sema::insertImplicitConversion(ExprPtr &slot, Type *from, Type *to) {
  if (!slot || !from || !to || !AsDecl)
    return false;
  if (from->isError() || to->isError() || from == to)
    return false;
  if (isImplicitlyConvertible(from, to))
    return false;
  // A `some Mark` keeps its veil: converting along the type behind it would
  // give that type away.
  if (from->isOpaque() || to->isOpaque())
    return false;
  NominalDecl *wanted = instantiateNominal(AsDecl, {to}, slot->Range);
  auto *mark =
      wanted ? dyn_cast<MarkDecl>(static_cast<Decl *>(wanted)) : nullptr;
  FunctionDecl *impl = mark ? lookupMarkMethod(from, mark, "convert") : nullptr;
  if (!impl)
    return false;
  ensureTemplateSignature(impl);
  if (!impl->Ty)
    return false;
  auto conv = std::make_unique<IntoExpr>();
  conv->Range = slot->Range;
  conv->Conversion = impl;
  conv->Ty = impl->Ty->result();
  conv->Category = ValueCategory::RValue;
  conv->Operand = std::move(slot);
  slot = std::move(conv);
  return true;
}

Type *Sema::checkInto(IntoExpr *e) {
  Type *from = checkExpr(e->Operand.get(), nullptr);
  Type *to = resolveTypeOrError(e->TargetType.get(), Types.errorType());
  if (from->isError() || to->isError())
    return to;

  MarkDecl *as = AsDecl;
  if (!as) {
    Diags.error(e->Range, "`into` needs the `As` mark")
        .note("it lives in `std::convert`, which the prelude imports; a build "
              "with `--no-stdlib` has no conversions")
        .code(236);
    return Types.errorType();
  }

  // `x into Fahrenheit` looks for `bind As<Fahrenheit> to <type of x>`,
  // which is also how `bind <type of x> into Fahrenheit` is stored.
  NominalDecl *wanted = instantiateNominal(as, {to}, e->Range);
  auto *wantedMark = wanted ? dyn_cast<MarkDecl>(static_cast<Decl *>(wanted))
                            : nullptr;
  FunctionDecl *impl =
      wantedMark ? lookupMarkMethod(from, wantedMark, "convert") : nullptr;
  if (!impl) {
    auto d = Diags.error(e->Range, "'{}' does not convert into '{}'",
                         from->toString(), to->toString());
    d.note(fmt("write `bind {} into {}` to say how", from->toString(),
                  to->toString())
               .c_str())
        .code(236);
    if (isExplicitlyCastable(from, to))
      d.note(fmt("`{} as {}` is a conversion the compiler already knows",
                    from->toString(), to->toString())
                 .c_str());
    return Types.errorType();
  }
  ensureTemplateSignature(impl);
  e->Conversion = impl;
  return impl->Ty ? impl->Ty->result() : Types.errorType();
}

bool Sema::rewriteAnyTypeTest(ExprPtr &cond, PatternPtr &pat,
                              Type *condType) {
  if (!pat || !cond || !condType)
    return false;
  // Through a borrow: a lent `Any` answers the same question.
  Type *seen = condType;
  while (seen->is(TypeKind::Pointer) && !seen->isRawPointer() &&
         !seen->isWeakPointer() && seen->pointee())
    seen = seen->pointee();
  if (!seen->isAny())
    return false;

  std::vector<std::string> path;
  if (auto *bp = dyn_cast<BindingPattern>(pat.get())) {
    if (!bp->Sub)
      path = {bp->Name};
  } else if (auto *pp = dyn_cast<PathPattern>(pat.get())) {
    path = pp->Path;
  }
  if (path.empty()) {
    Diags.error(pat->Range, "an `Any` has no structure to match against")
        .note("ask what it holds instead: `is T`, `.holds::<T>()`, or "
              "`.get::<T>()` for the value")
        .code(246);
    // Report it as handled: the condition is wrong, but saying so twice —
    // once here and again as "expected bool" — helps nobody.
    pat = nullptr;
    return true;
  }

  auto named = std::make_unique<NamedTypeRepr>();
  named->Range = pat->Range;
  named->NameRange = pat->Range;
  named->Path = path;
  auto test = std::make_unique<TypeTestExpr>();
  test->Range = cond->Range.merge(pat->Range);
  test->Operand = std::move(cond);
  test->TargetType = std::move(named);
  cond = std::move(test);
  pat = nullptr;
  // The operand carries its type already, so re-checking it costs nothing;
  // what this runs is the type test's own validation.
  checkExpr(cond.get(), Types.boolType());
  return true;
}

Type *Sema::checkCast(CastExpr *c) {
  Type *from = checkExpr(c->Operand.get(), nullptr);
  Type *to = resolveTypeOrError(c->TargetType.get(), Types.errorType());
  if (from->isError() || to->isError())
    return to;

  // Naming a function yields a closure value, but a *declared* function has no
  // environment, so its address alone is meaningful: that is what a C callback
  // slot or a vtable entry wants.
  bool wantsAddress = to->is(TypeKind::CFunction) ||
                      (to->is(TypeKind::Pointer) && to->isRawPointer());
  if (from->is(TypeKind::Function) && wantsAddress) {
    auto *ref = dyn_cast<DeclRefExpr>(c->Operand.get());
    if (ref && ref->Resolved && isa<FunctionDecl>(ref->Resolved)) {
      c->IsFunctionAddress = true;
      reportUnsafe(c->Range,
                   fmt("taking the address of '{}'",
                          cast<FunctionDecl>(ref->Resolved)->Name),
                   "wrap it in `unsafe { ... }`, mark the function @unsafe, or "
                   "justify it with @safe(\"reason\")");
      return to;
    }
    auto d = Diags.error(c->Range, "only a declared function has an address");
    d.note("a closure carries an environment, so it cannot become a bare "
           "pointer")
        .code(395);
    return to;
  }

  // A float-valued enum's `as` gives the value each variant was declared
  // with, so any number will do as the target.
  if (from->is(TypeKind::Enum) && to->isNumeric())
    if (NominalDecl *nd = from->nominal())
      if (auto *en = dyn_cast<EnumDecl>(static_cast<Decl *>(nd)))
        if (en->RawFloat)
          return to;

  if (!isExplicitlyCastable(from, to)) {
    auto d = Diags.error(c->Range, "cannot cast '{}' to '{}'", from->toString(),
                         to->toString());
    d.note("`as` converts between numbers, enums and pointers, and its "
           "meanings are the compiler's own")
        .code(395);
    // If the program supplied this conversion itself, name the spelling that
    // reaches it.
    bool haveInto = false;
    if (AsDecl)
      if (NominalDecl *inst = instantiateNominal(AsDecl, {to}, c->Range))
        if (auto *mk = dyn_cast<MarkDecl>(static_cast<Decl *>(inst)))
          haveInto = lookupMarkMethod(from, mk, "convert") != nullptr;
    if (haveInto)
      d.note(fmt("`{} into {}` is the conversion this program defines",
                    from->toString(), to->toString())
                 .c_str());
    else
      d.note(fmt("define one with `bind {} into {}`, then write "
                    "`value into {}`", from->toString(), to->toString(),
                    to->toString())
                 .c_str());
    return to;
  }

  bool fromPtr = from->is(TypeKind::Pointer) || from->is(TypeKind::CString);
  bool toPtr = to->is(TypeKind::Pointer) || to->is(TypeKind::CString);
  if ((fromPtr && to->isInt()) || (from->isInt() && toPtr) ||
      (to->is(TypeKind::Pointer) && to->isRawPointer() && !from->isError()) ||
      (fromPtr && toPtr && from->pointee() != to->pointee()))
    reportUnsafe(c->Range,
                 fmt("cast from '{}' to '{}'", from->toString(),
                        to->toString()),
                 "wrap it in `unsafe { ... }`, mark the function @unsafe, or "
                 "justify it with @safe(\"reason\")");
  return to;
}

Type *Sema::checkTry(TryExpr *t) {
  Type *ot = checkExpr(t->Operand.get(), nullptr);
  if (ot->isError())
    return ot;

  const bool isOption = isOptionType(ot);
  const bool isResult = isResultType(ot);
  if (!isOption && !isResult) {
    Diags.error(t->Range, "`?` needs an Option or a Result, but got '{}'",
                ot->toString())
        .note("`?` returns early when the value is `None` or `Err`")
        .code(396);
    return ot;
  }

  FunctionContext *f = fn();
  Type *want = f ? f->ReturnType : nullptr;
  const bool wantOption = want && isOptionType(want);
  const bool wantResult = want && isResultType(want);

  if (isOption && !wantOption) {
    auto d = Diags.error(t->Range,
                         "`?` on an Option needs a function that returns an "
                         "Option");
    d.note(fmt("declare the result as `-> {}?`",
               optionPayload(ot) ? optionPayload(ot)->toString() : "T")
               .c_str())
        .code(397);
    if (f && f->Fn && f->Fn->NameRange.isValid())
      d.related(f->Fn->NameRange, fmt("'{}' declared here", f->Fn->Name),
                "its result type has to be an Option for `?` to return early");
  } else if (isResult && !wantResult) {
    auto d = Diags.error(t->Range,
                         "`?` on a Result needs a function that returns a "
                         "Result");
    d.note(fmt("declare the result as `-> Result<T, {}>`",
               resultError(ot) ? resultError(ot)->toString() : "E")
               .c_str())
        .code(397);
    if (f && f->Fn && f->Fn->NameRange.isValid())
      d.related(f->Fn->NameRange, fmt("'{}' declared here", f->Fn->Name),
                "its result type has to be a Result for `?` to return early");
  } else if (isResult && wantResult) {
    // The error travels out as the function's own error type. The same type
    // passes through untouched; one that converts implicitly is widened; and
    // one the program has written a conversion for — `bind Theirs into Ours`,
    // the same as `bind As<Ours> to Theirs` — goes through that `convert`, so
    // a layered library can hand a low-level failure up without a `mapErr`
    // at every boundary.
    Type *from = resultError(ot);
    Type *to = resultError(want);
    if (from && to && from != to && !from->isError() && !to->isError()) {
      if (isImplicitlyConvertible(from, to)) {
        t->ErrorCoerces = true;
      } else if (FunctionDecl *conv = lookupErrorConversion(from, to, t->Range)) {
        t->ErrorConversion = conv;
      } else {
        auto d = Diags.error(t->Range, "expected '{}' — got '{}'",
                             to->toString(), from->toString());
        d.note("`?` hands the error to the caller as the function's own error "
               "type, and nothing says how to get there from this one")
            .note(fmt("write `bind {} into {}` with a `convert` to say how, "
                      "or `.mapErr(...)` before the `?`",
                      from->toString(), to->toString())
                      .c_str())
            .code(398);
      }
    }
  }

  return isOption ? optionPayload(ot) : resultValue(ot);
}

/// The `convert` a `bind As<to> to from` supplies, or null when there is no
/// such binding. Quiet: the caller decides what a missing one means.
FunctionDecl *Sema::lookupErrorConversion(Type *from, Type *to,
                                          SourceRange at) {
  if (!AsDecl)
    return nullptr;
  NominalDecl *wanted = instantiateNominal(AsDecl, {to}, at);
  auto *wantedMark = wanted ? dyn_cast<MarkDecl>(static_cast<Decl *>(wanted))
                            : nullptr;
  FunctionDecl *impl =
      wantedMark ? lookupMarkMethod(from, wantedMark, "convert") : nullptr;
  if (!impl)
    return nullptr;
  ensureTemplateSignature(impl);
  return impl;
}

} // namespace rune
