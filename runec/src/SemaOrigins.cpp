//===- SemaOrigins.cpp - `from` clauses and views ---------------*- C++ -*-===//
//
// The Zombie borrow checker infers where every reference is borrowed from and
// which fields every function touches. What is written down — `-> &V from
// self.map`, `body: &String from self.text`, `&var self { counter }` — is a
// contract the checker verifies rather than information it needs, and this
// file is where those written forms are turned from names into declarations.
//
// A `from` place is always a *place*: a parameter and a field path under it,
// `self.field` inside a type declaration (an internal reference), a binding in
// scope, or `global`. Which of those a name may be depends on where the clause
// was written, which is what `OriginContext` carries.
//
//===----------------------------------------------------------------------===//
#include "rune/Sema.h"

namespace rune {

namespace {

/// Calls `fn` for `t` and every type written inside it, except that a
/// function type is not entered: it names its own parameters and is resolved
/// on its own (see `resolveOriginClauses`).
template <typename F> void forEachRepr(TypeRepr *t, F &&fn) {
  if (!t)
    return;
  fn(t);
  switch (t->Kind) {
  case NodeKind::PointerType:
    forEachRepr(cast<PointerTypeRepr>(t)->Pointee.get(), fn);
    break;
  case NodeKind::ArrayType:
    forEachRepr(cast<ArrayTypeRepr>(t)->Element.get(), fn);
    break;
  case NodeKind::SliceType:
    forEachRepr(cast<SliceTypeRepr>(t)->Element.get(), fn);
    break;
  case NodeKind::OptionalType:
    forEachRepr(cast<OptionalTypeRepr>(t)->Element.get(), fn);
    break;
  case NodeKind::UniqType:
    forEachRepr(cast<UniqTypeRepr>(t)->Element.get(), fn);
    break;
  case NodeKind::TupleType:
    for (auto &e : cast<TupleTypeRepr>(t)->Elements)
      forEachRepr(e.get(), fn);
    break;
  case NodeKind::NamedType:
    for (auto &a : cast<NamedTypeRepr>(t)->GenericArgs)
      forEachRepr(a.get(), fn);
    break;
  default:
    break;
  }
}

std::string joinPath(const std::vector<std::string> &path) {
  std::string s;
  for (size_t i = 0; i < path.size(); ++i) {
    if (i)
      s += ".";
    s += path[i];
  }
  return s;
}

/// The type a field path steps through: borrows and `Unique` are looked
/// through, because `self.map` reaches the map whether `self` is `&self` or
/// `self`.
Type *throughHandles(Type *t) {
  while (t) {
    if (t->is(TypeKind::Pointer) && !t->isRawPointer())
      t = t->pointee();
    else if (t->is(TypeKind::Class) && t->isUniq())
      t = TypeContext::stripUniq(t);
    else
      break;
  }
  return t;
}

} // namespace

FieldDecl *Sema::fieldOf(Type *t, const std::string &name) {
  t = throughHandles(t);
  if (!t || !t->isNominal() || t->isOpaque())
    return nullptr;
  NominalDecl *nd = t->nominal();
  std::vector<NominalDecl *> chain{nd};
  if (auto *c = dyn_cast<ClassDecl>(static_cast<Decl *>(nd)))
    for (ClassDecl *s = c->Super; s; s = s->Super)
      chain.push_back(s);
  for (NominalDecl *n : chain)
    for (const auto &f : n->Fields)
      if (f->Name == name)
        return f.get();
  return nullptr;
}

bool Sema::resolveFieldPath(Type *root, const std::vector<std::string> &names,
                            size_t from, std::vector<unsigned> &out,
                            SourceRange at) {
  Type *t = root;
  for (size_t i = from; i < names.size(); ++i) {
    Type *here = throughHandles(t);
    if (!here || here->isError())
      return false;
    // A tuple field by index.
    if (here->is(TypeKind::Tuple)) {
      const std::string &n = names[i];
      bool digits = !n.empty() &&
                    n.find_first_not_of("0123456789") == std::string::npos;
      size_t idx = digits ? static_cast<size_t>(std::stoul(n)) : ~size_t(0);
      if (idx >= here->tupleElements().size()) {
        Diags.error(at, "'{}' is not an element of '{}'", n, here->toString())
            .code(293);
        return false;
      }
      out.push_back(static_cast<unsigned>(idx));
      t = here->tupleElements()[idx];
      continue;
    }
    FieldDecl *f = fieldOf(here, names[i]);
    if (!f) {
      auto d = Diags.error(at, "'{}' has no field '{}'", here->toString(),
                           names[i]);
      d.note("a `from` place or a view names a parameter, `self`, or "
             "`global`, and then fields under it");
      d.code(293);
      return false;
    }
    out.push_back(f->Index);
    t = f->Ty;
  }
  return true;
}

bool Sema::resolveOriginPlace(OriginPlace &place, const OriginContext &ctx) {
  if (place.Path.empty())
    return false;
  const std::string &head = place.Path.front();

  if (head == "global") {
    if (place.Path.size() != 1) {
      Diags.error(place.Range, "`global` stands alone in a `from` clause")
          .code(293);
      return false;
    }
    place.Root = OriginPlace::RootKind::Global;
    return true;
  }

  // A signature: parameters by name, `self` included.
  if (ctx.Params) {
    for (size_t i = 0; i < ctx.Params->size(); ++i) {
      const Param &p = (*ctx.Params)[i];
      if (p.Name != head)
        continue;
      place.Root = OriginPlace::RootKind::Param;
      place.ParamIndex = static_cast<int>(i);
      return resolveFieldPath(p.Ty, place.Path, 1, place.FieldPath,
                              place.Range);
    }
    auto d = Diags.error(place.Range,
                         "'{}' is not a parameter of this function", head);
    d.note("a result borrows from a parameter, a field under one, or "
           "`global`");
    d.code(293);
    return false;
  }

  // A function type: the names written before its parameter types.
  if (ctx.FnType) {
    const auto &names = ctx.FnType->ParamNames;
    for (size_t i = 0; i < names.size(); ++i) {
      if (names[i] != head)
        continue;
      place.Root = OriginPlace::RootKind::Param;
      place.ParamIndex = static_cast<int>(i);
      Type *pt = i < ctx.FnType->Params.size()
                     ? ctx.FnType->Params[i]->Resolved
                     : nullptr;
      if (place.Path.size() == 1)
        return true;
      if (!pt)
        return false;
      return resolveFieldPath(pt, place.Path, 1, place.FieldPath, place.Range);
    }
    auto d = Diags.error(place.Range,
                         "'{}' is not a parameter of this function type", head);
    d.note("name the parameter in the type — `@function(list: &List) -> "
           "&Item from list`");
    d.code(293);
    return false;
  }

  // A field declaration: `self.field`, an internal reference.
  if (ctx.Self) {
    if (head != "self" || place.Path.size() < 2) {
      auto d = Diags.error(place.Range,
                           "a field borrows from another field of the same "
                           "value: write `from self.<field>`");
      d.note("a struct that borrows from *outside* says nothing here — the "
             "place is named where the value is made or returned");
      d.code(293);
      return false;
    }
    place.Root = OriginPlace::RootKind::SelfField;
    // An enum variant's fields are not reachable from the enum's type, so
    // the first step is looked up by hand and the rest through its type.
    if (ctx.SelfFields) {
      const FieldDecl *first = nullptr;
      for (const auto &f : *ctx.SelfFields)
        if (f->Name == place.Path[1])
          first = f.get();
      if (!first) {
        Diags.error(place.Range, "this variant has no field '{}'",
                    place.Path[1])
            .code(293);
        return false;
      }
      place.FieldPath.push_back(first->Index);
      return resolveFieldPath(first->Ty, place.Path, 2, place.FieldPath,
                              place.Range);
    }
    return resolveFieldPath(ctx.Self->DeclaredType, place.Path, 1,
                            place.FieldPath, place.Range);
  }

  // A local binding's annotation: whatever is in scope.
  if (ctx.Locals) {
    Symbol *sym = CurScope ? CurScope->find(head) : nullptr;
    auto *v = sym && sym->D ? dyn_cast<VarDecl>(sym->D) : nullptr;
    if (!v) {
      Diags.error(place.Range, "'{}' is not a binding in scope", head)
          .code(293);
      return false;
    }
    place.Root = OriginPlace::RootKind::Local;
    place.Local = v;
    return resolveFieldPath(v->Ty, place.Path, 1, place.FieldPath, place.Range);
  }

  Diags.error(place.Range, "a `from` clause cannot be written here").code(293);
  return false;
}

void Sema::resolveOriginClauses(TypeRepr *repr, const OriginContext &ctx) {
  forEachRepr(repr, [&](TypeRepr *t) {
    if (t->Origin)
      for (OriginPlace &place : t->Origin->Places)
        resolveOriginPlace(place, ctx);
    // A function type is a signature of its own: clauses inside it name
    // its parameters, not the enclosing function's.
    if (auto *f = dyn_cast<FunctionTypeReprNode>(t)) {
      OriginContext inner;
      inner.FnType = f;
      for (auto &p : f->Params)
        resolveOriginClauses(p.get(), inner);
      resolveOriginClauses(f->ReturnType.get(), inner);
    }
  });
}

void Sema::resolveSignatureAnnotations(FunctionDecl *fn) {
  if (!fn || fn->OriginsResolved)
    return;
  fn->OriginsResolved = true;

  OriginContext ctx;
  ctx.Params = &fn->Params;

  // `@zombie` without a reason: accepted, but the reason is the point.
  if (fn->IsZombieTrusted && fn->ZombieReason.empty()) {
    if (const Attribute *a = fn->findAttr("zombie")) {
      auto d = Diags.error(a->Range, "@zombie needs a reason, the way @safe "
                                     "does");
      d.note("the borrow checker takes this body on trust; say why that is "
             "sound — `@zombie(\"the task is joined before scope returns\")`");
      d.code(294);
    }
  }

  for (Param &p : fn->Params) {
    if (p.TypeAnnotation)
      resolveOriginClauses(p.TypeAnnotation.get(), ctx);
    if (!p.HasView)
      continue;
    // A view is a promise about what is reached *through* the parameter, so
    // the parameter has to be a borrow — or `self`, which is one in spirit.
    Type *pt = p.Ty;
    bool borrowed = p.IsSelf ? p.SelfByRef
                             : (pt && pt->is(TypeKind::Pointer) &&
                                !pt->isRawPointer());
    if (!borrowed && !(pt && pt->is(TypeKind::Class))) {
      auto d = Diags.error(p.Range, "a view goes on a borrowed parameter");
      d.note("`{ fields }` says which fields are touched through `&self`, "
             "`&var self` or a `&T` parameter; a value passed by value is "
             "owned whole");
      d.code(286);
      continue;
    }
    for (FieldPathRepr &f : p.View)
      resolveFieldPath(pt, f.Path, 0, f.Resolved, f.Range);
  }
  if (fn->ReturnType)
    resolveOriginClauses(fn->ReturnType.get(), ctx);
}

void Sema::resolveFieldAnnotations(NominalDecl *nd) {
  if (!nd)
    return;
  OriginContext ctx;
  ctx.Self = nd;

  auto check = [&](FieldDecl *f, const std::vector<std::unique_ptr<FieldDecl>>
                                     &siblings, bool variant) {
    if (!f->TypeAnnotation)
      return;
    ctx.SelfFields = variant ? &siblings : nullptr;
    forEachRepr(f->TypeAnnotation.get(), [&](TypeRepr *t) {
      if (!t->Origin)
        return;
      for (OriginPlace &place : t->Origin->Places) {
        if (!resolveOriginPlace(place, ctx))
          continue;
        if (place.Root != OriginPlace::RootKind::SelfField ||
            place.FieldPath.empty())
          continue;
        // The target has to own a heap object: that is what stays put when
        // the value holding both of them moves.
        FieldDecl *target = nullptr;
        for (const auto &sf : siblings)
          if (sf->Index == place.FieldPath.front())
            target = sf.get();
        if (!target)
          continue;
        Type *tt = target->Ty ? TypeContext::stripUniq(target->Ty) : nullptr;
        if (!tt || !tt->isHeapHandle()) {
          auto d = Diags.error(place.Range,
                               "'{}' cannot borrow from '{}': it is stored "
                               "inline and moves with the value",
                               f->Name, joinPath(place.Path));
          d.note("a field can only borrow from another field that owns a "
                 "heap object — a class, a `String`, a `Vector`");
          d.related(target->Range, "declared here",
                    "own it through a class, or keep an index");
          d.code(296);
          continue;
        }
        // Fields are destroyed in reverse declaration order, and the
        // reference has to go before what it points at.
        if (target->Index >= f->Index) {
          auto d = Diags.error(f->Range,
                               "'{}' borrows from '{}', so it must be "
                               "declared after it",
                               f->Name, target->Name);
          d.note("fields are destroyed in reverse declaration order, and "
                 "the reference has to go first");
          d.code(298);
        }
        f->InternalRef = true;
      }
    });
  };

  for (auto &f : nd->Fields)
    check(f.get(), nd->Fields, /*variant=*/false);
  if (auto *e = dyn_cast<EnumDecl>(static_cast<Decl *>(nd)))
    for (auto &v : e->Variants)
      for (auto &f : v->Fields)
        check(f.get(), v->Fields, /*variant=*/true);
}

void Sema::resolveLocalAnnotations(TypeRepr *repr) {
  if (!repr)
    return;
  OriginContext ctx;
  ctx.Locals = true;
  resolveOriginClauses(repr, ctx);
}

} // namespace rune
