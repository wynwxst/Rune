#include "rune/ASTClone.h"

namespace rune {

namespace {

template <typename T> std::unique_ptr<T> alloc(const Node *from) {
  auto n = std::make_unique<T>();
  n->Range = from->Range;
  return n;
}

template <typename T>
std::vector<T> cloneVector(const std::vector<T> &in,
                           T (*fn)(const typename T::element_type *)) {
  std::vector<T> out;
  out.reserve(in.size());
  for (const auto &e : in)
    out.push_back(fn(e.get()));
  return out;
}

std::vector<TypeReprPtr> cloneTypeList(const std::vector<TypeReprPtr> &in) {
  std::vector<TypeReprPtr> out;
  out.reserve(in.size());
  for (const auto &t : in)
    out.push_back(cloneTypeRepr(t.get()));
  return out;
}

std::vector<GenericParam> cloneGenerics(const std::vector<GenericParam> &in) {
  std::vector<GenericParam> out;
  for (const auto &g : in) {
    GenericParam c;
    c.Name = g.Name;
    c.Range = g.Range;
    c.Bounds = cloneTypeList(g.Bounds);
    out.push_back(std::move(c));
  }
  return out;
}

std::vector<WhereClause> cloneWheres(const std::vector<WhereClause> &in) {
  std::vector<WhereClause> out;
  for (const auto &w : in) {
    WhereClause c;
    c.Range = w.Range;
    c.Subject = cloneTypeRepr(w.Subject.get());
    c.Bounds = cloneTypeList(w.Bounds);
    out.push_back(std::move(c));
  }
  return out;
}

std::vector<Attribute> cloneAttrs(const std::vector<Attribute> &in) {
  std::vector<Attribute> out;
  for (const auto &a : in)
    out.push_back(cloneAttribute(a));
  return out;
}

std::unique_ptr<FieldDecl> cloneField(const FieldDecl *f) {
  auto c = alloc<FieldDecl>(f);
  c->Name = f->Name;
  c->NameRange = f->NameRange;
  c->IsPublic = f->IsPublic;
  c->ModulePath = f->ModulePath;
  c->Attrs = cloneAttrs(f->Attrs);
  c->TypeAnnotation = cloneTypeRepr(f->TypeAnnotation.get());
  c->DefaultValue = cloneExpr(f->DefaultValue.get());
  c->Index = f->Index;
  c->IsMutable = f->IsMutable;
  c->IsWeak = f->IsWeak;
  return c;
}

} // namespace

std::unique_ptr<FieldDecl> cloneFieldDecl(const FieldDecl *f) {
  return cloneField(f);
}

std::unique_ptr<EnumVariantDecl> cloneEnumVariant(const EnumVariantDecl *v) {
  auto c = alloc<EnumVariantDecl>(v);
  c->Name = v->Name;
  c->NameRange = v->NameRange;
  c->Shape = v->Shape;
  c->Index = v->Index;
  c->Value = v->Value;
  c->TupleTypes = cloneTypeList(v->TupleTypes);
  for (const auto &f : v->Fields)
    c->Fields.push_back(cloneField(f.get()));
  c->Discriminant = cloneExpr(v->Discriminant.get());
  return c;
}

Attribute cloneAttribute(const Attribute &a) {
  Attribute c;
  c.Name = a.Name;
  c.Range = a.Range;
  for (const auto &arg : a.Args)
    c.Args.push_back(cloneExpr(arg.get()));
  return c;
}

/// A `from` clause is source, not resolution: only the paths are copied.
std::unique_ptr<OriginClause> cloneOrigin(const OriginClause *o) {
  if (!o)
    return nullptr;
  auto c = std::make_unique<OriginClause>();
  c->Range = o->Range;
  for (const OriginPlace &place : o->Places) {
    OriginPlace cp;
    cp.Path = place.Path;
    cp.Range = place.Range;
    c->Places.push_back(std::move(cp));
  }
  return c;
}

Param cloneParam(const Param &p) {
  Param c;
  c.Name = p.Name;
  c.Range = p.Range;
  c.IsSelf = p.IsSelf;
  c.SelfByRef = p.SelfByRef;
  c.SelfMutable = p.SelfMutable;
  c.IsMutable = p.IsMutable;
  c.IsVariadic = p.IsVariadic;
  c.HasView = p.HasView;
  for (const FieldPathRepr &f : p.View) {
    FieldPathRepr cf;
    cf.Path = f.Path;
    cf.Range = f.Range;
    c.View.push_back(std::move(cf));
  }
  c.TypeAnnotation = cloneTypeRepr(p.TypeAnnotation.get());
  c.DefaultValue = cloneExpr(p.DefaultValue.get());
  return c;
}

TypeReprPtr cloneTypeReprNoOrigin(const TypeRepr *t);

TypeReprPtr cloneTypeRepr(const TypeRepr *t) {
  TypeReprPtr c = cloneTypeReprNoOrigin(t);
  if (c && t)
    c->Origin = cloneOrigin(t->Origin.get());
  return c;
}

TypeReprPtr cloneTypeReprNoOrigin(const TypeRepr *t) {
  if (!t)
    return nullptr;
  switch (t->Kind) {
  case NodeKind::NamedType: {
    const auto *n = cast<NamedTypeRepr>(t);
    auto c = alloc<NamedTypeRepr>(t);
    c->Path = n->Path;
    c->NameRange = n->NameRange;
    c->GenericArgs = cloneTypeList(n->GenericArgs);
    return c;
  }
  case NodeKind::PointerType: {
    const auto *p = cast<PointerTypeRepr>(t);
    auto c = alloc<PointerTypeRepr>(t);
    c->Pointee = cloneTypeRepr(p->Pointee.get());
    c->IsMutable = p->IsMutable;
    c->IsRaw = p->IsRaw;
    c->IsWeak = p->IsWeak;
    return c;
  }
  case NodeKind::ArrayType: {
    const auto *a = cast<ArrayTypeRepr>(t);
    auto c = alloc<ArrayTypeRepr>(t);
    c->Element = cloneTypeRepr(a->Element.get());
    c->Size = cloneExpr(a->Size.get());
    return c;
  }
  case NodeKind::SliceType: {
    auto c = alloc<SliceTypeRepr>(t);
    c->Element = cloneTypeRepr(cast<SliceTypeRepr>(t)->Element.get());
    return c;
  }
  case NodeKind::TupleType: {
    auto c = alloc<TupleTypeRepr>(t);
    c->Elements = cloneTypeList(cast<TupleTypeRepr>(t)->Elements);
    return c;
  }
  case NodeKind::FunctionTypeRepr: {
    const auto *f = cast<FunctionTypeReprNode>(t);
    auto c = alloc<FunctionTypeReprNode>(t);
    c->Params = cloneTypeList(f->Params);
    c->ParamNames = f->ParamNames;
    c->ReturnType = cloneTypeRepr(f->ReturnType.get());
    return c;
  }
  case NodeKind::OptionalType: {
    auto c = alloc<OptionalTypeRepr>(t);
    c->Element = cloneTypeRepr(cast<OptionalTypeRepr>(t)->Element.get());
    return c;
  }
  case NodeKind::UniqType: {
    auto c = alloc<UniqTypeRepr>(t);
    c->Element = cloneTypeRepr(cast<UniqTypeRepr>(t)->Element.get());
    return c;
  }
  case NodeKind::SomeType: {
    auto c = alloc<SomeTypeRepr>(t);
    c->MarkType = cloneTypeRepr(cast<SomeTypeRepr>(t)->MarkType.get());
    return c;
  }
  case NodeKind::DynType: {
    auto c = alloc<DynTypeRepr>(t);
    c->MarkType = cloneTypeRepr(cast<DynTypeRepr>(t)->MarkType.get());
    return c;
  }
  case NodeKind::TypeOfType: {
    auto c = alloc<TypeOfRepr>(t);
    c->Operand = cloneExpr(cast<TypeOfRepr>(t)->Operand.get());
    return c;
  }
  case NodeKind::SelfType:
    return alloc<SelfTypeRepr>(t);
  case NodeKind::InferType:
  default:
    return alloc<InferTypeRepr>(t);
  }
}

PatternPtr clonePattern(const Pattern *p) {
  if (!p)
    return nullptr;
  switch (p->Kind) {
  case NodeKind::WildcardPat:
    return alloc<WildcardPattern>(p);
  case NodeKind::BindingPat: {
    const auto *b = cast<BindingPattern>(p);
    auto c = alloc<BindingPattern>(p);
    c->Name = b->Name;
    c->IsMutable = b->IsMutable;
    c->Takes = b->Takes;
    c->TakeShort = b->TakeShort;
    c->MustBeVariant = b->MustBeVariant;
    c->Sub = clonePattern(b->Sub.get());
    return c;
  }
  case NodeKind::LiteralPat: {
    auto c = alloc<LiteralPattern>(p);
    c->Value = cloneExpr(cast<LiteralPattern>(p)->Value.get());
    return c;
  }
  case NodeKind::TuplePat: {
    auto c = alloc<TuplePattern>(p);
    for (const auto &e : cast<TuplePattern>(p)->Elements)
      c->Elements.push_back(clonePattern(e.get()));
    return c;
  }
  case NodeKind::StructPat: {
    const auto *s = cast<StructPattern>(p);
    auto c = alloc<StructPattern>(p);
    c->Path = s->Path;
    c->HasRest = s->HasRest;
    for (const auto &f : s->Fields) {
      StructPatternField cf;
      cf.Name = f.Name;
      cf.Range = f.Range;
      cf.Value = clonePattern(f.Value.get());
      c->Fields.push_back(std::move(cf));
    }
    return c;
  }
  case NodeKind::EnumPat: {
    const auto *e = cast<EnumPattern>(p);
    auto c = alloc<EnumPattern>(p);
    c->Path = e->Path;
    for (const auto &el : e->Elements)
      c->Elements.push_back(clonePattern(el.get()));
    return c;
  }
  case NodeKind::PathPat: {
    auto c = alloc<PathPattern>(p);
    c->Path = cast<PathPattern>(p)->Path;
    return c;
  }
  case NodeKind::RangePat: {
    const auto *r = cast<RangePattern>(p);
    auto c = alloc<RangePattern>(p);
    c->Lo = cloneExpr(r->Lo.get());
    c->Hi = cloneExpr(r->Hi.get());
    c->Inclusive = r->Inclusive;
    return c;
  }
  case NodeKind::OrPat: {
    auto c = alloc<OrPattern>(p);
    for (const auto &a : cast<OrPattern>(p)->Alternatives)
      c->Alternatives.push_back(clonePattern(a.get()));
    return c;
  }
  case NodeKind::SlicePat: {
    const auto *s = cast<SlicePattern>(p);
    auto c = alloc<SlicePattern>(p);
    for (const auto &e : s->Prefix)
      c->Prefix.push_back(clonePattern(e.get()));
    c->HasRest = s->HasRest;
    c->Rest = clonePattern(s->Rest.get());
    for (const auto &e : s->Suffix)
      c->Suffix.push_back(clonePattern(e.get()));
    return c;
  }
  case NodeKind::RefPat: {
    const auto *r = cast<RefPattern>(p);
    auto c = alloc<RefPattern>(p);
    c->Sub = clonePattern(r->Sub.get());
    c->IsMutable = r->IsMutable;
    return c;
  }
  default:
    return alloc<WildcardPattern>(p);
  }
}

std::unique_ptr<BlockExpr> cloneBlock(const BlockExpr *b) {
  if (!b)
    return nullptr;
  auto c = alloc<BlockExpr>(b);
  for (const auto &s : b->Stmts)
    c->Stmts.push_back(cloneStmt(s.get()));
  c->Tail = cloneExpr(b->Tail.get());
  return c;
}

ExprPtr cloneExpr(const Expr *e) {
  if (!e)
    return nullptr;
  switch (e->Kind) {
  case NodeKind::IntLit: {
    const auto *l = cast<IntLitExpr>(e);
    auto c = alloc<IntLitExpr>(e);
    c->Value = l->Value;
    c->Suffix = l->Suffix;
    c->IsNegated = l->IsNegated;
    return c;
  }
  case NodeKind::FloatLit: {
    const auto *l = cast<FloatLitExpr>(e);
    auto c = alloc<FloatLitExpr>(e);
    c->Value = l->Value;
    c->Suffix = l->Suffix;
    return c;
  }
  case NodeKind::StringLit: {
    auto c = alloc<StringLitExpr>(e);
    c->Value = cast<StringLitExpr>(e)->Value;
    return c;
  }
  case NodeKind::CharLit: {
    auto c = alloc<CharLitExpr>(e);
    c->Value = cast<CharLitExpr>(e)->Value;
    return c;
  }
  case NodeKind::BoolLit: {
    auto c = alloc<BoolLitExpr>(e);
    c->Value = cast<BoolLitExpr>(e)->Value;
    return c;
  }
  case NodeKind::NilLit:
    return alloc<NilLitExpr>(e);
  case NodeKind::ArrayLit: {
    const auto *a = cast<ArrayLitExpr>(e);
    auto c = alloc<ArrayLitExpr>(e);
    for (const auto &el : a->Elements)
      c->Elements.push_back(cloneExpr(el.get()));
    c->RepeatCount = cloneExpr(a->RepeatCount.get());
    return c;
  }
  case NodeKind::TupleLit: {
    auto c = alloc<TupleLitExpr>(e);
    for (const auto &el : cast<TupleLitExpr>(e)->Elements)
      c->Elements.push_back(cloneExpr(el.get()));
    return c;
  }
  case NodeKind::StructLit: {
    const auto *s = cast<StructLitExpr>(e);
    auto c = alloc<StructLitExpr>(e);
    c->Path = s->Path;
    c->PathRange = s->PathRange;
    c->GenericArgs = cloneTypeList(s->GenericArgs);
    for (const auto &f : s->Fields) {
      StructLitField cf;
      cf.Name = f.Name;
      cf.Range = f.Range;
      cf.Value = cloneExpr(f.Value.get());
      c->Fields.push_back(std::move(cf));
    }
    c->Base = cloneExpr(s->Base.get());
    return c;
  }
  case NodeKind::DeclRef: {
    const auto *r = cast<DeclRefExpr>(e);
    auto c = alloc<DeclRefExpr>(e);
    c->Path = r->Path;
    c->GenericArgs = cloneTypeList(r->GenericArgs);
    c->FromInferredType = r->FromInferredType;
    return c;
  }
  case NodeKind::Unary: {
    const auto *u = cast<UnaryExpr>(e);
    auto c = alloc<UnaryExpr>(e);
    c->Op = u->Op;
    c->OpRange = u->OpRange;
    c->Operand = cloneExpr(u->Operand.get());
    return c;
  }
  case NodeKind::Binary: {
    const auto *b = cast<BinaryExpr>(e);
    auto c = alloc<BinaryExpr>(e);
    c->Op = b->Op;
    c->OpRange = b->OpRange;
    c->LHS = cloneExpr(b->LHS.get());
    c->RHS = cloneExpr(b->RHS.get());
    return c;
  }
  case NodeKind::Assign: {
    const auto *a = cast<AssignExpr>(e);
    auto c = alloc<AssignExpr>(e);
    c->Op = a->Op;
    c->OpRange = a->OpRange;
    c->LHS = cloneExpr(a->LHS.get());
    c->RHS = cloneExpr(a->RHS.get());
    return c;
  }
  case NodeKind::Call: {
    const auto *k = cast<CallExpr>(e);
    auto c = alloc<CallExpr>(e);
    c->Callee = cloneExpr(k->Callee.get());
    c->ParenRange = k->ParenRange;
    c->IsMethodCall = k->IsMethodCall;
    c->BuilderSeed = k->BuilderSeed;
    c->PointeeAccess = k->PointeeAccess;
    for (const auto &a : k->Args) {
      Argument ca;
      ca.Label = a.Label;
      ca.LabelRange = a.LabelRange;
      ca.Value = cloneExpr(a.Value.get());
      c->Args.push_back(std::move(ca));
    }
    return c;
  }
  case NodeKind::Member: {
    const auto *m = cast<MemberExpr>(e);
    auto c = alloc<MemberExpr>(e);
    c->Base = cloneExpr(m->Base.get());
    c->Name = m->Name;
    c->NameRange = m->NameRange;
    c->IsTupleIndex = m->IsTupleIndex;
    c->TupleIndex = m->TupleIndex;
    c->GenericArgs = cloneTypeList(m->GenericArgs);
    c->QualifiedMark = m->QualifiedMark;
    c->IsIntrinsic = m->IsIntrinsic;
    c->IsAwait = m->IsAwait;
    return c;
  }
  case NodeKind::Index: {
    const auto *i = cast<IndexExpr>(e);
    auto c = alloc<IndexExpr>(e);
    c->ThroughRawPointer = i->ThroughRawPointer;
    c->StringChar = i->StringChar;
    c->Base = cloneExpr(i->Base.get());
    c->Index = cloneExpr(i->Index.get());
    c->BracketRange = i->BracketRange;
    return c;
  }
  case NodeKind::Into: {
    const auto *k = cast<IntoExpr>(e);
    auto c = alloc<IntoExpr>(e);
    c->Operand = cloneExpr(k->Operand.get());
    c->TargetType = cloneTypeRepr(k->TargetType.get());
    c->Conversion = k->Conversion;
    return c;
  }
  case NodeKind::Cast: {
    const auto *k = cast<CastExpr>(e);
    auto c = alloc<CastExpr>(e);
    c->Operand = cloneExpr(k->Operand.get());
    c->TargetType = cloneTypeRepr(k->TargetType.get());
    c->IsFunctionAddress = k->IsFunctionAddress;
    return c;
  }
  case NodeKind::TypeTest: {
    const auto *k = cast<TypeTestExpr>(e);
    auto c = alloc<TypeTestExpr>(e);
    c->Operand = cloneExpr(k->Operand.get());
    c->TargetType = cloneTypeRepr(k->TargetType.get());
    return c;
  }
  case NodeKind::Closure: {
    const auto *k = cast<ClosureExpr>(e);
    auto c = alloc<ClosureExpr>(e);
    for (const Param &p : k->Params)
      c->Params.push_back(cloneParam(p));
    c->ReturnType = cloneTypeRepr(k->ReturnType.get());
    c->Body = cloneBlock(k->Body.get());
    c->IsMove = k->IsMove;
    c->IsAsyncBody = k->IsAsyncBody;
    return c;
  }
  case NodeKind::Block:
    return cloneBlock(cast<BlockExpr>(e));
  case NodeKind::If: {
    const auto *i = cast<IfExpr>(e);
    auto c = alloc<IfExpr>(e);
    c->Cond = cloneExpr(i->Cond.get());
    c->BindingPat = clonePattern(i->BindingPat.get());
    c->Then = cloneBlock(i->Then.get());
    c->Else = cloneExpr(i->Else.get());
    return c;
  }
  case NodeKind::While: {
    const auto *w = cast<WhileExpr>(e);
    auto c = alloc<WhileExpr>(e);
    c->Cond = cloneExpr(w->Cond.get());
    c->BindingPat = clonePattern(w->BindingPat.get());
    c->Body = cloneBlock(w->Body.get());
    c->Label = w->Label;
    return c;
  }
  case NodeKind::Loop: {
    const auto *l = cast<LoopExpr>(e);
    auto c = alloc<LoopExpr>(e);
    c->Body = cloneBlock(l->Body.get());
    c->Label = l->Label;
    return c;
  }
  case NodeKind::For: {
    const auto *f = cast<ForExpr>(e);
    auto c = alloc<ForExpr>(e);
    c->Binding = clonePattern(f->Binding.get());
    c->Sequence = cloneExpr(f->Sequence.get());
    c->Body = cloneBlock(f->Body.get());
    c->Label = f->Label;
    return c;
  }
  case NodeKind::Match: {
    const auto *m = cast<MatchExpr>(e);
    auto c = alloc<MatchExpr>(e);
    c->Scrutinee = cloneExpr(m->Scrutinee.get());
    for (const auto &a : m->Arms) {
      MatchArm ca;
      ca.Range = a.Range;
      ca.Pat = clonePattern(a.Pat.get());
      ca.Guard = cloneExpr(a.Guard.get());
      ca.Body = cloneExpr(a.Body.get());
      c->Arms.push_back(std::move(ca));
    }
    return c;
  }
  case NodeKind::Return: {
    auto c = alloc<ReturnExpr>(e);
    c->Value = cloneExpr(cast<ReturnExpr>(e)->Value.get());
    return c;
  }
  case NodeKind::Break: {
    const auto *b = cast<BreakExpr>(e);
    auto c = alloc<BreakExpr>(e);
    c->Value = cloneExpr(b->Value.get());
    c->Label = b->Label;
    return c;
  }
  case NodeKind::Continue: {
    auto c = alloc<ContinueExpr>(e);
    c->Label = cast<ContinueExpr>(e)->Label;
    return c;
  }
  case NodeKind::Borrow: {
    const auto *b = cast<BorrowExpr>(e);
    auto c = alloc<BorrowExpr>(e);
    c->Operand = cloneExpr(b->Operand.get());
    c->IsMutable = b->IsMutable;
    return c;
  }
  case NodeKind::Deref: {
    const auto *d = cast<DerefExpr>(e);
    auto c = alloc<DerefExpr>(e);
    c->Operand = cloneExpr(d->Operand.get());
    c->OverloadResolved = d->OverloadResolved;
    return c;
  }
  case NodeKind::Range: {
    const auto *r = cast<RangeExpr>(e);
    auto c = alloc<RangeExpr>(e);
    c->Lo = cloneExpr(r->Lo.get());
    c->Hi = cloneExpr(r->Hi.get());
    c->Inclusive = r->Inclusive;
    return c;
  }
  case NodeKind::SelfRef:
    return alloc<SelfExpr>(e);
  case NodeKind::SuperRef:
    return alloc<SuperExpr>(e);
  case NodeKind::UnsafeBlock: {
    auto c = alloc<UnsafeBlockExpr>(e);
    c->Body = cloneBlock(cast<UnsafeBlockExpr>(e)->Body.get());
    return c;
  }
  case NodeKind::Try: {
    auto c = alloc<TryExpr>(e);
    c->Operand = cloneExpr(cast<TryExpr>(e)->Operand.get());
    return c;
  }
  default:
    return alloc<ErrorExpr>(e);
  }
}

StmtPtr cloneStmt(const Stmt *s) {
  if (!s)
    return nullptr;
  switch (s->Kind) {
  case NodeKind::ExprStmt: {
    auto c = alloc<ExprStmt>(s);
    c->Value = cloneExpr(cast<ExprStmt>(s)->Value.get());
    return c;
  }
  case NodeKind::VarStmt: {
    const auto *v = cast<VarStmtNode>(s);
    auto c = alloc<VarStmtNode>(s);
    c->Binding = clonePattern(v->Binding.get());
    c->TypeAnnotation = cloneTypeRepr(v->TypeAnnotation.get());
    c->Init = cloneExpr(v->Init.get());
    c->IsMutable = v->IsMutable;
    c->IsGlobal = v->IsGlobal;
    c->DeclaresNew = v->DeclaresNew;
    return c;
  }
  case NodeKind::DeclStmtKind: {
    auto c = alloc<DeclStmt>(s);
    c->Inner = cloneDecl(cast<DeclStmt>(s)->Inner.get());
    return c;
  }
  case NodeKind::DeferStmt: {
    auto c = alloc<DeferStmtNode>(s);
    c->Body = cloneExpr(cast<DeferStmtNode>(s)->Body.get());
    return c;
  }
  default:
    return nullptr;
  }
}

std::unique_ptr<FunctionDecl> cloneFunction(const FunctionDecl *f) {
  if (!f)
    return nullptr;
  auto c = alloc<FunctionDecl>(f);
  c->Name = f->Name;
  c->NameRange = f->NameRange;
  c->IsPublic = f->IsPublic;
  c->ModulePath = f->ModulePath;
  c->Parent = f->Parent;
  c->Attrs = cloneAttrs(f->Attrs);
  c->Generics = cloneGenerics(f->Generics);
  c->WhereClauses = cloneWheres(f->WhereClauses);
  for (const Param &p : f->Params)
    c->Params.push_back(cloneParam(p));
  c->ReturnType = cloneTypeRepr(f->ReturnType.get());
  c->Body = cloneBlock(f->Body.get());
  c->Flavour = f->Flavour;
  c->IsUnsafe = f->IsUnsafe;
  c->IsSafeJustified = f->IsSafeJustified;
  c->SafetyReason = f->SafetyReason;
  c->IsZombieTrusted = f->IsZombieTrusted;
  c->ZombieReason = f->ZombieReason;
  c->LinkName = f->LinkName;
  c->IsExtern = f->IsExtern;
  c->ExternABI = f->ExternABI;
  c->CxxScope = f->CxxScope;
  c->CxxOperator = f->CxxOperator;
  c->IsVariadic = f->IsVariadic;
  c->IsVirtual = f->IsVirtual;
  c->IsOverride = f->IsOverride;
  c->IsStatic = f->IsStatic;
  c->IsOperatorImpl = f->IsOperatorImpl;
  c->FromMark = f->FromMark;
  c->Bind = f->Bind;
  c->IsAsync = f->IsAsync;
  c->AsyncResult = cloneTypeRepr(f->AsyncResult.get());
  return c;
}

DeclPtr cloneDecl(const Decl *d) {
  if (!d)
    return nullptr;
  switch (d->Kind) {
  case NodeKind::Function:
    return cloneFunction(cast<FunctionDecl>(d));
  case NodeKind::Struct: {
    const auto *s = cast<StructDecl>(d);
    auto c = alloc<StructDecl>(d);
    c->Name = s->Name;
    c->NameRange = s->NameRange;
    c->IsPublic = s->IsPublic;
    c->ModulePath = s->ModulePath;
    c->Attrs = cloneAttrs(s->Attrs);
    c->Generics = cloneGenerics(s->Generics);
    c->WhereClauses = cloneWheres(s->WhereClauses);
    // The parent's members have already been spliced into `Fields`, so the
    // clone carries them; the link is kept for the conversions that rest on
    // it.
    c->Inherits = cloneTypeRepr(s->Inherits.get());
    c->InheritsDecl = s->InheritsDecl;
    c->InheritanceDone = s->InheritanceDone;
    // An instantiation of a C++ template struct is still C++'s: same scope,
    // same base, mangled as a specialisation of the same template.
    if (s->Cxx)
      c->Cxx = std::make_unique<CxxDeclInfo>(*s->Cxx);
    for (const auto &f : s->Fields) {
      auto cf = cloneField(f.get());
      cf->Parent = c.get();
      c->Fields.push_back(std::move(cf));
    }
    for (const auto &m : s->Methods) {
      auto cm = cloneFunction(m.get());
      cm->Parent = c.get();
      c->Methods.push_back(std::move(cm));
    }
    return c;
  }
  case NodeKind::Mark: {
    // A generic mark is instantiated per argument list, so that
    // `As<Fahrenheit>` and `As<Kelvin>` are different conformances.
    const auto *m = cast<MarkDecl>(d);
    auto c = alloc<MarkDecl>(d);
    c->Name = m->Name;
    c->NameRange = m->NameRange;
    c->IsPublic = m->IsPublic;
    c->ModulePath = m->ModulePath;
    c->Attrs = cloneAttrs(m->Attrs);
    c->Generics = cloneGenerics(m->Generics);
    c->WhereClauses = cloneWheres(m->WhereClauses);
    c->SuperMarks = cloneTypeList(m->SuperMarks);
    c->Supers = m->Supers;
    for (const auto &at : m->AssociatedTypes) {
      auto ca = alloc<AssociatedTypeDecl>(at.get());
      ca->Name = at->Name;
      ca->NameRange = at->NameRange;
      ca->IsPublic = at->IsPublic;
      ca->Bounds = cloneTypeList(at->Bounds);
      ca->Value = cloneTypeRepr(at->Value.get());
      ca->Parent = c.get();
      c->AssociatedTypes.push_back(std::move(ca));
    }
    for (const auto &fn : m->Methods) {
      auto cm = cloneFunction(fn.get());
      cm->Parent = c.get();
      c->Methods.push_back(std::move(cm));
    }
    return c;
  }
  case NodeKind::Enum: {
    const auto *e = cast<EnumDecl>(d);
    auto c = alloc<EnumDecl>(d);
    c->Name = e->Name;
    c->NameRange = e->NameRange;
    c->IsPublic = e->IsPublic;
    c->ModulePath = e->ModulePath;
    c->Attrs = cloneAttrs(e->Attrs);
    c->Generics = cloneGenerics(e->Generics);
    c->IsSimple = e->IsSimple;
    c->Inherits = cloneTypeRepr(e->Inherits.get());
    c->InheritsDecl = e->InheritsDecl;
    c->InheritanceDone = e->InheritanceDone;
    if (e->Cxx)
      c->Cxx = std::make_unique<CxxDeclInfo>(*e->Cxx);
    for (const auto &v : e->Variants) {
      auto cv = alloc<EnumVariantDecl>(v.get());
      cv->Name = v->Name;
      cv->NameRange = v->NameRange;
      cv->Shape = v->Shape;
      cv->Index = v->Index;
      cv->Value = v->Value;
      cv->TupleTypes = cloneTypeList(v->TupleTypes);
      for (const auto &f : v->Fields)
        cv->Fields.push_back(cloneField(f.get()));
      cv->Discriminant = cloneExpr(v->Discriminant.get());
      cv->Parent = c.get();
      c->Variants.push_back(std::move(cv));
    }
    for (const auto &m : e->Methods) {
      auto cm = cloneFunction(m.get());
      cm->Parent = c.get();
      c->Methods.push_back(std::move(cm));
    }
    return c;
  }
  case NodeKind::Class: {
    const auto *k = cast<ClassDecl>(d);
    auto c = alloc<ClassDecl>(d);
    c->Name = k->Name;
    c->NameRange = k->NameRange;
    c->IsPublic = k->IsPublic;
    c->ModulePath = k->ModulePath;
    c->Attrs = cloneAttrs(k->Attrs);
    c->Generics = cloneGenerics(k->Generics);
    c->SuperClass = cloneTypeRepr(k->SuperClass.get());
    for (const auto &f : k->Fields) {
      auto cf = cloneField(f.get());
      cf->Parent = c.get();
      c->Fields.push_back(std::move(cf));
    }
    for (const auto &m : k->Methods) {
      auto cm = cloneFunction(m.get());
      cm->Parent = c.get();
      if (cm->Name == "init") c->Init = cm.get();
      if (cm->Name == "deinit") c->Deinit = cm.get();
      c->Methods.push_back(std::move(cm));
    }
    return c;
  }
  case NodeKind::GlobalVar: {
    const auto *g = cast<GlobalVarDecl>(d);
    auto c = alloc<GlobalVarDecl>(d);
    c->Name = g->Name;
    c->NameRange = g->NameRange;
    c->IsPublic = g->IsPublic;
    c->ModulePath = g->ModulePath;
    c->TypeAnnotation = cloneTypeRepr(g->TypeAnnotation.get());
    c->Init = cloneExpr(g->Init.get());
    c->IsMutable = g->IsMutable;
    return c;
  }
  default:
    // Marks, binds, extends and imports are never cloned: generic
    // instantiation only ever copies functions and data types.
    return nullptr;
  }
}

} // namespace rune
