//===- Sema.cpp - Name resolution and type checking ------------*- C++ -*-===//

#include "rune/Sema.h"

#include "rune/ASTWalk.h"
#include "rune/Ownership.h"
#include "rune/Zombie.h"

#include <chrono>
#include "rune/Parallel.h"

#include <set>

#include "rune/ASTClone.h"

#include <algorithm>
#include <functional>
#include <ostream>

namespace rune {

/// Structural type equality; defined with the conformance checks below, and
/// needed before them by the method tables.
static bool sameType(Type *a, Type *b);

//===----------------------------------------------------------------------===//
// Construction and scopes
//===----------------------------------------------------------------------===//

Sema::Sema(const SourceManager &sm, DiagnosticEngine &diags, TypeContext &types,
           SafetyLevel safety, MemoryMode memory, DumpKind dump,
           bool zombieStdlib)
    : SM(sm), Diags(diags), Types(types), Safety(safety), Memory(memory),
      Dump(dump), ZombieStdlib(zombieStdlib) {}

Sema::~Sema() = default;

Scope *Sema::pushScope(ScopeKind k) {
  ScopeStorage.push_back(std::make_unique<Scope>(k, CurScope));
  CurScope = ScopeStorage.back().get();
  return CurScope;
}

void Sema::popScope() {
  if (CurScope)
    CurScope = CurScope->parent();
}

Scope *Sema::scopeForModule(Module *m) {
  auto it = ModuleScopes.find(m);
  return it == ModuleScopes.end() ? nullptr : it->second;
}

void Sema::addModule(Module *m) {
  Modules.push_back(m);
  ModulesByName[m->Name] = m;
  for (const std::string &alias : m->Aliases)
    ModulesByName[alias] = m;
  // The standard library and an imported `.rul` are read for what they
  // declare; they are not what this compile is producing. CodeGen needs to
  // know which is which so it can leave out the parts nothing reaches.
  if (m->IsStdlib || m->FromLibrary) {
    Result.AncillaryModules.insert(m->Name);
    for (const std::string &alias : m->Aliases)
      Result.AncillaryModules.insert(alias);
  }
}

void Sema::noteDeclaredAt(DiagBuilder &b, const Decl *d,
                          const std::string &label, const std::string &hint) {
  if (d && d->NameRange.isValid())
    b.related(d->NameRange, label, hint);
}

/// How an operator method's name reads back as punctuation, for diagnostics.
static std::string assignOrOperatorSpelling(const std::string &op) {
  static const std::map<std::string, std::string> kSpelling = {
      {"add", "+"},   {"sub", "-"},   {"mul", "*"},    {"div", "/"},
      {"rem", "%"},   {"eq", "=="},   {"cmp", "<"},    {"bitand", "&"},
      {"bitor", "|"}, {"bitxor", "^"}, {"shl", "<<"},  {"shr", ">>"},
  };
  auto it = kSpelling.find(op);
  return it == kSpelling.end() ? op : it->second;
}

std::string Sema::canonicalOperatorName(const std::string &raw) {
  // The language allows both word spellings and bracket names; normalise so
  // `operator::LeftSquareBracket` and `operator::index` mean the same thing.
  static const std::map<std::string, std::string> kAliases = {
      {"LeftSquareBracket", "index"},   {"RightSquareBracket", "index"},
      {"Subscript", "index"},           {"LeftParen", "call"},
      {"Plus", "add"},                  {"Minus", "sub"},
      {"Star", "mul"},                  {"Slash", "div"},
      {"Percent", "rem"},               {"EqualEqual", "eq"},
      {"Equals", "eq"},                 {"LessThan", "cmp"},
      {"GreaterThan", "cmp"},           {"Compare", "cmp"},
      {"Ampersand", "bitand"},          {"Pipe", "bitor"},
      {"Caret", "bitxor"},              {"ShiftLeft", "shl"},
      {"ShiftRight", "shr"},            {"Negate", "neg"},
      {"Bang", "not"},                  {"Tilde", "bitnot"},
      {"Deref", "deref"},              {"Asterisk", "deref"},
      {"DerefSet", "deref_set"},       {"derefSet", "deref_set"},
      {"AsteriskEquals", "deref_set"}, {"*=", "deref_set"},
      // Punctuation spellings: `bind operator::"*" to T`. `*` names both
      // multiplication and dereference, so the method inside decides which —
      // `fn mul(&self, rhs: T)` or `fn deref(&self)`.
      {"+", "add"},                    {"-", "sub"},
      {"*", "mul"},                    {"/", "div"},
      {"%", "rem"},                    {"==", "eq"},
      {"!=", "eq"},                    {"<", "cmp"},
      {">", "cmp"},                    {"<=", "cmp"},
      {">=", "cmp"},                   {"&", "bitand"},
      {"|", "bitor"},                  {"^", "bitxor"},
      {"<<", "shl"},                   {">>", "shr"},
      {"!", "not"},                    {"~", "bitnot"},
      {"[]", "index"},                 {"()", "call"},
      {"IndexSet", "index_set"},       {"indexSet", "index_set"},
      {"SubscriptSet", "index_set"},
      {"*self", "deref"},              {"unary*", "deref"},
  };
  auto it = kAliases.find(raw);
  if (it != kAliases.end())
    return it->second;
  // A name registered with `@alias("...")` somewhere in the program.
  auto user = OperatorAliases.find(raw);
  return user == OperatorAliases.end() ? raw : user->second;
}

void Sema::bindTemplateGenerics(const NominalDecl *tmpl,
                                const std::vector<Type *> &args,
                                std::map<std::string, Type *> &out) {
  if (!tmpl)
    return;
  for (size_t i = 0; i < tmpl->Generics.size() && i < args.size(); ++i) {
    out[tmpl->Generics[i].Name] = args[i];
    // `extend<A> List<A>`: the methods folded in from that block call this
    // parameter `A`, and both names have to mean the argument.
    for (const std::string &alias : tmpl->Generics[i].Aliases)
      out[alias] = args[i];
  }
}

void Sema::bindOwnerGenerics(const FunctionDecl *fn,
                             std::map<std::string, Type *> &out) {
  auto *nd = fn->Parent ? dyn_cast<NominalDecl>(fn->Parent) : nullptr;
  if (!nd || !nd->GenericTemplate)
    return;
  bindTemplateGenerics(nd->GenericTemplate, nd->TypeArguments, out);
}

Type *Sema::selfTypeFor(const Param &p, Type *base) {
  if (!base)
    return Types.errorType();
  if (!p.SelfByRef || base->isPointerLike())
    return base;
  // Only types with an interesting interior are worth borrowing.
  bool borrowable = base->isNominal() || base->is(TypeKind::Array) ||
                    base->is(TypeKind::Tuple) || base->is(TypeKind::Slice);
  if (!borrowable) {
    if (p.SelfMutable && !base->isError())
      Diags.error(p.Range,
                  "`&var self` needs a type that can be borrowed — '{}' is "
                  "passed by value",
                  base->toString())
          .note("drop the `&var` and return the updated value instead")
          .code(265);
    return base;
  }
  return Types.pointerTo(base, p.SelfMutable, /*raw=*/false);
}

namespace {
/// The cache key for one instantiation of `tmpl` with `args`.
///
/// Every `Type *` is interned, so its address *is* its identity: two
/// arguments share one only when they are the same type. Spelling them
/// instead would put `prog::Item` and `other::Item` under the same key, and
/// the second instantiation asked for would be handed the first.
std::string instantiationKey(const Decl *tmpl, const std::vector<Type *> &args) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%p", static_cast<const void *>(tmpl));
  std::string key = buf;
  for (const Type *a : args) {
    snprintf(buf, sizeof(buf), "%p", static_cast<const void *>(a));
    key += "|";
    key += buf;
  }
  return key;
}
} // namespace

namespace {
/// How a type is written into a symbol name.
///
/// `toString` is written for diagnostics, where `Item` is the clearest thing
/// to say; two modules may each declare one, and their symbols have to differ.
/// `runtimeTypeName` qualifies a nominal type by its module, which is exactly
/// the distinction wanted here — `Unique<T>` aside, which it does not spell
/// out because `Any` has no use for the difference.
std::string mangleTypeName(const Type *t) {
  if (!t)
    return "?";
  // A `some` is a type of its own, distinct from the one behind it, so a
  // generic instantiated over it is a different instantiation from one over
  // the concrete type — and the symbol has to say so. The owner's own symbol
  // is what makes it unique, and the same across every compilation that
  // sees the function.
  if (t->isOpaque()) {
    const FunctionDecl *owner = t->opaqueOwner();
    std::string of = owner ? (owner->MangledName.empty() ? owner->Name
                                                         : owner->MangledName)
                           : std::string("?");
    return "some<" + of + ">";
  }
  if (t->isUniq())
    return "Unique<" + runtimeTypeName(t) + ">";
  return runtimeTypeName(t);
}
} // namespace

//===----------------------------------------------------------------------===//
// extern "C++"
//===----------------------------------------------------------------------===//

namespace {
/// Answers the mangler's one question about names: is this written type an
/// alias, and of what. Looked up in the scope the signature is being resolved
/// in, quietly — an unknown name has already been reported by `resolveType`.
class SemaAliasResolver : public CxxNameResolver {
public:
  SemaAliasResolver(std::function<Symbol *(const std::vector<std::string> &)> f)
      : Lookup(std::move(f)) {}
  const TypeRepr *aliasTarget(const NamedTypeRepr *n) override {
    Symbol *sym = Lookup(n->Path);
    if (!sym || !sym->D)
      return nullptr;
    if (const auto *alias = dyn_cast<TypeAliasDecl>(sym->D))
      return alias->Aliased.get();
    return nullptr;
  }

private:
  std::function<Symbol *(const std::vector<std::string> &)> Lookup;
};
} // namespace

std::string Sema::mangleCxx(const FunctionDecl *fn) {
  if (!Cxx.Itanium) {
    Diags.error(fn->NameRange.isValid() ? fn->NameRange : fn->Range,
                "`extern \"C++\"` is not available for an MSVC target")
        .note("this compiler speaks the Itanium C++ ABI, which is what "
              "Clang and GCC use everywhere but Microsoft's own toolchain; "
              "target `-windows-gnu` (MinGW) instead")
        .code(520);
    return fn->LinkName.empty() ? fn->Name : fn->LinkName;
  }
  SemaAliasResolver resolver([&](const std::vector<std::string> &path) {
    return lookupPath(path, SourceRange(), /*quiet=*/true);
  });
  CxxMangler mangler(Cxx, resolver);
  std::string symbol = mangler.mangleFunction(fn);
  if (mangler.failed()) {
    SourceRange at = mangler.problemRange().isValid()
                         ? mangler.problemRange()
                         : (fn->NameRange.isValid() ? fn->NameRange : fn->Range);
    auto d = Diags.error(at, "{}", mangler.problem());
    d.note("a C++ signature is spelled in C++'s types: scalars, `c_` names, "
           "pointers, references, and the classes, structs and enums the "
           "`extern \"C++\"` block declares");
    if (!mangler.problemHint().empty())
      d.note("{}", mangler.problemHint());
    d.code(521);
  }
  return symbol;
}

std::vector<NominalDecl *> Sema::cxxBasesOf(NominalDecl *nd) {
  std::vector<NominalDecl *> out;
  for (NominalDecl *b = nd && nd->Cxx ? nd->Cxx->BaseDecl : nullptr;
       b && out.size() < 64; b = b->Cxx ? b->Cxx->BaseDecl : nullptr)
    out.push_back(b);
  return out;
}

void Sema::resolveCxxType(NominalDecl *nd) {
  if (!nd->Cxx)
    return;
  UsesCxx = true;
  CxxDeclInfo &info = *nd->Cxx;
  if (!info.BaseName.empty() && !info.BaseDecl) {
    Symbol *sym = lookupPath({info.BaseName}, info.BaseRange, /*quiet=*/true);
    auto *base = sym && sym->D ? dyn_cast<NominalDecl>(sym->D) : nullptr;
    if (!base || !base->Cxx || !base->Cxx->IsClass) {
      auto d = Diags.error(info.BaseRange,
                           "'{}' is not a C++ class declared in an "
                           "`extern \"C++\"` block",
                           info.BaseName);
      d.note("a base has to be a `class` from the same or another "
             "`extern \"C++\"` block, so the compiler knows a pointer to the "
             "derived class is also one to the base")
          .code(522);
      if (base)
        noteDeclaredAt(d, base, "declared here", "this is not a C++ class");
    } else if (base == nd) {
      Diags.error(info.BaseRange, "'{}' cannot be its own base",
                  static_cast<Decl *>(nd)->Name)
          .code(522);
    } else {
      info.BaseDecl = base;
    }
  }
  if (!info.IsClass && info.Size) {
    Diags.error(static_cast<Decl *>(nd)->NameRange,
                "`@size` belongs on a `class`, whose layout Rune does not "
                "know")
        .note("a `struct` is laid out from its fields; its size follows")
        .code(523);
  }
  if (!info.IsClass && !info.BaseName.empty()) {
    Diags.error(info.BaseRange, "only a `class` may name a base")
        .note("a struct that begins with another struct's fields declares "
              "them; C++ passes both the same way")
        .code(522);
  }
  // A destructor takes nothing; a constructor returns nothing.
  for (auto &m : nd->Methods) {
    bool hasSelf = false;
    for (const Param &p : m->Params)
      hasSelf = hasSelf || p.IsSelf;
    if ((isCxxConstructor(m.get()) || isCxxDestructor(m.get())) && !hasSelf) {
      Diags.error(m->NameRange, "`{}` takes `&var self` — it runs on an "
                                "object that already has storage",
                  m->LinkName.empty() ? m->Name : m->LinkName)
          .note("allocate with `cxx::alloc<T>()`, then call `init` on the "
                "pointer; `deinit` and `cxx::free` undo the two")
          .code(524);
    }
    if (isCxxConstructor(m.get()) && m->ReturnType) {
      Diags.error(m->ReturnType->Range, "a constructor returns nothing")
          .code(524);
    }
    if (isCxxDestructor(m.get())) {
      size_t params = 0;
      for (const Param &p : m->Params)
        if (!p.IsSelf)
          ++params;
      if (params || m->ReturnType)
        Diags.error(m->NameRange, "a destructor takes only `&var self` and "
                                  "returns nothing")
            .code(524);
    }
    if (!m->CxxOperator.empty() && !cxxOwnerOf(m.get())) {
      Diags.error(m->NameRange, "`@operator` names a member of a C++ class")
          .code(524);
    }
  }
}

bool Sema::cxxPointerConvertible(Type *from, Type *to) {
  if (!from || !to || !from->is(TypeKind::Pointer) || !to->is(TypeKind::Pointer))
    return false;
  if (from->isWeakPointer() || to->isWeakPointer())
    return false;
  // A shared borrow or a read-only pointer cannot become a mutable one.
  if (to->isMutablePointer() && !from->isMutablePointer())
    return false;
  // A borrow is checked; a raw pointer is not. Handing a borrow where C++
  // wrote `T*` is fine — it is an address either way — and handing a raw
  // pointer where C++ wrote `T&` is what a call from C++ would do too.
  Type *f = from->pointee();
  Type *t = to->pointee();
  if (!f || !t)
    return false;
  f = f->canonical();
  t = t->canonical();
  if (f == t)
    return true;
  if (!f->isNominal() || !t->isNominal())
    return false;
  for (NominalDecl *base : cxxBasesOf(f->nominal()))
    if (base->DeclaredType == t)
      return true;
  return false;
}

void Sema::rejectCxxClassByValue(Type *t, SourceRange where, const char *role) {
  if (!t)
    return;
  t = t->canonical();
  if (!t->isNominal())
    return;
  NominalDecl *nd = t->nominal();
  if (!nd->Cxx || !nd->Cxx->IsClass)
    return;
  Diags.error(where, "a C++ class is never held by value — '{}' is one",
              t->toString())
      .note("Rune does not know how to copy, move or destroy a C++ object; "
            "keep it behind a pointer — `*var {}` for {} — and let C++ do "
            "those",
            t->toString(), role)
      .code(525);
}

/// A mark names a *requirement*, not a value: many types carry it, each a
/// different size, so there is no such thing as "a Node" to hold. The two
/// ways to hold one are `dyn Node`, which carries the type along at run
/// time, and a type parameter bounded by it, which fixes the type at each
/// call. Saying so where the type is written keeps the mistake from
/// surfacing later as a missing `$clone` somewhere inside a container.
void Sema::rejectMarkByValue(Type *t, SourceRange where, const char *role) {
  if (!t || !t->is(TypeKind::Mark))
    return;
  // `Self` inside a mark *is* the mark, and there it means "whichever type
  // carries this" — a requirement's own signature is the one place a mark
  // legitimately stands where a value's type goes.
  if (t == ActiveSelfType)
    return;
  const std::string name = t->toString();
  auto d = Diags.error(where, "'{}' is a mark, not a type — {} cannot be one",
                       name, role);
  d.note(fmt("a mark says what a type can do; every type that carries it is "
             "a different size, so there is no one value to hold")
             .c_str());
  d.note(fmt("write `dyn {}` to hold any of them, which carries the type "
             "along at run time", name)
             .c_str());
  d.note(fmt("or make it a bound — `<T: {}>` — to fix one type per call, "
             "with no boxing at all", name)
             .c_str());
  d.code(219);
}

std::string Sema::mangleFunction(const FunctionDecl *fn,
                                 const std::vector<Type *> &typeArgs) {
  // A foreign function links against the name C exports. `@as` changed the
  // name Rune uses; the symbol is still what was written. C++ exports the
  // name with its scope and parameter types folded in, so that is computed.
  if (isCxxExtern(fn))
    return mangleCxx(fn);
  if (fn->IsExtern)
    return fn->LinkName.empty() ? fn->Name : fn->LinkName;
  if (const char *lang = langItemSymbol(fn))
    return lang;
  if (fn->hasAttr("export")) {
    const Attribute *a = fn->findAttr("export");
    if (!a->Args.empty())
      if (const auto *s = dyn_cast<StringLitExpr>(a->Args[0].get()))
        return s->Value;
    return fn->Name;
  }

  std::string s = "_R";
  if (!fn->ModulePath.empty()) {
    std::string mod = fn->ModulePath;
    for (char &c : mod)
      if (c == ':' || c == '/' || c == '.' || c == '-')
        c = '_';
    s += std::to_string(mod.size()) + mod;
  }
  auto sanitise = [](std::string v) {
    for (char &c : v)
      if (!std::isalnum(static_cast<unsigned char>(c)))
        c = '_';
    return v;
  };
  if (fn->Parent && !fn->Parent->Name.empty()) {
    // The owner's own arguments belong in the name. A generic method carries
    // only its own in the `G` suffix, so without these `Filter<A>::map<T>`
    // and `Filter<B>::map<T>` would be the same symbol.
    std::string owner = fn->Parent->Name;
    if (auto *nd = dyn_cast<NominalDecl>(fn->Parent))
      for (Type *a : nd->TypeArguments)
        owner += "_" + sanitise(mangleTypeName(a));
    s += "T" + std::to_string(owner.size()) + owner;
  } else if (fn->OwnerType) {
    // A method bound to a builtin has no owning declaration, so the type's
    // own spelling keeps `bind Display to i64` and `to f64` apart.
    std::string owner = sanitise(mangleTypeName(fn->OwnerType));
    s += "T" + std::to_string(owner.size()) + owner;
  }
  // A mark's implementation and an inherent method of the same name live on
  // the same type at once, and two marks may both supply `describe`. The mark
  // name keeps all of them apart.
  if (fn->FromMark && !static_cast<Decl *>(fn->FromMark)->Name.empty()) {
    std::string mk = static_cast<Decl *>(fn->FromMark)->Name;
    // A generic mark's arguments are part of which binding this is:
    // `As<Fahrenheit>` and `As<Kelvin>` both supply `convert` for one type.
    for (Type *a : fn->FromMark->TypeArguments)
      mk += "_" + sanitise(mangleTypeName(a));
    s += "M" + std::to_string(mk.size()) + mk;
  }
  s += "F" + std::to_string(fn->Name.size()) + fn->Name;
  // A method a `bind` supplies may be overloaded — an operator once per
  // right-hand type, and any bound method once per parameter list — so
  // several bodies of one name can sit on one type at once. The parameters'
  // spelling is what keeps their symbols apart, and every bound method
  // carries it whether or not it turned out to have a rival, so that the
  // name does not depend on what else the program happens to declare.
  if (fn->IsOperatorImpl || fn->Bind) {
    for (const Param &p : fn->Params) {
      if (p.IsSelf || !p.Ty)
        continue;
      std::string ps = sanitise(mangleTypeName(p.Ty));
      s += "P" + std::to_string(ps.size()) + ps;
    }
  }
  for (Type *t : typeArgs) {
    std::string ts = mangleTypeName(t);
    // Keep the mangled form identifier-safe.
    for (char &c : ts)
      if (!std::isalnum(static_cast<unsigned char>(c)))
        c = '_';
    s += "G" + std::to_string(ts.size()) + ts;
  }
  return s;
}

//===----------------------------------------------------------------------===//
// Pass driver
//===----------------------------------------------------------------------===//

bool Sema::check() {
  ModuleScope = pushScope(ScopeKind::Module);

  // Each module gets its own scope, all sharing the root so builtins resolve.
  for (Module *m : Modules) {
    CurScope = ModuleScope;
    Scope *ms = pushScope(ScopeKind::Module);
    ModuleScopes[m] = ms;
    CurScope = ModuleScope;
  }

  for (Module *m : Modules)
    collectModule(m);
  findLangItems();
  for (Module *m : Modules)
    resolveImports(m);
  // An `extend` on a generic type is folded into the type before any module's
  // shapes are resolved. Resolving a module's shapes instantiates types, and
  // an instance made before its template had the extend's methods never gets
  // them: done module by module, `Vector<String>` — instantiated by a module
  // that happens to come before `std::collections::vector` — had no `values`
  // or `drain`, while `Vector<Point>` in the program had both.
  for (Module *m : Modules) {
    CurModule = m;
    CurScope = scopeForModule(m);
    for (auto &d : m->Decls)
      if (auto *e = dyn_cast<ExtendDecl>(d.get()))
        foldGenericExtend(e);
  }
  for (Module *m : Modules)
    resolveShapes(m);
  ShapesDone = true;
  // A conditional bind turned down while the binds its `where` asks about
  // were still being registered gets its answer now. Applying one may
  // instantiate more, which queue behind it.
  for (size_t i = 0; i < DeferredConditionalBinds.size(); ++i) {
    DeferredConditionalBind d = DeferredConditionalBinds[i];
    if (!bindApplies(d.M, d.B, d.Args))
      continue;
    registerBindFor(d.B, d.Inst->DeclaredType, static_cast<Decl *>(d.Inst),
                    d.Args);
    resolveSuppliedSignatures(d.Inst);
  }
  DeferredConditionalBinds.clear();
  // Every `bind` is registered by now, so a bound on an associated type can
  // be answered by one written after the bind that has to satisfy it.
  for (const DeferredBound &d : DeferredBounds)
    checkGenericBound(d.Chosen, d.Bound, d.At, d.Declared, d.Owner);
  DeferredBounds.clear();
  for (Module *m : Modules)
    resolveSignatures(m);
  // Two bindings that both claimed one name are rivals only if they take the
  // same things; otherwise they are two ways to call it. Parameter types are
  // known now, so the question can finally be answered.
  reportSlotClashes();
  checkStrongCycles();
  checkConventions();
  // Everything is declared, so bodies can be checked: first the generic
  // instantiations that earlier passes asked for, then the modules.
  // Every method table is settled, so each type's `deinit` — whether it came
  // from the body, an `extend` or a `bind` — can be found. This has to happen
  // before *any* body is checked, instantiated ones included: handing an
  // owning value away is a move, and whether a value owns anything is exactly
  // this question. A body checked before the answer is known would emit the
  // destructor without the move, and destroy what it had already given away.
  resolveDeinitialisers();
  InBodyPass = true;
  drainPendingInstantiations();
  drainPendingMethods();
  drainStructuralBodies();
  for (Module *m : Modules)
    checkBodies(m);
  drainStructuralBodies();
  // Instantiations asked for while bodies were checked bring their own
  // `deinit` with them.
  resolveDeinitialisers();
  // A generic binding only claims its slots once something instantiates the
  // type it applies to, which may not have happened until a body asked. Any
  // clash that turned up then is decided here.
  reportSlotClashes();

  // Last, and all together: every body has been checked, so each of these has
  // everything it needs and none of them needs anything from another.
  checkOwnershipOfQueued();

  // Hand the resolved tables to CodeGen, which needs them for `dyn` dispatch.
  // Both of them: a mark bound twice to one type has one version answering
  // its requirement and the rest reachable by their parameters, and only the
  // per-mark table knows which is which.
  Result.MethodTables = Methods;
  Result.MarkTables = MarkImpls;

  CurScope = ModuleScope;
  return !Diags.hadError();
}

/// Reads every queued body for what it does with what it owns.
///
/// This is the one pass in checking whose items are genuinely independent:
/// `checkOwnership` is handed a function and looks only inside it, resolving
/// nothing and consulting no table. So they are done on every core there is.
///
/// The order the findings come back in is the order the bodies were queued,
/// not the order the threads happened to finish: each body reports into a
/// bucket of its own, and the buckets are replayed in sequence. Two builds of
/// the same program therefore print the same diagnostics in the same order,
/// whatever the machine was doing at the time.
void Sema::checkOwnershipOfQueued() {
  if (OwnershipQueue.empty())
    return;

  // Under Zombie the queue goes to the borrow checker, which orders and
  // parallelises the bodies itself: a body needs its callees' summaries.
  if (Memory == MemoryMode::Zombie) {
    const auto start = std::chrono::steady_clock::now();
    zombie::checkProgram(OwnershipQueue, {}, Diags, Dump, ZombieStdlib);
    ZombieMillis += std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - start)
                        .count();
    OwnershipQueue.clear();
    OwnershipQueued.clear();
    return;
  }

  std::vector<std::vector<Diagnostic>> found(OwnershipQueue.size());
  parallelFor(OwnershipQueue.size(), [&](size_t i) {
    Diags.beginCapture(&found[i]);
    checkOwnership(OwnershipQueue[i], Diags, Safety);
    Diags.endCapture();
  });
  for (const auto &bucket : found)
    Diags.replay(bucket);

  OwnershipQueue.clear();
  OwnershipQueued.clear();
}

//===----------------------------------------------------------------------===//
// Specialisation
//===----------------------------------------------------------------------===//

namespace {
/// How much of `t` is decided rather than left to a parameter. `Wrapper<i64>`
/// counts more than `Wrapper<T>`, which counts more than a bare `T`.
int typeSpecificity(const Type *t, unsigned depth = 0) {
  if (!t || depth > 16)
    return 0;
  if (t->is(TypeKind::Generic))
    return 0;
  int score = 1;
  switch (t->kind()) {
  case TypeKind::Struct:
  case TypeKind::Enum:
  case TypeKind::Class:
  case TypeKind::Mark:
    for (const Type *a : t->typeArguments())
      score += typeSpecificity(a, depth + 1);
    break;
  case TypeKind::Pointer:
  case TypeKind::Array:
  case TypeKind::Slice:
    score += typeSpecificity(t->element(), depth + 1);
    break;
  case TypeKind::Tuple:
    for (const Type *e : t->tupleElements())
      score += typeSpecificity(e, depth + 1);
    break;
  default:
    break;
  }
  return score;
}
} // namespace

int Sema::bindSpecificity(const BindDecl *b) const {
  if (!b)
    return 0;
  // A bind with no parameters names exactly one type, and nothing written
  // with parameters can be more specific than that.
  if (b->Generics.empty())
    return 1000000;
  // Otherwise: how much of the target is pinned down, and then how much is
  // asked of what is left. `where T: Show` is narrower than nothing at all.
  int bounds = 0;
  for (const auto &g : b->Generics)
    bounds += static_cast<int>(g.Bounds.size());
  for (const auto &w : b->WhereClauses)
    bounds += static_cast<int>(w.Bounds.size());
  return typeSpecificity(b->ResolvedTarget) * 1000 + bounds;
}

bool Sema::sameBindFamily(const BindDecl *a, const BindDecl *b) {
  if (!a || !b)
    return false;
  if (a->IsOperatorBinding || b->IsOperatorBinding)
    return a->IsOperatorBinding && b->IsOperatorBinding &&
           a->OperatorName == b->OperatorName;
  return a->ResolvedMark && a->ResolvedMark == b->ResolvedMark;
}

void Sema::addOverload(Type *target, const std::string &name, FunctionDecl *fn,
                       const BindDecl *from) {
  if (!fn)
    return;
  auto &list = MethodOverloads[{target, name}];
  for (FunctionDecl *&have : list) {
    if (have == fn)
      return;
    // A generic bind hands out a fresh clone for every instantiation, and a
    // binding may be registered again from a second direction. Neither is a
    // second way to call the name: same binding, same parameters, one entry.
    if (have->Bind == from && have->Parent == fn->Parent &&
        sameParameterList(have, fn)) {
      have = fn;
      return;
    }
  }
  list.push_back(fn);
}

void Sema::addMarkImpl(Type *target, MarkDecl *mark, const std::string &name,
                       FunctionDecl *fn) {
  if (!target || !mark || !fn)
    return;
  auto &list = MarkImplSets[{target, mark}][name];
  for (FunctionDecl *&have : list) {
    if (have == fn)
      return;
    if (have->Bind == fn->Bind && have->Parent == fn->Parent &&
        sameParameterList(have, fn)) {
      have = fn;
      return;
    }
  }
  list.push_back(fn);
}

std::vector<FunctionDecl *> Sema::markImplSet(Type *receiver, MarkDecl *mark,
                                              const std::string &name) {
  std::vector<FunctionDecl *> out;
  if (!receiver || !mark)
    return out;
  while (receiver->is(TypeKind::Pointer))
    receiver = receiver->pointee();
  auto it = MarkImplSets.find({receiver, mark});
  if (it == MarkImplSets.end())
    return out;
  auto nit = it->second.find(name);
  if (nit != it->second.end())
    out = nit->second;
  return out;
}

/// Drops every candidate a narrower binding has just displaced. Specificity
/// decides between rival bindings before parameters do: a written-out target
/// beats a parameterised one whatever either of them takes.
void Sema::dropWiderOverloads(Type *target, const std::string &name,
                              const BindDecl *from, int score) {
  auto it = MethodOverloads.find({target, name});
  if (it == MethodOverloads.end())
    return;
  auto &list = it->second;
  list.erase(std::remove_if(list.begin(), list.end(),
                            [&](FunctionDecl *f) {
                              return sameBindFamily(f->Bind, from) &&
                                     f->Bind != from &&
                                     bindSpecificity(f->Bind) < score;
                            }),
             list.end());
}

void Sema::claimMethodSlot(Type *target, const std::string &name,
                           FunctionDecl *fn, const BindDecl *from, int score,
                           std::map<std::string, FunctionDecl *> &table) {
  // A method the type declares itself always answers `value.name()`; a
  // binding's stays reachable as `value::Mark.name()`.
  if (InherentMethods.count({target, name}))
    return;
  if (from)
    fn->Bind = from;

  auto slot = std::make_pair(target, name);
  auto held = MethodClaims.find(slot);
  if (held != MethodClaims.end()) {
    if (held->second.second == from) {
      // The same binding again — either re-registered, or writing the name
      // twice with different parameters, which is an overload.
      addOverload(target, name, fn, from);
      if (MethodOverloads[slot].front() == fn)
        table[name] = fn;
      return;
    }
    // Only two bindings that supply this slot for the *same* reason are in
    // competition: the same mark, or the same operator. Two marks that happen
    // to name a method the same way are not: `Iterator` and `Sequence` both
    // supply `map`, and `As<Fahrenheit>` and `As<Kelvin>` both supply
    // `convert`. Which of those answers is settled elsewhere — by the mark
    // asked for — so this leaves them exactly as it found them.
    const BindDecl *other = held->second.second;
    if (!sameBindFamily(from, other)) {
      MethodClaims[slot] = {score, from};
      table[name] = fn;
      return;
    }
    if (held->second.first > score)
      return;             // what is there is narrower
    if (held->second.first == score && from && held->second.second) {
      // Two bindings that are equally specific both claim this. That is a
      // clash when they take the same things and an overload when they do
      // not, and which it is cannot be known until parameter types are
      // resolved — so record both and decide after the signature pass.
      FunctionDecl *heldFn = table.count(name) ? table[name] : nullptr;
      addOverload(target, name, heldFn ? heldFn : fn, other);
      addOverload(target, name, fn, from);
      SlotClashes.push_back({target, name, other, from, heldFn, fn});
      return;
    }
  }
  MethodClaims[slot] = {score, from};
  table[name] = fn;
  dropWiderOverloads(target, name, from, score);
  addOverload(target, name, fn, from);
}

bool Sema::sameParameterList(FunctionDecl *a, FunctionDecl *b) {
  if (!a || !b)
    return false;
  ensureTemplateSignature(a);
  ensureTemplateSignature(b);
  std::vector<Type *> pa, pb;
  for (const Param &p : a->Params)
    if (!p.IsSelf)
      pa.push_back(p.Ty);
  for (const Param &p : b->Params)
    if (!p.IsSelf)
      pb.push_back(p.Ty);
  if (pa.size() != pb.size())
    return false;
  for (size_t i = 0; i < pa.size(); ++i) {
    // An unresolved parameter is not evidence of a difference, so treat a
    // pair the signature pass has not reached yet as matching.
    if (!pa[i] || !pb[i])
      continue;
    if (!sameType(pa[i], pb[i]))
      return false;
  }
  return true;
}

void Sema::reportSlotClashes() {
  for (const SlotClash &c : SlotClashes) {
    if (!c.HeldFn || !c.ClaimedFn)
      continue;
    if (!sameParameterList(c.HeldFn, c.ClaimedFn))
      continue;   // two ways to call the name, not two answers to one call
    auto d = Diags.error(c.Claimed->MarkRange.isValid() ? c.Claimed->MarkRange
                                                        : c.Claimed->Range,
                         "two bindings of '{}' are equally specific for '{}'",
                         c.Name, c.Target->toString());
    d.note("neither is narrower than the other, so which one applies would "
           "depend on the order they were reached in");
    d.note("give one of them a `where` clause, write the target out, or have "
           "them take different parameters");
    d.related(c.Held->MarkRange.isValid() ? c.Held->MarkRange : c.Held->Range,
              "the other binding", "one of these has to become narrower");
    d.code(219);
  }
  SlotClashes.clear();
}

bool Sema::unifyGenericArg(Type *pattern, Type *concrete,
                           std::map<std::string, Type *> &out) {
  if (!pattern || !concrete)
    return false;
  if (pattern == concrete)
    return true;
  // A parameter matches anything, once: `bind<T> Show to Pair<T, T>` needs
  // both halves to agree.
  if (pattern->is(TypeKind::Generic)) {
    auto it = out.find(pattern->genericName());
    if (it != out.end())
      return it->second == concrete;
    out[pattern->genericName()] = concrete;
    return true;
  }
  if (pattern->kind() != concrete->kind())
    return false;
  switch (pattern->kind()) {
  case TypeKind::Struct:
  case TypeKind::Enum:
  case TypeKind::Class:
  case TypeKind::Mark: {
    // The same template, then argument by argument. This is what stops
    // `bind<T> Show to Wrapper<Wrapper<T>>` from claiming every `Wrapper`.
    NominalDecl *pn = pattern->nominal();
    NominalDecl *cn = concrete->nominal();
    if (!pn || !cn)
      return false;
    NominalDecl *pt = pn->GenericTemplate ? pn->GenericTemplate : pn;
    NominalDecl *ct = cn->GenericTemplate ? cn->GenericTemplate : cn;
    if (pt != ct)
      return false;
    if (pattern->typeArguments().size() != concrete->typeArguments().size())
      return false;
    for (size_t i = 0; i < pattern->typeArguments().size(); ++i)
      if (!unifyGenericArg(pattern->typeArguments()[i],
                           concrete->typeArguments()[i], out))
        return false;
    return true;
  }
  case TypeKind::Pointer:
    if (pattern->isMutablePointer() != concrete->isMutablePointer() ||
        pattern->isRawPointer() != concrete->isRawPointer() ||
        pattern->isWeakPointer() != concrete->isWeakPointer())
      return false;
    return unifyGenericArg(pattern->pointee(), concrete->pointee(), out);
  case TypeKind::Slice:
    return unifyGenericArg(pattern->element(), concrete->element(), out);
  case TypeKind::Array:
    if (pattern->arraySize() != concrete->arraySize())
      return false;
    return unifyGenericArg(pattern->element(), concrete->element(), out);
  case TypeKind::Tuple: {
    if (pattern->tupleElements().size() != concrete->tupleElements().size())
      return false;
    for (size_t i = 0; i < pattern->tupleElements().size(); ++i)
      if (!unifyGenericArg(pattern->tupleElements()[i],
                           concrete->tupleElements()[i], out))
        return false;
    return true;
  }
  default:
    return false;
  }
}

//===----------------------------------------------------------------------===//
// Generic binds
//===----------------------------------------------------------------------===//

bool Sema::bindApplies(Module *bindModule, BindDecl *b,
                       const std::map<std::string, Type *> &bindArgs) {
  // A bound is written in the bind's own file, so that is where it resolves.
  Scope *savedScope = CurScope;
  Module *savedModule = CurModule;
  if (bindModule)
    if (Scope *own = scopeForModule(bindModule)) {
      CurScope = own;
      CurModule = bindModule;
    }
  struct Restore {
    Sema &S; Scope *Sc; Module *M;
    ~Restore() { S.CurScope = Sc; S.CurModule = M; }
  } restore{*this, savedScope, savedModule};

  auto boundsHold = [&](const std::string &paramName,
                        const std::vector<TypeReprPtr> &bounds) {
    auto argIt = bindArgs.find(paramName);
    if (argIt == bindArgs.end())
      return true;
    for (const auto &boundRepr : bounds) {
      // An operator bound asks what the argument overloads, not what mark it
      // implements. A conditional bind that does not apply is silent, so this
      // only decides — it never reports.
      if (auto *named = dyn_cast<NamedTypeRepr>(boundRepr.get()))
        if (named->Path.size() == 2 && named->Path[0] == "operator") {
          if (!lookupOperator(argIt->second,
                              canonicalOperatorName(named->Path[1])))
            return false;
          continue;
        }
      auto savedGenerics = ActiveGenericParams;
      bindGenerics(b->Generics, Types, ActiveGenericParams);
      Type *bt = resolveTypeOrError(boundRepr.get(), Types.errorType());
      ActiveGenericParams = savedGenerics;
      if (!bt->is(TypeKind::Mark))
        continue;
      if (!typeConformsTo(argIt->second,
                          reinterpret_cast<MarkDecl *>(bt->nominal())))
        return false;
    }
    return true;
  };

  for (const auto &g : b->Generics)
    if (!boundsHold(g.Name, g.Bounds))
      return false;
  for (const auto &w : b->WhereClauses) {
    auto *subject = dyn_cast<NamedTypeRepr>(w.Subject.get());
    if (!subject)
      continue;
    if (subject->Path.size() == 1) {
      if (!boundsHold(subject->Path[0], w.Bounds))
        return false;
      continue;
    }
    // `where I::Item: Iterator` — the subject is a projection off one of the
    // bind's parameters, so it is resolved with the arguments standing in.
    // A projection that does not exist means the clause does not hold; it is
    // not something to report, since a conditional bind that does not apply
    // is meant to be silent.
    if (!bindArgs.count(subject->Path[0]))
      continue;
    // Resolved on a copy: a resolved annotation is cached on its node, and
    // this one has a different answer for every target the bind is tried on.
    auto savedGenerics = ActiveGenericParams;
    ActiveGenericParams = bindArgs;
    TypeReprPtr copy = cloneTypeRepr(w.Subject.get());
    Diags.beginSpeculation();
    Type *subjectTy = resolveType(copy.get());
    Diags.endSpeculation();
    ActiveGenericParams = savedGenerics;
    if (!subjectTy || subjectTy->isError())
      return false;
    for (const auto &boundRepr : w.Bounds) {
      if (auto *named = dyn_cast<NamedTypeRepr>(boundRepr.get()))
        if (named->Path.size() == 2 && named->Path[0] == "operator") {
          if (!lookupOperator(subjectTy, canonicalOperatorName(named->Path[1])))
            return false;
          continue;
        }
      auto saved = ActiveGenericParams;
      bindGenerics(b->Generics, Types, ActiveGenericParams);
      Type *bt = resolveTypeOrError(boundRepr.get(), Types.errorType());
      ActiveGenericParams = saved;
      if (!bt->is(TypeKind::Mark))
        continue;
      if (!typeConformsTo(subjectTy,
                          reinterpret_cast<MarkDecl *>(bt->nominal())))
        return false;
    }
  }
  return true;
}

void Sema::registerBindFor(BindDecl *b, Type *target, Decl *parent,
                           const std::map<std::string, Type *> &bindArgs) {
  // Once per type: a conditional bind turned down early and asked again may
  // meet one another path already applied, and a second set of copies would
  // leave one of them unchecked.
  if (!RegisteredBinds.insert({target, b}).second)
    return;
  auto &table = Methods[target];
  const int score = bindSpecificity(b);
  // The mark's own table for this target, so `value::Mark.name()` and
  // everything else that dispatches through a mark — `for` asking an
  // `Iterator` for `next`, among them — finds the binding here too.
  std::map<std::string, FunctionDecl *> *markTable =
      b->ResolvedMark ? &MarkImpls[{target, b->ResolvedMark}] : nullptr;
  for (auto &fn : b->Methods) {
    auto cloneFn = cloneFunction(fn.get());
    cloneFn->Parent = parent;
    // A method copied from a mark default already knows which file it was
    // written in, and that is where its names resolve. Only a method the bind
    // itself wrote belongs to the bind's module.
    cloneFn->ModulePath =
        fn->ModulePath.empty() ? b->ModulePath : fn->ModulePath;
    FunctionDecl *raw = cloneFn.get();
    raw->OwnerType = target;
    raw->Bind = b;
    Synthesised.push_back(std::move(cloneFn));
    if (b->IsOperatorBinding) {
      // A block may carry several operators — `deref` and `derefSet` in one
      // `operator::"*"` — so each method claims the slot its own name
      // canonicalises to, not the block's.
      std::string op = canonicalOperatorName(b->OperatorName);
      std::string slot =
          raw->Name == op ? op : canonicalOperatorName(raw->Name);
      Operators[{target, slot}].push_back(raw);
      for (const auto &alias : aliasesOf(raw))
        Operators[{target, canonicalOperatorName(alias.first)}].push_back(raw);
    }
    claimMethodSlot(target, raw->Name, raw, b, score, table);
    for (const auto &alias : aliasesOf(raw))
      claimMethodSlot(target, alias.first, raw, b, score, table);
    if (markTable) {
      (*markTable)[raw->Name] = raw;
      addMarkImpl(target, b->ResolvedMark, raw->Name, raw);
      for (const auto &alias : aliasesOf(raw)) {
        markTable->emplace(alias.first, raw);
        addMarkImpl(target, b->ResolvedMark, alias.first, raw);
      }
    }
  }
  if (!b->ResolvedMark)
    return;
  Conformances[target].push_back(b->ResolvedMark);
  // And what the bind chose for the mark's associated types, read with this
  // target's arguments in scope so `type Item = T` becomes the argument
  // rather than staying a parameter. The choice is resolved on a *copy* of
  // the annotation: the template's own node keeps the generic answer that
  // every other target needs.
  auto &chosen = AssocTypes[{target, b->ResolvedMark}];
  auto savedAssocGenerics = ActiveGenericParams;
  Type *savedAssocSelf = ActiveSelfType;
  ActiveGenericParams = bindArgs;
  ActiveSelfType = target;
  for (auto &at : b->AssociatedTypes) {
    if (!at->Value)
      continue;
    TypeReprPtr copy = cloneTypeRepr(at->Value.get());
    chosen[at->Name] = resolveTypeOrError(copy.get(), Types.errorType());
  }
  ActiveGenericParams = savedAssocGenerics;
  ActiveSelfType = savedAssocSelf;
}

bool Sema::unifyTargetRepr(Module *bindModule, TypeRepr *repr, Type *t,
                           const std::set<std::string> &params,
                           std::map<std::string, Type *> &out) {
  if (!repr || !t)
    return false;

  // A bare parameter name matches anything, once: `bind<T> Show to (T, T)`
  // needs both halves to agree.
  if (auto *named = dyn_cast<NamedTypeRepr>(repr)) {
    if (named->Path.size() == 1 && named->GenericArgs.empty() &&
        params.count(named->Path[0])) {
      auto it = out.find(named->Path[0]);
      if (it != out.end())
        return it->second == t;
      out[named->Path[0]] = t;
      return true;
    }
    // Anything else is a name, and a name means nothing without a scope: it
    // has to be resolved in the file the bind was written in.
    Scope *savedScope = CurScope;
    Module *savedModule = CurModule;
    if (bindModule)
      if (Scope *own = scopeForModule(bindModule)) {
        CurScope = own;
        CurModule = bindModule;
      }
    auto savedGenerics = ActiveGenericParams;
    Type *resolved = resolveType(repr);
    ActiveGenericParams = savedGenerics;
    CurScope = savedScope;
    CurModule = savedModule;
    return resolved && resolved == t;
  }

  switch (repr->Kind) {
  case NodeKind::SliceType: {
    // `[T]` matches an array as well as a slice. An array converts to one
    // wherever a slice is wanted, so a binding written for the shape covers
    // both — and the methods are registered against each separately, so
    // `self` is whichever the caller actually had.
    if (!t->is(TypeKind::Slice) && !t->is(TypeKind::Array))
      return false;
    return unifyTargetRepr(bindModule, cast<SliceTypeRepr>(repr)->Element.get(),
                           t->element(), params, out);
  }
  case NodeKind::ArrayType: {
    if (!t->is(TypeKind::Array))
      return false;
    // The length is not a parameter — Rune has no constant generics — so an
    // array target only matches when the length it names is that length.
    auto *arr = cast<ArrayTypeRepr>(repr);
    if (arr->Size) {
      int64_t written = 0;
      if (!evalConstInt(arr->Size.get(), written) ||
          static_cast<uint64_t>(written) != t->arraySize())
        return false;
    }
    return unifyTargetRepr(bindModule, arr->Element.get(), t->element(),
                           params, out);
  }
  case NodeKind::PointerType: {
    if (!t->is(TypeKind::Pointer))
      return false;
    auto *p = cast<PointerTypeRepr>(repr);
    if (p->IsMutable != t->isMutablePointer() ||
        p->IsRaw != t->isRawPointer() || p->IsWeak != t->isWeakPointer())
      return false;
    return unifyTargetRepr(bindModule, p->Pointee.get(), t->pointee(), params,
                           out);
  }
  case NodeKind::TupleType: {
    if (!t->is(TypeKind::Tuple))
      return false;
    auto *tp = cast<TupleTypeRepr>(repr);
    if (tp->Elements.size() != t->tupleElements().size())
      return false;
    for (size_t i = 0; i < tp->Elements.size(); ++i)
      if (!unifyTargetRepr(bindModule, tp->Elements[i].get(),
                           t->tupleElements()[i], params, out))
        return false;
    return true;
  }
  case NodeKind::OptionalType:
    // `T?` is `Option<T>`, a nominal type, and the nominal path already
    // covers it.
    return false;
  default:
    return false;
  }
}

/// True for a target that names a shape rather than a declaration.
static bool isStructuralTarget(const TypeRepr *repr) {
  if (!repr)
    return false;
  switch (repr->Kind) {
  case NodeKind::SliceType:
  case NodeKind::ArrayType:
  case NodeKind::PointerType:
  case NodeKind::TupleType:
    return true;
  default:
    return false;
  }
}

void Sema::ensureStructuralBinds(Type *t) {
  if (!t || t->isNominal() || t->isError() || t->is(TypeKind::Generic))
    return;
  // Marked before the work, not after: checking a bound below asks the same
  // question again, and the answer has to be "already in hand" rather than a
  // second walk that never ends.
  if (!StructuralBindsApplied.insert(t).second)
    return;

  for (Module *m : Modules)
    for (auto &d : m->Decls) {
      auto *b = dyn_cast<BindDecl>(d.get());
      if (!b || b->Generics.empty() || !isStructuralTarget(b->TargetType.get()))
        continue;
      // The bind has to be prepared — its mark resolved, the mark's defaults
      // copied in — before its methods mean anything.
      prepareBind(m, b);

      std::set<std::string> params;
      for (const auto &g : b->Generics)
        params.insert(g.Name);
      std::map<std::string, Type *> bindArgs;
      if (!unifyTargetRepr(m, b->TargetType.get(), t, params, bindArgs))
        continue;
      // Every parameter has to have been pinned down; one the shape never
      // mentions would leave a method with a type nobody chose.
      if (bindArgs.size() != params.size())
        continue;
      if (!bindApplies(m, b, bindArgs))
        continue;

      registerBindFor(b, t, /*parent=*/nullptr, bindArgs);

      // Signatures now, with the bind's parameters standing for what they
      // were matched to. There is no instantiation pass to come back and do
      // it, so this is the only chance.
      auto savedGenerics = ActiveGenericParams;
      Type *savedSelf = ActiveSelfType;
      Scope *savedScope = CurScope;
      Module *savedModule = CurModule;
      ActiveGenericParams = bindArgs;
      ActiveSelfType = t;
      std::vector<FunctionDecl *> mine;
      std::vector<FunctionDecl *> supplied;
      for (auto &entry : Methods[t])
        supplied.push_back(entry.second);
      // Only one version of an overloaded name holds the table slot; the
      // others still need a signature and a body of their own.
      appendOverloadsFor(t, supplied);
      for (FunctionDecl *fn : supplied) {
        if (fn->OwnerType != t || fn->Ty || !fn->Generics.empty())
          continue;
        resolveMethodSignature(fn, t, {});
        if (fn->Body && !fn->IsImported)
          mine.push_back(fn);
      }
      ActiveGenericParams = savedGenerics;
      ActiveSelfType = savedSelf;
      CurScope = savedScope;
      CurModule = savedModule;

      // Bodies after the whole program is declared. Checking one here could
      // ask about a type whose own shapes have not been resolved yet — the
      // first question about a slice can be asked from the signature pass.
      for (FunctionDecl *fn : mine)
        PendingStructuralBodies.push_back({fn, t, bindArgs});
      if (InBodyPass)
        drainStructuralBodies();
    }
}

void Sema::checkStructuralBody(const StructuralBody &body) {
  if (!body.Fn || BodiesChecked.count(body.Fn))
    return;
  auto savedGenerics = ActiveGenericParams;
  Type *savedSelf = ActiveSelfType;
  ActiveGenericParams = body.Args;
  ActiveSelfType = body.Target;
  checkFunction(body.Fn, body.Target, nullptr);
  Result.Functions.push_back(body.Fn);
  ActiveGenericParams = savedGenerics;
  ActiveSelfType = savedSelf;
}

void Sema::drainStructuralBodies() {
  // Checking one may apply a bind to another shape, which lands at the back
  // of the queue; keep going until nothing new arrives.
  for (size_t i = 0; i < PendingStructuralBodies.size(); ++i)
    checkStructuralBody(PendingStructuralBodies[i]);
  PendingStructuralBodies.clear();
}

//===----------------------------------------------------------------------===//
// Destructors on value types
//===----------------------------------------------------------------------===//

void Sema::checkDeinitSignature(NominalDecl *nd, FunctionDecl *fn) {
  const std::string &owner = static_cast<Decl *>(nd)->Name;
  SourceRange at = fn->NameRange.isValid() ? fn->NameRange : fn->Range;

  if (!fn->Generics.empty()) {
    Diags.error(at, "'{}' cannot have type parameters", owner + "::deinit")
        .note("the compiler calls it on the way out of a scope, where there "
              "is nothing to infer them from")
        .code(268);
    return;
  }
  const Param *self = nullptr;
  unsigned others = 0;
  for (const Param &p : fn->Params) {
    if (p.IsSelf)
      self = &p;
    else
      ++others;
  }
  if (!self) {
    Diags.error(at, "'{}' has to take `self`", owner + "::deinit")
        .note("write `fn deinit(&var self)` — it runs on a value that is "
              "about to be destroyed")
        .code(268);
    return;
  }
  if (others) {
    Diags.error(at, "'{}' takes only `self`", owner + "::deinit")
        .note("nothing is available to pass it: the compiler calls it on the "
              "way out of a scope")
        .code(268);
    return;
  }
  // A class reference is a pointer already, so `deinit(self)` on one hands
  // over the instance rather than a copy of it. A value type has to say
  // `&self`, or the destructor would run on a copy and leave the original
  // holding what it was meant to release.
  const bool isValueType = !isa<ClassDecl>(static_cast<Decl *>(nd));
  if (isValueType && !self->SelfByRef) {
    Diags.error(at, "'{}' has to borrow `self`", owner + "::deinit")
        .note("write `&self` or `&var self`; taking it by value would run on "
              "a copy and leave the original holding what it was meant to "
              "release")
        .code(268);
    return;
  }
  Type *ret = fn->Ty ? fn->Ty->result() : nullptr;
  if (ret && !ret->isVoid() && !ret->isError()) {
    Diags.error(fn->ReturnType ? fn->ReturnType->Range : at,
                "'{}' cannot return a value", owner + "::deinit")
        .note("there is no one left to hand it to")
        .code(268);
  }
}

namespace {
/// Every `x.name` written anywhere inside `n`, by name.
///
/// This is deliberately blunt: it answers "does the `deinit` look at this
/// field at all", not "does it release it correctly". A `@resource` is a
/// promise the author made about a field, and the compiler's part is to
/// notice when the promise was forgotten.
void collectMemberNames(const Node *n, std::set<std::string> &out) {
  if (!n)
    return;
  if (const auto *m = dyn_cast<MemberExpr>(n))
    out.insert(m->Name);
  forEachChild(n, [&](const Node *c) { collectMemberNames(c, out); });
}
} // namespace

void Sema::checkResourceFields(NominalDecl *nd) {
  // Which field names the `deinit` body mentions through `self`. A field
  // marked `@resource` holds something reference counting cannot release, so
  // a `deinit` that never looks at it is almost certainly a leak.
  std::set<std::string> mentioned;
  if (nd->Deinit && nd->Deinit->Body)
    collectMemberNames(nd->Deinit->Body.get(), mentioned);

  auto fieldsOf = [&](std::vector<FieldDecl *> &out) {
    for (auto &f : nd->Fields)
      out.push_back(f.get());
    if (auto *e = dyn_cast<EnumDecl>(static_cast<Decl *>(nd)))
      for (auto &v : e->Variants)
        for (auto &f : v->Fields)
          out.push_back(f.get());
  };
  std::vector<FieldDecl *> fields;
  fieldsOf(fields);

  for (FieldDecl *f : fields) {
    const Attribute *a = f->findAttr("resource");
    if (!a)
      continue;
    const std::string &owner = static_cast<Decl *>(nd)->Name;
    // A `@resource` is a promise about a field. `--safety full` holds the
    // program to it; below that the promise is still worth reporting, but it
    // does not stop the build.
    auto report = [&](SourceRange at, const std::string &text) {
      return Safety == SafetyLevel::Full ? Diags.error(at, "{}", text)
                                         : Diags.warn(at, "{}", text);
    };
    if (!nd->Deinit) {
      auto d = report(a->Range,
                      fmt("'{}' is a `@resource`, so '{}' needs a `deinit`",
                          f->Name, owner));
      d.note("a `@resource` field holds something reference counting cannot "
             "release — a descriptor, a handle, a lock");
      d.note(fmt("write `extend {} {{ fn deinit(&var self) {{ ... }} }}`",
                 owner).c_str());
      d.code(269);
      continue;
    }
    if (mentioned.count(f->Name))
      continue;
    auto d = report(a->Range,
                    fmt("'{}' is a `@resource` that '{}::deinit' never "
                        "mentions", f->Name, owner));
    d.note("release it there, or drop the `@resource` if nothing has to be "
           "handed back");
    d.related(nd->Deinit->NameRange.isValid() ? nd->Deinit->NameRange
                                              : nd->Deinit->Range,
              "this is the `deinit`",
              fmt("read `self.{}` here and release what it holds", f->Name));
    d.code(269);
  }
}

void Sema::resolveDeinitialisers() {
  std::vector<NominalDecl *> types;
  for (Module *m : Modules)
    for (auto &d : m->Decls)
      if (auto *nd = dyn_cast<NominalDecl>(d.get()))
        if (!isa<MarkDecl>(static_cast<Decl *>(nd)))
          types.push_back(nd);
  for (NominalDecl *inst : Result.Nominals)
    types.push_back(inst);

  for (NominalDecl *nd : types) {
    if (!nd->DeclaredType || !nd->Generics.empty())
      continue;
    // A C++ type's `deinit` is its destructor, and C++ objects are destroyed
    // when the program says so — never by Rune on the way out of a scope.
    if (nd->Cxx)
      continue;
    // Wherever it was written — the body, an `extend`, or a `bind` — the
    // method table is what a call would find, so it is what the compiler
    // calls too. An *inherited* one does not count: a subclass's destruction
    // chains to its base, which runs the base's `deinit` there. Claiming it
    // here as well would run it twice.
    NominalDecl *owner = nullptr;
    FunctionDecl *found = lookupMethod(nd->DeclaredType, "deinit", &owner);
    if (!found || owner != nd)
      continue;
    if (nd->Deinit && nd->Deinit != found)
      continue;              // a class's own body already named one
    nd->Deinit = found;
    checkDeinitSignature(nd, found);
  }
  // A type's own `clone`, found the way a call would find it: written in the
  // body, added by an `extend`, or supplied by a `bind`. `$clone()` calls it
  // rather than copying field by field.
  for (NominalDecl *nd : types) {
    if (!nd->DeclaredType || !nd->Generics.empty() || nd->CloneFn)
      continue;
    FunctionDecl *clone = lookupMethod(nd->DeclaredType, "clone");
    if (!clone)
      continue;
    ensureTemplateSignature(clone);
    bool self = false;
    size_t others = 0;
    for (const Param &p : clone->Params) {
      if (p.IsSelf) self = true; else ++others;
    }
    // `clone(&self) -> Self` and nothing else: a `clone` that takes
    // arguments, or hands back something other than this type, is a method
    // that happens to share the name.
    Type *result = clone->Ty ? clone->Ty->result() : nullptr;
    if (!self || others != 0 || !result ||
        TypeContext::stripUniq(result) != nd->DeclaredType)
      continue;
    nd->CloneFn = clone;
  }
  for (NominalDecl *nd : types)
    if (nd->DeclaredType && nd->Generics.empty() &&
        ResourcesChecked.insert(nd).second)
      checkResourceFields(nd);
  // `@never(M)` is read the first time somebody asks whether this type has
  // `M`, and a program may never ask — so it is read here as well, to report
  // one that names something which is not an automatic mark.
  for (NominalDecl *nd : types)
    if (static_cast<Decl *>(nd)->findAttr("never"))
      refusedMarks(nd);
}

bool Sema::typeOwnsResources(Type *t, std::set<Type *> &seen) {
  if (!t || !seen.insert(t).second)
    return false;
  switch (t->kind()) {
  case TypeKind::Struct:
  case TypeKind::Enum: {
    NominalDecl *nd = t->nominal();
    if (nd && nd->Deinit)
      return true;
    if (!nd)
      return false;
    for (auto &f : nd->Fields)
      if (typeOwnsResources(f->Ty, seen))
        return true;
    if (auto *e = dyn_cast<EnumDecl>(static_cast<Decl *>(nd)))
      for (auto &v : e->Variants) {
        for (auto &f : v->Fields)
          if (typeOwnsResources(f->Ty, seen))
            return true;
        for (auto &tt : v->TupleTypes)
          if (tt && typeOwnsResources(tt->Resolved, seen))
            return true;
      }
    return false;
  }
  case TypeKind::Array:
    return typeOwnsResources(t->element(), seen);
  case TypeKind::Tuple:
    for (Type *e : t->tupleElements())
      if (typeOwnsResources(e, seen))
        return true;
    return false;
  default:
    return false;
  }
}

/// Marks every local that `e` could hand over, following the positions a
/// value actually comes out of.
///
/// A `match` does not produce a value of its own: whichever arm ran did, and
/// it is that arm's binding the value came from. The same goes for an `if`
/// and for a block's trailing expression, so the walk follows all three
/// rather than only recognising a bare name.
bool Sema::markMovesInResultPosition(Expr *e) {
  if (!e)
    return false;
  switch (e->Kind) {
  case NodeKind::DeclRef: {
    auto *r = cast<DeclRefExpr>(e);
    auto *v = r->Resolved ? dyn_cast<VarDecl>(r->Resolved) : nullptr;
    if (!v || v->IsGlobal)
      return false;
    MovedFrom[v] = e;
    r->MovedOut = true;
    v->MovedSomewhere = true;
    return true;
  }
  case NodeKind::Block:
    return markMovesInResultPosition(cast<BlockExpr>(e)->Tail.get());
  case NodeKind::UnsafeBlock:
    return markMovesInResultPosition(
        cast<UnsafeBlockExpr>(e)->Body->Tail.get());
  case NodeKind::If: {
    auto *i = cast<IfExpr>(e);
    bool any = markMovesInResultPosition(i->Then ? i->Then->Tail.get()
                                                 : nullptr);
    return markMovesInResultPosition(i->Else.get()) || any;
  }
  case NodeKind::Match: {
    auto *m = cast<MatchExpr>(e);
    bool any = false;
    for (auto &arm : m->Arms)
      any = markMovesInResultPosition(arm.Body.get()) || any;
    return any;
  }
  default:
    return false;
  }
}

bool Sema::markOwnedMove(Expr *e, Type *from, Type *to) {
  if (!e || !from || !to || Safety == SafetyLevel::None)
    return false;
  if (!typeOwnsResources(from))
    return false;
  // Borrowing looks at the value without taking it, so it moves nothing.
  if (to->is(TypeKind::Pointer))
    return false;
  return markMovesInResultPosition(e);
}

//===----------------------------------------------------------------------===//
// Pass 1: collect
//===----------------------------------------------------------------------===//

/// Every name an `@alias("...")` decorator gives a declaration.
std::vector<std::pair<std::string, SourceRange>> Sema::aliasesOf(Decl *d) {
  std::vector<std::pair<std::string, SourceRange>> out;
  if (!d)
    return out;
  for (const Attribute &a : d->Attrs) {
    if (a.Name != "alias")
      continue;
    if (a.Args.empty()) {
      Diags.error(a.Range, "`@alias` needs the name to add")
          .note("write `@alias(\"other\")`")
          .code(233);
      continue;
    }
    const auto *lit = dyn_cast<StringLitExpr>(a.Args[0].get());
    if (!lit || lit->Value.empty()) {
      Diags.error(a.Args[0]->Range, "`@alias` takes a non-empty string")
          .note("the point of a string is to allow names an identifier "
                "cannot hold, e.g. `@alias(\"*\")`")
          .code(233);
      continue;
    }
    out.push_back({lit->Value, a.Range});
  }
  return out;
}

void Sema::collectDecl(Decl *d, Scope *scope) {
  auto declare = [&](Symbol sym) {
    if (Symbol *prev = scope->insert(sym)) {
      auto b = Diags.error(d->NameRange.isValid() ? d->NameRange : d->Range,
                           "'{}' is declared more than once in this module",
                           sym.Name);
      b.code(200);
      if (prev->D)
        noteDeclaredAt(b, prev->D, "the earlier declaration",
                       "rename one of them, or move it into another module");
    }
  };
  // `@alias("other")` puts the same declaration in scope under another name.
  auto declareWithAliases = [&](Symbol sym) {
    declare(sym);
    for (const auto &alias : aliasesOf(d)) {
      Symbol extra = sym;
      extra.Name = alias.first;
      if (Symbol *prev = scope->insert(extra)) {
        auto b = Diags.error(alias.second, "'{}' is already declared here",
                             alias.first);
        b.code(233);
        if (prev->D)
          noteDeclaredAt(b, prev->D, "the earlier declaration",
                         "an alias cannot take a name that is already taken");
      }
    }
  };

  switch (d->Kind) {
  case NodeKind::Function: {
    auto *f = cast<FunctionDecl>(d);
    Symbol s;
    s.Kind = SymbolKind::Function;
    s.Name = f->Name;
    s.D = f;
    s.IsPublic = f->IsPublic;
    declareWithAliases(s);
    break;
  }
  case NodeKind::Struct:
  case NodeKind::Enum:
  case NodeKind::Class:
  case NodeKind::Mark: {
    auto *nd = static_cast<NominalDecl *>(d);
    Symbol s;
    s.Kind = d->Kind == NodeKind::Mark ? SymbolKind::MarkName
                                       : SymbolKind::TypeName;
    s.Name = d->Name;
    s.D = d;
    s.IsPublic = d->IsPublic;
    declareWithAliases(s);
    // A generic template has no type of its own until it is instantiated.
    if (nd->Generics.empty())
      nd->DeclaredType = Types.nominalOf(nd);

    // Unit enum variants are also reachable by their bare name. An enum with
    // a parent has none of its numbering settled yet — the parent's variants
    // go in front of its own — so those are registered by the splice
    // instead, once the numbers are final.
    if (auto *e = dyn_cast<EnumDecl>(d); e && !e->Inherits) {
      for (const auto &v : e->Variants) {
        Symbol vs;
        vs.Kind = SymbolKind::Variant;
        vs.Name = v->Name;
        vs.D = v.get();
        vs.Owner = e;
        vs.VariantIndex = static_cast<int>(v->Index);
        vs.IsPublic = e->IsPublic;
        // Bare variant names live beside the module's declarations rather
        // than among them: a variable may share a variant's name, and two
        // enums may share one with each other. What that costs is that a
        // shared name has to be written out — `Car::Wheel` — at the point it
        // is used, which is where the ambiguity actually is.
        scope->addVariant(vs);
      }
    }
    break;
  }
  case NodeKind::GlobalVar: {
    Symbol s;
    s.Kind = SymbolKind::Value;
    s.Name = d->Name;
    s.D = d;
    s.IsPublic = d->IsPublic;
    declare(s);
    break;
  }
  case NodeKind::TypeAlias: {
    Symbol s;
    s.Kind = SymbolKind::TypeName;
    s.Name = d->Name;
    s.D = d;
    s.IsPublic = d->IsPublic;
    declare(s);
    break;
  }
  case NodeKind::Extern: {
    auto *e = cast<ExternDecl>(d);
    for (auto &f : e->Functions) {
      f->ModulePath = e->ModulePath;
      collectDecl(f.get(), scope);
    }
    for (auto &g : e->Globals) {
      g->ModulePath = e->ModulePath;
      collectDecl(g.get(), scope);
    }
    break;
  }
  default:
    break; // binds, extends and imports are handled in later passes
  }
}

namespace {
/// Flags every function a declaration owns as coming from a library.
void markImported(Decl *d) {
  if (auto *fn = dyn_cast<FunctionDecl>(d)) {
    fn->IsImported = true;
    return;
  }
  if (auto *nd = dyn_cast<NominalDecl>(d)) {
    for (auto &m : nd->Methods)
      m->IsImported = true;
    return;
  }
  if (auto *b = dyn_cast<BindDecl>(d)) {
    for (auto &m : b->Methods)
      m->IsImported = true;
    return;
  }
  if (auto *e = dyn_cast<ExtendDecl>(d)) {
    for (auto &m : e->Methods)
      m->IsImported = true;
    return;
  }
}
} // namespace

void Sema::collectModule(Module *m) {
  CurModule = m;
  Scope *ms = scopeForModule(m);
  for (auto &d : m->Decls) {
    d->ModulePath = m->Name;
    if (m->FromLibrary)
      markImported(d.get());
    collectDecl(d.get(), ms);
  }
}

void Sema::findLangItems() {
  auto findEnum = [&](const char *moduleName, const char *typeName) -> EnumDecl * {
    auto mit = ModulesByName.find(moduleName);
    if (mit == ModulesByName.end())
      return nullptr;
    Scope *ms = scopeForModule(mit->second);
    if (!ms)
      return nullptr;
    Symbol *sym = ms->findLocal(typeName);
    return sym && sym->D ? dyn_cast<EnumDecl>(sym->D) : nullptr;
  };

  auto findMark = [&](const char *moduleName, const char *name) -> MarkDecl * {
    auto mit = ModulesByName.find(moduleName);
    if (mit == ModulesByName.end())
      return nullptr;
    Scope *ms = scopeForModule(mit->second);
    if (!ms)
      return nullptr;
    Symbol *sym = ms->findLocal(name);
    return sym && sym->D ? dyn_cast<MarkDecl>(sym->D) : nullptr;
  };

  // `into` dispatches through `As`, so Sema has to know which mark that is.
  AsDecl = findMark("std::convert", "As");
  // `for` drives `Iterator`, and asks `Sequence` for one when what it was
  // given is a container rather than a cursor over it.
  IteratorDecl = findMark("std::iter", "Iterator");
  SequenceDecl = findMark("std::iter", "Sequence");

  // `Send` and `Sync` are marks nobody binds: the compiler works out which
  // types satisfy them from what those types are made of, so a struct of
  // scalars is `Send` without anyone saying so, and adding a class field
  // takes it away again.
  SendDecl = findMark("std::thread", "Send");
  SyncDecl = findMark("std::thread", "Sync");

  // `[K:V]` is written as sugar for this one.
  if (auto mit = ModulesByName.find("std::dictionary");
      mit != ModulesByName.end())
    if (Scope *ms = scopeForModule(mit->second))
      if (Symbol *sym = ms->findLocal("Map"))
        MapDecl = sym->D ? dyn_cast<NominalDecl>(sym->D) : nullptr;

  OptionDecl = findEnum("std::option", "Option");
  ResultDecl = findEnum("std::result", "Result");
  if (OptionDecl)
    OptionDecl->Lang = LangItem::Option;
  if (ResultDecl)
    ResultDecl->Lang = LangItem::Result;

  // Option and Result are part of the language, not just the library: `T?`,
  // `nil` and `?` all lower onto them. Put them, and their variants, in every
  // module's scope so no import is needed. `insert` rather than `overwrite`,
  // so a module that declares its own `Some` still wins.
  auto preludeInto = [&](Scope *scope) {
    auto addType = [&](EnumDecl *e) {
      if (!e)
        return;
      Symbol sym;
      sym.Kind = SymbolKind::TypeName;
      sym.Name = e->Name;
      sym.D = e;
      sym.IsPublic = true;
      scope->insert(sym);
      for (const auto &v : e->Variants) {
        Symbol vs;
        vs.Kind = SymbolKind::Variant;
        vs.Name = v->Name;
        vs.D = v.get();
        vs.Owner = e;
        vs.VariantIndex = static_cast<int>(v->Index);
        vs.IsPublic = true;
        scope->addVariant(vs, /*fromPrelude=*/true);
      }
    };
    addType(OptionDecl);
    addType(ResultDecl);
    // `As` is in scope everywhere too, so a binding never needs an import.
    // The same goes for `Iterator` and `Sequence`: `for` is syntax, so what
    // it dispatches through has to be nameable without ceremony.
    auto addMark = [&](MarkDecl *mk) {
      if (!mk)
        return;
      Symbol sym;
      sym.Kind = SymbolKind::MarkName;
      sym.Name = mk->Name;
      sym.D = mk;
      sym.IsPublic = true;
      scope->insert(sym);
    };
    addMark(AsDecl);
    addMark(IteratorDecl);
    addMark(SequenceDecl);
  };
  for (Module *m : Modules)
    if (Scope *ms = scopeForModule(m))
      preludeInto(ms);
}

Type *Sema::optionOf(Type *element, SourceRange range) {
  if (!OptionDecl) {
    Diags.error(range, "`Option` is unavailable")
        .note("`T?`, `nil` and `?` are sugar over std::option::Option; this "
              "build was compiled with --no-stdlib")
        .code(266);
    return Types.errorType();
  }
  if (!element)
    return Types.errorType();
  // Inside a generic template the argument is still symbolic, so keep the
  // parameterised form and instantiate once it becomes concrete.
  if (element->containsGenericParam())
    return Types.nominalOf(static_cast<NominalDecl *>(OptionDecl), {element});
  NominalDecl *inst = instantiateNominal(static_cast<NominalDecl *>(OptionDecl),
                                         {element}, range);
  return inst ? inst->DeclaredType : Types.errorType();
}

void Sema::resolveImports(Module *m) {
  CurModule = m;
  Scope *ms = scopeForModule(m);
  for (auto &d : m->Decls) {
    auto *imp = dyn_cast<ImportDecl>(d.get());
    if (!imp)
      continue;
    std::string path;
    for (size_t i = 0; i < imp->Path.size(); ++i) {
      if (i) path += "::";
      path += imp->Path[i];
    }
    auto it = ModulesByName.find(path);
    if (it == ModulesByName.end()) {
      // `std` is not a module of its own — it is the prefix `std::io`,
      // `std::mem` and the rest live under. An import that names a prefix
      // brings in what is beneath it.
      const std::string prefix = path + "::";
      std::vector<std::pair<std::string, Module *>> children;
      for (const auto &entry : ModulesByName) {
        if (entry.first.compare(0, prefix.size(), prefix) != 0)
          continue;
        // Direct children only: `std::io`, not `std::structures::collections`.
        std::string tail = entry.first.substr(prefix.size());
        if (tail.find("::") != std::string::npos)
          continue;
        children.push_back({tail, entry.second});
      }
      if (!children.empty()) {
        auto bind = [&](const std::string &name, Module *mod) {
          Symbol s;
          s.Kind = SymbolKind::ModuleName;
          s.Name = name;
          s.Mod = mod;
          s.D = imp;
          s.IsPublic = imp->IsPublic;
          ms->overwrite(s);
          m->Imports.push_back(mod);
        };
        if (!imp->Names.empty()) {
          // `import std::{io, mem}`
          for (const std::string &want : imp->Names) {
            bool found = false;
            for (const auto &c : children)
              if (c.first == want) { bind(want, c.second); found = true; }
            if (found)
              continue;
            // The name may itself be a prefix — `std::collections` holds
            // `std::collections::vector` rather than being a module of its
            // own. Say which spellings do exist.
            const std::string deeper = path + "::" + want + "::";
            std::string under;
            for (const auto &entry : ModulesByName)
              if (entry.first.compare(0, deeper.size(), deeper) == 0) {
                std::string tail = entry.first.substr(deeper.size());
                if (tail.find("::") == std::string::npos)
                  under += (under.empty() ? "" : ", ") + tail;
              }
            auto d = Diags.error(imp->Range, "'{}' has no module named '{}'",
                                 path, want);
            if (!under.empty())
              d.note(fmt("'{}::{}' is itself a prefix — import what is under "
                         "it: {}", path, want, under)
                         .c_str());
            else
              d.note("it is a prefix, so what follows it has to be one of the "
                     "modules underneath");
            d.code(202);
          }
          continue;
        }
        // `import std` and `import std::*` both bring the modules beneath it
        // into scope under their short names. A fully qualified `std::io::x`
        // resolves without any import regardless.
        for (const auto &c : children)
          bind(c.first, c.second);
        continue;
      }
      auto b = Diags.error(imp->Range, "cannot find module '{}'", path);
      b.note("modules are found in the package's src/ directory or in a "
             "dependency listed in Rune.toml")
          .code(201);
      continue;
    }
    Module *target = it->second;
    imp->ResolvedModule = target;
    m->Imports.push_back(target);
    Scope *ts = scopeForModule(target);
    if (!ts)
      continue;

    if (imp->IsGlob) {
      for (const auto &entry : ts->all())
        if (entry.second.IsPublic)
          ms->overwrite(entry.second);
      // A glob brings the bare variant names too, which is what makes
      // `import site::Bind::*` enough to write `AF_INET`. They arrive as
      // candidates rather than declarations, so importing two modules that
      // each have a `Wheel` is not an error until one is written bare.
      for (const auto &named : ts->allVariants())
        for (const auto &entry : named.second)
          if (entry.Sym.IsPublic && !entry.FromPrelude)
            ms->addVariant(entry.Sym);
      continue;
    }
    if (!imp->Names.empty()) {
      for (const std::string &name : imp->Names) {
        Symbol *sym = ts->findLocal(name);
        if (sym && sym->IsPublic) {
          ms->overwrite(*sym);
          // Naming an enum brings its variants' bare names with it.
          if (auto *e = dyn_cast<EnumDecl>(sym->D))
            for (const auto &v : e->Variants) {
              Symbol vs;
              vs.Kind = SymbolKind::Variant;
              vs.Name = v->Name;
              vs.D = v.get();
              vs.Owner = e;
              vs.VariantIndex = static_cast<int>(v->Index);
              vs.IsPublic = e->IsPublic;
              ms->addVariant(vs);
            }
          continue;
        }
        // `import m::{Wheel}` may be naming a variant rather than a
        // declaration, which is the only way to bring one bare name in
        // without the rest of its enum.
        bool broughtVariant = false;
        if (const auto *list = ts->variantsNamed(name))
          for (const auto &entry : *list)
            if (entry.Sym.IsPublic && !entry.FromPrelude) {
              ms->addVariant(entry.Sym);
              broughtVariant = true;
            }
        if (broughtVariant)
          continue;
        Diags.error(imp->Range, "module '{}' has no public item named '{}'",
                    path, name)
            .note("only declarations marked `pub` are importable")
            .code(202);
      }
      continue;
    }
    // Plain `import a::b` binds the trailing name to the module itself.
    Symbol s;
    s.Kind = SymbolKind::ModuleName;
    s.Name = imp->Name;
    s.Mod = target;
    s.D = imp;
    s.IsPublic = imp->IsPublic;
    ms->overwrite(s);
  }
}

//===----------------------------------------------------------------------===//
// Type resolution
//===----------------------------------------------------------------------===//

std::vector<Symbol> Sema::bareVariants(const std::string &name) {
  std::vector<Symbol> own, prelude;
  auto add = [](std::vector<Symbol> &into, const Symbol &s) {
    for (const Symbol &have : into)
      if (have.D == s.D)
        return;
    into.push_back(s);
  };
  for (Scope *sc = CurScope; sc; sc = sc->parent())
    if (const auto *list = sc->variantsNamed(name))
      for (const auto &entry : *list)
        add(entry.FromPrelude ? prelude : own, entry.Sym);
  return own.empty() ? prelude : own;
}

void Sema::reportAmbiguousVariant(const std::string &name, SourceRange range,
                                  const std::vector<Symbol> &candidates) {
  std::string spellings;
  for (const Symbol &c : candidates) {
    if (!spellings.empty())
      spellings += ", ";
    spellings += (c.Owner ? static_cast<Decl *>(c.Owner)->Name : std::string()) +
                 "::" + name;
  }
  auto d = Diags.error(range, "'{}' names a variant of more than one enum",
                       name);
  d.note(fmt("write which one is meant: {}", spellings).c_str());
  d.note("a bare variant name is a convenience; it is available only while "
         "one enum claims it");
  d.code(207);
  for (const Symbol &c : candidates)
    if (c.Owner)
      noteDeclaredAt(d, static_cast<Decl *>(c.Owner), "declared here",
                     "qualify the mention with this enum's name");
}

Symbol *Sema::lookupPath(const std::vector<std::string> &path, SourceRange range,
                         bool quiet) {
  if (path.empty())
    return nullptr;
  // `Self` names the type currently being defined or extended.
  if (path[0] == "Self" && ActiveSelfType && ActiveSelfType->isNominal()) {
    Symbol &selfSym = SyntheticSymbols.emplace_back();
    selfSym.Kind = SymbolKind::TypeName;
    selfSym.Name = "Self";
    selfSym.D = static_cast<Decl *>(ActiveSelfType->nominal());
    selfSym.IsPublic = true;
    if (path.size() == 1)
      return &selfSym;
    std::vector<std::string> rest = path;
    rest[0] = static_cast<Decl *>(ActiveSelfType->nominal())->Name;
    Symbol *found = nullptr;
    // Fall through to the normal walk using the concrete name.
    Symbol *base = &selfSym;
    for (size_t i = 1; i < rest.size(); ++i) {
      if (auto *e = base->D ? dyn_cast<EnumDecl>(base->D) : nullptr) {
        for (const auto &v : e->Variants)
          if (v->Name == rest[i]) {
            Symbol &variantSym = SyntheticSymbols.emplace_back();
            variantSym.Kind = SymbolKind::Variant;
            variantSym.Name = v->Name;
            variantSym.D = v.get();
            variantSym.Owner = e;
            variantSym.VariantIndex = static_cast<int>(v->Index);
            variantSym.IsPublic = e->IsPublic;
            found = &variantSym;
          }
      }
      if (!found && base->D) {
        if (auto *nd = dyn_cast<NominalDecl>(base->D)) {
          auto tit = nd->DeclaredType ? Methods.find(nd->DeclaredType)
                                      : Methods.end();
          if (tit != Methods.end()) {
            auto mit = tit->second.find(rest[i]);
            if (mit != tit->second.end()) {
              Symbol &methodSym = SyntheticSymbols.emplace_back();
              methodSym.Kind = SymbolKind::Function;
              methodSym.Name = rest[i];
              methodSym.D = mit->second;
              methodSym.IsPublic = mit->second->IsPublic;
              found = &methodSym;
            }
          }
        }
      }
      if (!found) {
        if (!quiet)
          Diags.error(range, "'Self' has no member named '{}'", rest[i])
              .code(204);
        return nullptr;
      }
      base = found;
      found = nullptr;
    }
    return base;
  }

  Symbol *sym = CurScope->find(path[0]);
  if (!sym) {
    // A fully qualified path needs no import: `std::io::println(...)` names
    // exactly one thing whether or not the file said `import std::io`. Take
    // the longest prefix of the path that is a loaded module and carry on
    // from inside it.
    for (size_t take = path.size(); take >= 1 && !sym; --take) {
      std::string prefix;
      for (size_t i = 0; i < take; ++i) {
        if (i) prefix += "::";
        prefix += path[i];
      }
      auto mit = ModulesByName.find(prefix);
      if (mit == ModulesByName.end())
        continue;
      Symbol &modSym = SyntheticSymbols.emplace_back();
      modSym.Kind = SymbolKind::ModuleName;
      modSym.Name = path[take - 1];
      modSym.Mod = mit->second;
      modSym.IsPublic = true;
      sym = &modSym;
      // Continue the walk from just past the prefix.
      std::vector<std::string> rest(path.begin() + static_cast<long>(take),
                                    path.end());
      if (rest.empty())
        return sym;
      Symbol *cur = sym;
      for (const std::string &seg : rest) {
        Scope *ts = cur->Mod ? scopeForModule(cur->Mod) : nullptr;
        Symbol *next = ts ? ts->findLocal(seg) : nullptr;
        if (!next || !next->IsPublic) {
          cur = nullptr;
          break;
        }
        cur = next;
      }
      if (cur)
        return cur;
      sym = nullptr;
    }
  }
  // Nothing declared answers to this name. A single enum variant may still,
  // which is the bare `None` / `AF_INET` spelling; several mean the mention
  // has to say which enum it belongs to.
  if (!sym && path.size() == 1) {
    std::vector<Symbol> candidates = bareVariants(path[0]);
    if (candidates.size() == 1) {
      Symbol &variantSym = SyntheticSymbols.emplace_back();
      variantSym = candidates.front();
      return &variantSym;
    }
    if (candidates.size() > 1) {
      if (!quiet)
        reportAmbiguousVariant(path[0], range, candidates);
      return nullptr;
    }
  }
  if (!sym) {
    if (!quiet) {
      auto d = Diags.error(range, "cannot find '{}' in this scope", path[0]);
      if (path.size() == 1 && path[0] == "move")
        d.note("`move(...)` calls a function named `move`; to hand a value "
               "on, write `move value` or `value.$move()`");
      else
        d.note("check the spelling, or add an `import` for the module that "
               "declares it");
      d.code(203);
    }
    return nullptr;
  }
  for (size_t i = 1; i < path.size(); ++i) {
    if (sym->Kind == SymbolKind::ModuleName && sym->Mod) {
      Scope *ts = scopeForModule(sym->Mod);
      Symbol *next = ts ? ts->findLocal(path[i]) : nullptr;
      if (!next || !next->IsPublic) {
        // A submodule is not an item *inside* its parent, it is a module of
        // its own with a longer name. `json::io` is a module even though
        // `json` declares nothing called `io`, so look the whole path up
        // before deciding there is nothing there.
        std::string full = sym->Mod->Name;
        for (size_t j = i; j < path.size(); ++j)
          full += "::" + path[j];
        for (size_t take = path.size(); take > i; --take) {
          std::string prefix = sym->Mod->Name;
          for (size_t j = i; j < take; ++j)
            prefix += "::" + path[j];
          auto mit = ModulesByName.find(prefix);
          if (mit == ModulesByName.end())
            continue;
          Symbol &modSym = SyntheticSymbols.emplace_back();
          modSym.Kind = SymbolKind::ModuleName;
          modSym.Name = path[take - 1];
          modSym.Mod = mit->second;
          modSym.IsPublic = true;
          sym = &modSym;
          i = take - 1;
          next = sym;
          break;
        }
        if (next != sym || sym->Kind != SymbolKind::ModuleName) {
          if (!quiet) {
            auto d = Diags.error(range,
                                 "module '{}' has no public item named '{}'",
                                 sym->Name, path[i]);
            d.note("only declarations marked `pub` are visible outside their "
                   "module");
            d.code(202);
          }
          return nullptr;
        }
        continue;
      }
      sym = next;
      continue;
    }
    // `Enum::Variant`
    if (auto *e = sym->D ? dyn_cast<EnumDecl>(sym->D) : nullptr) {
      bool found = false;
      for (const auto &v : e->Variants) {
        if (v->Name != path[i])
          continue;
        Symbol &variantSym = SyntheticSymbols.emplace_back();
        variantSym.Kind = SymbolKind::Variant;
        variantSym.Name = v->Name;
        variantSym.D = v.get();
        variantSym.Owner = e;
        variantSym.VariantIndex = static_cast<int>(v->Index);
        variantSym.IsPublic = e->IsPublic;
        sym = &variantSym;
        found = true;
        break;
      }
      if (found)
        continue;
    }
    // `Type::staticMethod`
    if (auto *nd = sym->D ? dyn_cast<NominalDecl>(sym->D) : nullptr) {
      auto tit = nd->DeclaredType ? Methods.find(nd->DeclaredType)
                                  : Methods.end();
      if (tit != Methods.end()) {
        auto mit = tit->second.find(path[i]);
        if (mit != tit->second.end()) {
          Symbol &methodSym = SyntheticSymbols.emplace_back();
          methodSym.Kind = SymbolKind::Function;
          methodSym.Name = path[i];
          methodSym.D = mit->second;
          methodSym.IsPublic = mit->second->IsPublic;
          sym = &methodSym;
          continue;
        }
      }
    }
    if (!quiet)
      Diags.error(range, "'{}' has no member named '{}'", sym->Name, path[i])
          .code(204);
    return nullptr;
  }
  return sym;
}

/// The type an array length names, when it names one. `[String:i64]` is a
/// map; `[3:i64]` and `[SIZE:i64]` are arrays, because neither `3` nor a
/// global constant is a type.
//===----------------------------------------------------------------------===//
// Inheriting a shape
//
// `struct Derived : Base` and `enum Derived : Base` put the parent's members
// at the *front* of the child's, which is what makes the child readable as
// the parent: the bytes a `Base` occupies are the first bytes of a `Derived`,
// and a variant of `Base` keeps the number it had.
//
// A class does this differently — it keeps the chain and walks it — because a
// class instance is reached through a pointer and never copied. A value has
// no such indirection, so the members are spliced in once, here, and
// everything downstream sees one flat type.
//===----------------------------------------------------------------------===//

void Sema::spliceInheritance(NominalDecl *nd) {
  if (!nd || nd->InheritanceDone)
    return;
  nd->InheritanceDone = true;
  if (!nd->Inherits)
    return;

  auto *named = dyn_cast<NamedTypeRepr>(nd->Inherits.get());
  if (!named) {
    Diags.error(nd->Inherits->Range, "a parent has to be a named type")
        .note("`struct Derived : Base` extends one type, written by name")
        .code(213);
    return;
  }

  // The parent is looked up where the child was *written*, which for a type
  // declared in another module is not the scope this pass happens to be in.
  Scope *savedScope = CurScope;
  Module *savedModule = CurModule;
  auto mit = ModulesByName.find(static_cast<Decl *>(nd)->ModulePath);
  if (mit != ModulesByName.end()) {
    CurModule = mit->second;
    CurScope = scopeForModule(mit->second);
  }
  NominalDecl *parent = lookupNominal(named->Path, named->Range, /*quiet=*/true);
  CurScope = savedScope;
  CurModule = savedModule;

  if (!parent) {
    Diags.error(named->Range, "cannot find '{}' in this scope",
                named->Path.empty() ? std::string("the parent")
                                    : named->Path.back())
        .note("a parent is a struct or an enum declared somewhere this file "
              "can see")
        .code(203);
    return;
  }
  if (parent == nd) {
    Diags.error(named->Range, "'{}' cannot extend itself",
                static_cast<Decl *>(nd)->Name)
        .code(213);
    return;
  }
  if (static_cast<Decl *>(parent)->Kind != static_cast<Decl *>(nd)->Kind) {
    auto d = Diags.error(named->Range,
                         "a {} cannot extend a {}",
                         static_cast<Decl *>(nd)->Kind == NodeKind::Struct
                             ? "struct" : "enum",
                         static_cast<Decl *>(parent)->Kind == NodeKind::Struct
                             ? "struct"
                             : static_cast<Decl *>(parent)->Kind ==
                                       NodeKind::Enum
                                   ? "enum" : "type of another kind");
    d.note("the two have to have the same shape for one to be read as the "
           "other; a class extends a class with `class Derived : Base`")
        .code(213);
    return;
  }
  if (!parent->Generics.empty()) {
    Diags.error(named->Range,
                "'{}' is generic, so it cannot be a parent",
                static_cast<Decl *>(parent)->Name)
        .note("the parent's members are spliced in as they are written, and "
              "nothing here says what its parameters would be")
        .code(213);
    return;
  }

  spliceInheritance(parent);   // the parent's own parent first
  nd->InheritsDecl = parent;

  if (auto *e = dyn_cast<EnumDecl>(static_cast<Decl *>(nd))) {
    auto *pe = cast<EnumDecl>(static_cast<Decl *>(parent));
    std::vector<std::unique_ptr<EnumVariantDecl>> merged;
    for (const auto &v : pe->Variants) {
      auto copy = cloneEnumVariant(v.get());
      copy->Parent = static_cast<Decl *>(e);
      merged.push_back(std::move(copy));
    }
    for (auto &v : e->Variants) {
      for (const auto &existing : merged)
        if (existing->Name == v->Name)
          Diags.error(v->NameRange,
                      "'{}' already has a variant named '{}'",
                      static_cast<Decl *>(parent)->Name, v->Name)
              .note("a child may add variants, not redefine the parent's")
              .code(213);
      merged.push_back(std::move(v));
    }
    e->Variants = std::move(merged);
    for (size_t i = 0; i < e->Variants.size(); ++i)
      e->Variants[i]->Index = static_cast<unsigned>(i);
    e->IsSimple = e->IsSimple && pe->IsSimple;
    // Now that the numbering is settled, the bare names go into scope.
    auto own = ModulesByName.find(static_cast<Decl *>(e)->ModulePath);
    if (own != ModulesByName.end()) {
      if (Scope *scope = scopeForModule(own->second)) {
        for (const auto &v : e->Variants) {
          Symbol vs;
          vs.Kind = SymbolKind::Variant;
          vs.Name = v->Name;
          vs.D = v.get();
          vs.Owner = e;
          vs.VariantIndex = static_cast<int>(v->Index);
          vs.IsPublic = static_cast<Decl *>(e)->IsPublic;
          scope->addVariant(vs);
        }
      }
    }
    return;
  }

  std::vector<std::unique_ptr<FieldDecl>> merged;
  for (const auto &f : parent->Fields) {
    auto copy = cloneFieldDecl(f.get());
    copy->Parent = static_cast<Decl *>(nd);
    merged.push_back(std::move(copy));
  }
  for (auto &f : nd->Fields) {
    for (const auto &existing : merged)
      if (existing->Name == f->Name)
        Diags.error(f->NameRange, "'{}' already has a field named '{}'",
                    static_cast<Decl *>(parent)->Name, f->Name)
            .note("a child may add fields, not redefine the parent's")
            .code(213);
    merged.push_back(std::move(f));
  }
  nd->Fields = std::move(merged);
  for (size_t i = 0; i < nd->Fields.size(); ++i)
    nd->Fields[i]->Index = static_cast<unsigned>(i);
}

Type *Sema::typeWrittenAsSize(Expr *size) {
  if (!size)
    return nullptr;
  auto *ref = dyn_cast<DeclRefExpr>(size);
  if (!ref || ref->Path.empty())
    return nullptr;
  // A type parameter in scope — `[K:V]` inside a generic — is a type before
  // it is anything else.
  if (ref->Path.size() == 1) {
    auto git = ActiveGenericParams.find(ref->Path[0]);
    if (git != ActiveGenericParams.end())
      return git->second;
  }
  // Anything else has to be looked up. A builtin (`i64`, `String`) is not a
  // symbol in scope, so the question is put to the type resolver itself,
  // quietly: a length that turns out not to name a type simply is not one.
  auto named = std::make_unique<NamedTypeRepr>();
  named->Range = ref->Range;
  named->NameRange = ref->Range;
  named->Path = ref->Path;
  for (const auto &ga : ref->GenericArgs)
    named->GenericArgs.push_back(cloneTypeRepr(ga.get()));
  Diags.beginSpeculation();
  Type *resolved = resolveType(named.get());
  Diags.endSpeculation();
  if (!resolved || resolved->isError())
    return nullptr;
  SpeculativeTypeReprs.push_back(std::move(named));
  return resolved;
}

/// `Map<K, V>`, the type `[K:V]` is sugar for.
Type *Sema::mapOf(Type *key, Type *value, SourceRange range) {
  if (!MapDecl) {
    Diags.error(range, "`[K:V]` needs `std::dictionary::Map`")
        .note("a build with `--no-stdlib` has no map to name")
        .code(212);
    return Types.errorType();
  }
  if (!key || key->isError() || !value || value->isError())
    return Types.errorType();
  NominalDecl *inst = instantiateNominal(MapDecl, {key, value}, range);
  return inst && inst->DeclaredType ? inst->DeclaredType : Types.errorType();
}

/// The variant of `expected` called `name`, when `expected` is an enum with
/// one. This is what makes `.Red` work, and what settles a bare `Red` that
/// two enums both claim — most often because one inherits from the other.
Symbol *Sema::variantOfExpected(const std::string &name, Type *expected) {
  if (!expected)
    return nullptr;
  Type *t = expected->canonical();
  while (t && t->is(TypeKind::Pointer) && !t->isRawPointer() &&
         !t->isWeakPointer() && t->pointee())
    t = t->pointee();
  if (!t || t->isError() || !t->isNominal() || !t->nominal())
    return nullptr;
  auto *e = dyn_cast<EnumDecl>(static_cast<Decl *>(t->nominal()));
  if (!e)
    return nullptr;
  for (size_t i = 0; i < e->Variants.size(); ++i) {
    if (e->Variants[i]->Name != name)
      continue;
    Symbol &sym = SyntheticSymbols.emplace_back();
    sym.Kind = SymbolKind::Variant;
    sym.Name = name;
    sym.D = static_cast<Decl *>(e);
    sym.Owner = e;
    sym.VariantIndex = static_cast<int>(i);
    sym.IsPublic = static_cast<Decl *>(e)->IsPublic;
    return &sym;
  }
  return nullptr;
}

Symbol *Sema::resolveInferredPath(DeclRefExpr *ref, Type *expected) {
  if (!ref || !ref->FromInferredType || ref->Path.size() != 1 || !expected)
    return nullptr;
  Type *t = expected->canonical();
  // The context may want a borrow of the type, or an optional of it. A
  // borrow reads through; an optional does not, because `.Some` and the
  // payload's own members are different questions and `Option` answers the
  // first one.
  while (t && t->is(TypeKind::Pointer) && !t->isRawPointer() &&
         !t->isWeakPointer() && t->pointee())
    t = t->pointee();
  if (!t || t->isError() || !t->isNominal())
    return nullptr;
  NominalDecl *nd = t->nominal();
  if (!nd)
    return nullptr;
  const std::string &name = ref->Path[0];

  // A variant of the expected enum: `.Red`, `.Circle(2.0)`.
  if (Symbol *variant = variantOfExpected(name, t))
    return variant;

  // Anything else the type owns: `.seconds(5)`, `.zero`.
  auto tit = nd->DeclaredType ? Methods.find(nd->DeclaredType) : Methods.end();
  if (tit != Methods.end()) {
    auto mit = tit->second.find(name);
    if (mit != tit->second.end()) {
      Symbol &sym = SyntheticSymbols.emplace_back();
      sym.Kind = SymbolKind::Function;
      sym.Name = name;
      sym.D = mit->second;
      sym.IsPublic = mit->second->IsPublic;
      return &sym;
    }
  }
  return nullptr;
}

Symbol *Sema::lookupStaticOnInstantiation(DeclRefExpr *ref) {
  if (!ref || ref->Path.size() < 2 || ref->GenericArgs.empty())
    return nullptr;
  std::vector<std::string> prefix(ref->Path.begin(), ref->Path.end() - 1);
  NominalDecl *nd = lookupNominal(prefix, ref->Range, /*quiet=*/true);
  if (!nd)
    return nullptr;
  if (nd->GenericTemplate)
    nd = nd->GenericTemplate;
  // The arguments are the type's when they fit the type. A method with
  // parameters of its own is written `Type::method::<T>()`, and its own
  // count is what fits there.
  if (nd->Generics.size() != ref->GenericArgs.size())
    return nullptr;
  Type *instType = typeOfNominal(nd, ref->GenericArgs, ref->Range);
  if (!instType || instType->isError() || !instType->isNominal())
    return nullptr;
  const std::string &name = ref->Path.back();
  auto tit = Methods.find(instType);
  if (tit == Methods.end())
    return nullptr;
  auto mit = tit->second.find(name);
  if (mit == tit->second.end())
    return nullptr;
  Symbol &methodSym = SyntheticSymbols.emplace_back();
  methodSym.Kind = SymbolKind::Function;
  methodSym.Name = name;
  methodSym.D = mit->second;
  methodSym.IsPublic = mit->second->IsPublic;
  // Spent on the type; the method has none of its own to fill.
  ref->GenericArgs.clear();
  return &methodSym;
}

NominalDecl *Sema::lookupNominal(const std::vector<std::string> &path,
                                 SourceRange range, bool quiet) {
  Symbol *sym = lookupPath(path, range, quiet);
  if (!sym || !sym->D)
    return nullptr;
  return dyn_cast<NominalDecl>(sym->D);
}

Type *Sema::typeOfNominal(NominalDecl *nd, const std::vector<TypeReprPtr> &args,
                          SourceRange range) {
  // Inside an instantiation, the type's own name refers to that instantiation,
  // which is what lets `Option::Some(v)` work in Option's own methods. But
  // written *with* arguments — `Pair<B, A>` inside `Pair<A, B>` — the name
  // means the template again, so redirect before anything else.
  if (nd->GenericTemplate && !args.empty())
    nd = nd->GenericTemplate;

  if (nd->Generics.empty()) {
    if (!args.empty())
      Diags.error(range, "'{}' takes no generic arguments",
                  static_cast<Decl *>(nd)->Name)
          .code(205);
    return nd->DeclaredType ? nd->DeclaredType : Types.nominalOf(nd);
  }
  if (args.size() != nd->Generics.size()) {
    auto b = Diags.error(range,
                         "'{}' expects {} generic argument(s) — {} given",
                         static_cast<Decl *>(nd)->Name, nd->Generics.size(),
                         args.size());
    b.code(206);
    noteDeclaredAt(b, static_cast<Decl *>(nd), "declared here",
                   "the parameter list must match exactly");
    return Types.errorType();
  }
  std::vector<Type *> resolved;
  for (size_t i = 0; i < args.size(); ++i) {
    Type *arg = resolveTypeOrError(args[i].get(), Types.errorType());
    // A type argument is a *type*, and a mark is not one: `Box<Node>` cannot
    // be laid out, because every type carrying `Node` is a different size.
    // Refusing it here keeps the mistake from surfacing far away, as a
    // missing `$clone` somewhere inside the container's own source.
    if (arg->is(TypeKind::Mark) && arg != ActiveSelfType) {
      const std::string role =
          fmt("a type argument of '{}'", static_cast<Decl *>(nd)->Name);
      rejectMarkByValue(arg, args[i]->Range, role.c_str());
      arg = Types.errorType();
    }
    resolved.push_back(arg);
  }

  bool anyGeneric = false;
  for (Type *t : resolved)
    if (t->containsGenericParam())
      anyGeneric = true;
  // Inside an uninstantiated template the arguments are still symbolic; keep
  // the parameterised form and instantiate once they become concrete.
  if (anyGeneric)
    return Types.nominalOf(nd, resolved);

  NominalDecl *inst = instantiateNominal(nd, resolved, range);
  return inst ? Types.nominalOf(inst, resolved) : Types.errorType();
}

Type *Sema::resolveType(TypeRepr *repr) {
  if (!repr)
    return nullptr;
  if (repr->Resolved)
    return repr->Resolved;

  Type *result = Types.errorType();
  switch (repr->Kind) {
  case NodeKind::InferType:
    return nullptr; // caller infers
  case NodeKind::SelfType:
    if (ActiveSelfType)
      result = ActiveSelfType;
    else
      Diags.error(repr->Range, "`Self` is only meaningful inside a type, mark "
                               "or bind declaration")
          .code(207);
    break;
  case NodeKind::NamedType: {
    auto *n = cast<NamedTypeRepr>(repr);
    if (n->Path.size() == 1) {
      // Generic parameters shadow everything else.
      auto git = ActiveGenericParams.find(n->Path[0]);
      if (git != ActiveGenericParams.end()) {
        if (!n->GenericArgs.empty())
          Diags.error(repr->Range,
                      "generic parameter '{}' cannot take arguments", n->Path[0])
              .code(208);
        result = git->second;
        break;
      }
      if (n->Path[0] == "Self" && ActiveSelfType) {
        result = ActiveSelfType;
        break;
      }
      // `Unique<T>` — a class reference with exactly one owner. It is not a
      // generic type in the ordinary sense: it wraps the class rather than
      // being instantiated from a template, so it is resolved here.
      if (n->Path[0] == "Unique") {
        if (n->GenericArgs.size() != 1) {
          Diags.error(repr->Range,
                      "`Unique` takes one type argument — `Unique<Node>`")
              .code(238);
          result = Types.errorType();
          break;
        }
        Type *inner =
            resolveTypeOrError(n->GenericArgs[0].get(), Types.errorType());
        if (inner->is(TypeKind::Class)) {
          result = Types.uniqOf(inner);
        } else if (!inner->isError()) {
          Diags.error(repr->Range,
                      "`Unique` requires a class, but '{}' is not one",
                      inner->toString())
              .note("only a class instance is reference counted, so only one "
                    "can have a single owner to hand over")
              .code(238);
          result = inner;
        }
        break;
      }
      if (Type *builtin = Types.builtinNamed(n->Path[0])) {
        if (!n->GenericArgs.empty())
          Diags.error(repr->Range, "'{}' is not a generic type", n->Path[0])
              .code(208);
        result = builtin;
        break;
      }
      // `c_int`, `c_long`, `c_size_t`...: C++'s scalars, sized for the
      // target. An alias of a Rune type everywhere but in an `extern "C++"`
      // signature, where the name also decides how the parameter mangles.
      if (Type *scalar = cxxScalarType(n->Path[0], Cxx, Types)) {
        if (!n->GenericArgs.empty())
          Diags.error(repr->Range, "'{}' is not a generic type", n->Path[0])
              .code(208);
        result = scalar;
        break;
      }
    }
    // `Self::Item`, `T::Item`, `Self::Iter::Item` — associated types the
    // binding chose. The owner may be `Self`, a generic parameter, or a
    // concrete type, and each segment projects off the one before it.
    if (n->Path.size() >= 2) {
      Type *owner = nullptr;
      bool ownerIsParameter = false;
      if (n->Path[0] == "Self") {
        owner = ActiveSelfType;
        ownerIsParameter = true;
      } else {
        auto git = ActiveGenericParams.find(n->Path[0]);
        if (git != ActiveGenericParams.end()) {
          owner = git->second;
          ownerIsParameter = true;
        } else if (Symbol *os = lookupPath({n->Path[0]}, repr->Range, true)) {
          if (auto *nd = os->D ? dyn_cast<NominalDecl>(os->D) : nullptr)
            owner = nd->DeclaredType;
        }
      }

      // Projects one name off `owner`, or returns null when it cannot.
      auto project = [&](Type *from, const std::string &name) -> Type * {
        if (!from)
          return nullptr;
        // Inside the mark itself there is no binding to ask, so the type
        // stays symbolic until a `bind` copies the method and gives it a real
        // owner. The same goes for a parameter that is not yet substituted.
        if (from->is(TypeKind::Mark)) {
          auto *mk = reinterpret_cast<MarkDecl *>(from->nominal());
          for (auto &at : mk->AssociatedTypes)
            if (at->Name == name)
              return Types.genericParam("Self::" + name, 0);
          return nullptr;
        }
        if (from->isGeneric())
          return Types.genericParam(from->genericName() + "::" + name, 0);
        auto cit = Conformances.find(from);
        if (cit != Conformances.end())
          for (MarkDecl *mk : cit->second)
            if (Type *chosen = associatedTypeFor(from, mk, name))
              return chosen;
        return nullptr;
      };

      Type *projected = owner;
      for (size_t i = 1; projected && i < n->Path.size(); ++i)
        projected = project(projected, n->Path[i]);
      if (projected) {
        result = projected;
        break;
      }
      // A path rooted at `Self` or a parameter can only ever have been a
      // projection, so say what is missing rather than falling through to
      // "unknown type". Anything else may still be a module-qualified name.
      if (ownerIsParameter && owner) {
        auto d = Diags.error(repr->Range, "'{}' has no associated type '{}'",
                             owner->toString(), n->Path[1]);
        d.note(fmt("an associated type comes from a `bind`: `type {} = ...` "
                   "in the binding that gives this type the mark",
                   n->Path[1])
                   .c_str())
            .code(247);
        break;
      }
    }

    Symbol *sym = lookupPath(n->Path, n->NameRange.isValid() ? n->NameRange
                                                             : repr->Range,
                             /*quiet=*/true);
    if (!sym || !sym->D) {
      std::string joined;
      for (size_t i = 0; i < n->Path.size(); ++i) {
        if (i) joined += "::";
        joined += n->Path[i];
      }
      Diags.error(repr->Range, "unknown type '{}'", joined)
          .note("builtin types are i8..i64, u8..u64, isize, usize, f32, f64, "
                "bool, Character, CString, String and Any")
          .code(209);
      break;
    }
    if (auto *alias = dyn_cast<TypeAliasDecl>(sym->D)) {
      // What an alias stands for is written in the module the alias was
      // declared in, and names what *that* module imported. Resolving it in
      // whichever module happens to mention the alias first would ask the
      // wrong scope — `pub type Value = value::Value` in a library means
      // nothing to a package that imported the library but not the module
      // `value` is short for.
      Module *owner = nullptr;
      if (!alias->ModulePath.empty()) {
        auto mit = ModulesByName.find(alias->ModulePath);
        if (mit != ModulesByName.end())
          owner = mit->second;
      }
      auto inOwnerScope = [&](auto &&resolve) {
        Module *wasModule = CurModule;
        Scope *wasScope = CurScope;
        if (owner)
          if (Scope *os = scopeForModule(owner)) {
            CurModule = owner;
            CurScope = os;
          }
        Type *t = resolve();
        CurModule = wasModule;
        CurScope = wasScope;
        return t;
      };

      // `type Gen<T> = [T]` stands for a different type every time it is
      // written with different arguments, so it is resolved per use with its
      // parameters bound — and on a *copy* of what it aliases, because the
      // annotation itself caches what it resolved to and one answer would be
      // handed to every other use.
      if (!alias->Generics.empty()) {
        if (n->GenericArgs.size() != alias->Generics.size()) {
          auto d = Diags.error(repr->Range,
                               "'{}' takes {} generic argument(s) — {} given",
                               sym->Name, alias->Generics.size(),
                               n->GenericArgs.size());
          d.code(206);
          noteDeclaredAt(d, alias, "declared here",
                         "an alias is written with the same number of "
                         "arguments it declares");
          break;
        }
        std::vector<Type *> args;
        for (const auto &ga : n->GenericArgs)
          args.push_back(resolveTypeOrError(ga.get(), Types.errorType()));
        auto savedGenerics = ActiveGenericParams;
        // Only the alias's own parameters are in scope inside it: a `T` from
        // the function that wrote `Gen<T>` is not the alias's `T`.
        ActiveGenericParams.clear();
        for (size_t i = 0; i < args.size(); ++i)
          ActiveGenericParams[alias->Generics[i].Name] = args[i];
        TypeReprPtr copy = cloneTypeRepr(alias->Aliased.get());
        result = inOwnerScope(
            [&] { return resolveTypeOrError(copy.get(), Types.errorType()); });
        ActiveGenericParams = savedGenerics;
        break;
      }
      if (!n->GenericArgs.empty()) {
        auto d = Diags.error(repr->Range, "'{}' takes no generic arguments",
                             sym->Name);
        d.code(206);
        noteDeclaredAt(d, alias, "declared here",
                       "this alias declares no parameters to fill in");
        break;
      }
      if (!alias->Resolved)
        alias->Resolved = inOwnerScope([&] {
          return resolveTypeOrError(alias->Aliased.get(), Types.errorType());
        });
      result = alias->Resolved;
      break;
    }
    auto *nd = dyn_cast<NominalDecl>(sym->D);
    if (!nd) {
      auto b = Diags.error(repr->Range, "'{}' is not a type", sym->Name);
      b.code(210);
      noteDeclaredAt(b, sym->D, "declared here", "this names a value, not a type");
      break;
    }
    checkAvailableUnderZombie(sym->D, repr->Range);
    if (false) {
      break;
    }
    result = typeOfNominal(nd, n->GenericArgs, repr->Range);
    break;
  }
  case NodeKind::PointerType: {
    auto *p = cast<PointerTypeRepr>(repr);
    Type *pointee = resolveTypeOrError(p->Pointee.get(), Types.errorType());
    result = Types.pointerTo(pointee, p->IsMutable, p->IsRaw, p->IsWeak);
    break;
  }
  case NodeKind::OptionalType: {
    Type *inner = resolveTypeOrError(cast<OptionalTypeRepr>(repr)->Element.get(),
                                     Types.errorType());
    // `T??` would be a distinct type; collapse it, since the shorthand is only
    // ever meant to express "may be absent".
    result = isOptionType(inner) ? inner : optionOf(inner, repr->Range);
    break;
  }
  case NodeKind::SliceType:
    result = Types.sliceOf(
        resolveTypeOrError(cast<SliceTypeRepr>(repr)->Element.get(),
                           Types.errorType()));
    break;
  case NodeKind::ArrayType: {
    auto *a = cast<ArrayTypeRepr>(repr);
    // `[K:V]` where the part before the colon names a *type* is a map from
    // keys to values, not an array of that many elements: an array's length
    // is a number, and a number is never a type. Which one was meant is
    // therefore decided by what the name means, not by how it is written.
    if (Type *key = typeWrittenAsSize(a->Size.get())) {
      result = mapOf(key, resolveTypeOrError(a->Element.get(),
                                             Types.errorType()),
                     repr->Range);
      break;
    }
    Type *elem = resolveTypeOrError(a->Element.get(), Types.errorType());
    uint64_t size = 0;
    int64_t folded = 0;
    if (a->Size && evalConstInt(a->Size.get(), folded)) {
      if (folded < 0) {
        Diags.error(a->Size->Range, "array length cannot be negative")
            .note(fmt("this expression folds to {}", folded).c_str())
            .code(211);
      } else {
        size = static_cast<uint64_t>(folded);
      }
    } else if (a->Size) {
      Diags.error(a->Size->Range, "array length must be a constant expression")
          .note("lengths may use literals, arithmetic on them, and immutable "
                "globals; use a slice `[T]` when the length is only known at "
                "run time")
          .code(211);
    }
    result = Types.arrayOf(elem, size);
    break;
  }
  case NodeKind::TupleType: {
    auto *t = cast<TupleTypeRepr>(repr);
    std::vector<Type *> elems;
    for (const auto &e : t->Elements)
      elems.push_back(resolveTypeOrError(e.get(), Types.errorType()));
    result = Types.tupleOf(std::move(elems));
    break;
  }
  case NodeKind::FunctionTypeRepr: {
    auto *f = cast<FunctionTypeReprNode>(repr);
    std::vector<Type *> params;
    for (const auto &p : f->Params)
      params.push_back(resolveTypeOrError(p.get(), Types.errorType()));
    Type *ret = f->ReturnType
                    ? resolveTypeOrError(f->ReturnType.get(), Types.errorType())
                    : Types.voidType();
    result = f->IsCFunction ? Types.cfunctionOf(std::move(params), ret)
                            : Types.functionOf(std::move(params), ret);
    break;
  }
  case NodeKind::UniqType: {
    auto *u = cast<UniqTypeRepr>(repr);
    Type *inner = resolveTypeOrError(u->Element.get(), Types.errorType());
    if (inner->is(TypeKind::Class)) {
      result = Types.uniqOf(inner);
    } else if (!inner->isError()) {
      Diags.error(repr->Range,
                  "`uniq` requires a class, but '{}' is not one",
                  inner->toString())
          .note("only a class instance is reference counted, so only one can "
                "have a single owner to move around")
          .code(238);
      result = inner;
    }
    break;
  }
  case NodeKind::DynType: {
    auto *d = cast<DynTypeRepr>(repr);
    Type *inner = resolveTypeOrError(d->MarkType.get(), Types.errorType());
    if (inner->is(TypeKind::Mark)) {
      result = Types.dynMarkOf(reinterpret_cast<MarkDecl *>(inner->nominal()));
    } else if (!inner->isError())
      Diags.error(repr->Range, "`dyn` requires a mark, but '{}' is not one",
                  inner->toString())
          .code(212);
    break;
  }
  case NodeKind::TypeOfType: {
    // The expression is checked, never emitted: `typeof` asks what a value
    // would be, not for the value. That is what lets a macro build a
    // signature out of the arguments it was handed.
    auto *t = cast<TypeOfRepr>(repr);
    Type *got = checkExpr(t->Operand.get(), nullptr);
    if (!got || got->isError()) {
      result = Types.errorType();
      break;
    }
    if (got->isVoid() || got->isNever()) {
      Diags.error(repr->Range, "`typeof` needs an expression with a type, "
                               "and this one has '{}'",
                  got->toString())
          .note("a `()` or a `!` names nothing that can be written down")
          .code(212);
      result = Types.errorType();
      break;
    }
    result = got;
    break;
  }
  case NodeKind::SomeType: {
    auto *o = cast<SomeTypeRepr>(repr);
    Type *inner = resolveTypeOrError(o->MarkType.get(), Types.errorType());
    if (inner->isError())
      break;
    if (!inner->is(TypeKind::Mark)) {
      Diags.error(repr->Range, "`some` requires a mark, but '{}' is not one",
                  inner->toString())
          .note("`some Mark` is a result whose concrete type the body "
                "chooses; a type that is already concrete is written as "
                "itself")
          .code(212);
      break;
    }
    auto *mark = reinterpret_cast<MarkDecl *>(inner->nominal());
    // Only a function's own result type may be a `some`: that is where a
    // body exists to say what the type is. Anywhere else there is nothing to
    // decide it — a parameter, a field, a local, or a type nested inside the
    // result.
    if (repr != ResolvingReturnRepr || !ResolvingReturnFn) {
      auto d = Diags.error(repr->Range,
                           "`some {}` is only allowed as a function's result",
                           static_cast<Decl *>(mark)->Name);
      d.note("a `some` is a type the function's body decides, so it has to "
             "be the whole of what the function returns");
      d.note(fmt("for a value that can hold any `{}`, write `dyn {}`",
                 static_cast<Decl *>(mark)->Name,
                 static_cast<Decl *>(mark)->Name)
                 .c_str());
      d.code(214);
      break;
    }
    FunctionDecl *owner = ResolvingReturnFn;
    if (!owner->Body) {
      auto d = Diags.error(repr->Range,
                           "'{}' has no body to decide what its `some {}` is",
                           owner->Name, static_cast<Decl *>(mark)->Name);
      d.note("a mark requirement or a foreign declaration names its result "
             "type outright")
          .code(214);
      break;
    }
    result = Types.opaqueOf(owner, mark);
    OpaqueContext &ctx = OpaqueContexts[result];
    ctx.Fn = owner;
    ctx.SelfType = ActiveSelfType;
    ctx.Generics = ActiveGenericParams;
    ctx.Declared = repr->Range;
    break;
  }
  default:
    break;
  }

  repr->Resolved = result;
  return result;
}

Type *Sema::resolveReturnType(FunctionDecl *fn, Type *selfType) {
  if (!fn->ReturnType)
    return Types.voidType();
  TypeRepr *savedRepr = ResolvingReturnRepr;
  FunctionDecl *savedFn = ResolvingReturnFn;
  Type *savedSelf = ActiveSelfType;
  ResolvingReturnRepr = fn->ReturnType.get();
  ResolvingReturnFn = fn;
  if (selfType)
    ActiveSelfType = selfType;
  Type *t = resolveTypeOrError(fn->ReturnType.get(), Types.errorType());
  ResolvingReturnRepr = savedRepr;
  ResolvingReturnFn = savedFn;
  ActiveSelfType = savedSelf;
  return t;
}

//===----------------------------------------------------------------------===//
// `some Mark`
//===----------------------------------------------------------------------===//

void Sema::resolveOpaqueNow(Type *t, SourceRange at) {
  if (!t || !t->isOpaque() || t->opaqueUnderlying())
    return;
  auto it = OpaqueContexts.find(t);
  if (it == OpaqueContexts.end()) {
    Types.resolveOpaque(t, Types.errorType());
    return;
  }
  OpaqueContext &ctx = it->second;
  FunctionDecl *owner = ctx.Fn;
  if (ctx.Resolving) {
    // The body reached its own result — `fn f() -> some Shape { f() }` — so
    // there is nothing to fix the type from.
    auto d = Diags.error(at, "the result type of '{}' depends on itself",
                         owner->Name);
    d.note("a `some` is fixed by the first value the body returns, and this "
           "one returns itself before anything else")
        .code(215);
    d.related(ctx.Declared, "declared here", "return a concrete value first");
    Types.resolveOpaque(t, Types.errorType());
    return;
  }
  // A template's `some` belongs to no instantiation; only a clone's body is
  // ever checked, and every call reaches the clone's own type.
  if (!owner->Generics.empty()) {
    Types.resolveOpaque(t, Types.errorType());
    return;
  }
  ctx.Resolving = true;
  Scope *savedScope = CurScope;
  Module *savedModule = CurModule;
  auto savedGenerics = ActiveGenericParams;
  Type *savedSelf = ActiveSelfType;
  ActiveGenericParams = ctx.Generics;
  ActiveSelfType = ctx.SelfType;
  ClassDecl *cls = ctx.SelfType && ctx.SelfType->isNominal()
                       ? dyn_cast<ClassDecl>(
                             static_cast<Decl *>(ctx.SelfType->nominal()))
                       : nullptr;
  if (ctx.SelfType)
    pushScope(ScopeKind::TypeBody);
  checkFunction(owner, ctx.SelfType, cls);
  if (ctx.SelfType)
    popScope();
  ActiveGenericParams = savedGenerics;
  ActiveSelfType = savedSelf;
  CurScope = savedScope;
  CurModule = savedModule;
  ctx.Resolving = false;
  // Nothing in the body produced a value — every path diverges, or the body
  // was already reported on. Either way the type has to become something.
  if (!t->opaqueUnderlying())
    Types.resolveOpaque(t, Types.errorType());
}

Type *Sema::seeThroughOpaque(Type *t, SourceRange at) {
  if (!t || !t->isOpaque())
    return t;
  resolveOpaqueNow(t, at);
  return t->opaqueUnderlying() ? t->opaqueUnderlying() : Types.errorType();
}

bool Sema::rejectOpaqueUse(Type *t, SourceRange at, const std::string &what) {
  if (!t || !t->isOpaque())
    return false;
  if (t->isError())
    return true;
  auto d = Diags.error(at, "{} is not available on '{}'", what, t->toString());
  d.note(fmt("a `some` value exposes only what '{}' declares; the type "
             "behind it is the function's own business",
             t->opaqueMark() ? static_cast<Decl *>(t->opaqueMark())->Name
                             : std::string("?"))
             .c_str())
      .code(216);
  return true;
}

Type *Sema::resolveTypeOrError(TypeRepr *repr, Type *fallback) {
  Type *t = resolveType(repr);
  return t ? t : fallback;
}

/// Collects the generic parameter names a declaration introduces.
void bindGenerics(const std::vector<GenericParam> &generics, TypeContext &types,
                  std::map<std::string, Type *> &out) {
  for (size_t i = 0; i < generics.size(); ++i)
    out[generics[i].Name] =
        types.genericParam(generics[i].Name, static_cast<unsigned>(i));
}

//===----------------------------------------------------------------------===//
// Constant folding
//===----------------------------------------------------------------------===//

bool Sema::evalConstInt(const Expr *e, int64_t &out) {
  if (!e)
    return false;
  switch (e->Kind) {
  case NodeKind::IntLit:
    out = static_cast<int64_t>(cast<IntLitExpr>(e)->Value);
    return true;
  case NodeKind::CharLit:
    out = static_cast<int64_t>(cast<CharLitExpr>(e)->Value);
    return true;
  case NodeKind::BoolLit:
    out = cast<BoolLitExpr>(e)->Value ? 1 : 0;
    return true;
  case NodeKind::Cast:
    return evalConstInt(cast<CastExpr>(e)->Operand.get(), out);
  case NodeKind::Unary: {
    const auto *u = cast<UnaryExpr>(e);
    int64_t v = 0;
    if (!evalConstInt(u->Operand.get(), v))
      return false;
    switch (u->Op) {
    case UnaryOp::Neg: out = -v; return true;
    case UnaryOp::BitNot: out = ~v; return true;
    case UnaryOp::Not: out = v ? 0 : 1; return true;
    }
    return false;
  }
  case NodeKind::Binary: {
    const auto *b = cast<BinaryExpr>(e);
    int64_t l = 0, r = 0;
    if (!evalConstInt(b->LHS.get(), l) || !evalConstInt(b->RHS.get(), r))
      return false;
    switch (b->Op) {
    case BinaryOp::Add: out = l + r; return true;
    case BinaryOp::Sub: out = l - r; return true;
    case BinaryOp::Mul: out = l * r; return true;
    case BinaryOp::Div:
      if (r == 0) return false;
      out = l / r;
      return true;
    case BinaryOp::Rem:
      if (r == 0) return false;
      out = l % r;
      return true;
    case BinaryOp::BitAnd: out = l & r; return true;
    case BinaryOp::BitOr: out = l | r; return true;
    case BinaryOp::BitXor: out = l ^ r; return true;
    case BinaryOp::Shl: out = l << r; return true;
    case BinaryOp::Shr: out = l >> r; return true;
    default: return false;
    }
  }
  case NodeKind::DeclRef: {
    const auto *r = cast<DeclRefExpr>(e);
    // An immutable global with a constant initialiser is itself a constant.
    Decl *resolved = r->Resolved;
    if (!resolved) {
      Symbol *sym = lookupPath(r->Path, r->Range, /*quiet=*/true);
      resolved = sym ? sym->D : nullptr;
    }
    if (auto *g = dyn_cast<GlobalVarDecl>(resolved))
      if (!g->IsMutable && g->Init)
        return evalConstInt(g->Init.get(), out);
    return false;
  }
  default:
    return false;
  }
}

//===----------------------------------------------------------------------===//
// Pass 2: shapes
//===----------------------------------------------------------------------===//

namespace {
/// A variant's declared value, when it is a constant: a number, or integer
/// arithmetic on numbers and on the variants declared before it — `1 << 3`,
/// `Read | Write`, `AF_ISO`. Anything else is not a value an enum can have.
struct VariantConstant {
  bool Ok = false;
  bool IsFloat = false;
  int64_t Int = 0;
  double Float = 0;
  std::string Suffix;
};

/// `earlier` answers a bare name, or `Enum::Name`, with the value of a
/// variant already given one; anything it does not know is not a constant.
VariantConstant variantConstant(
    Expr *e,
    const std::function<VariantConstant(const DeclRefExpr *)> &earlier) {
  VariantConstant c;
  if (!e)
    return c;
  if (auto *i = dyn_cast<IntLitExpr>(e)) {
    c.Ok = true;
    c.Suffix = i->Suffix;
    c.Int = static_cast<int64_t>(i->Value);
    if (i->IsNegated)
      c.Int = -c.Int;
    c.Float = static_cast<double>(c.Int);
    c.IsFloat = c.Suffix == "f32" || c.Suffix == "f64";
    return c;
  }
  if (auto *f = dyn_cast<FloatLitExpr>(e)) {
    c.Ok = true;
    c.IsFloat = true;
    c.Suffix = f->Suffix;
    c.Float = f->Value;
    return c;
  }
  if (auto *r = dyn_cast<DeclRefExpr>(e))
    return earlier(r);
  if (auto *u = dyn_cast<UnaryExpr>(e)) {
    VariantConstant v = variantConstant(u->Operand.get(), earlier);
    if (!v.Ok)
      return c;
    if (u->Op == UnaryOp::Neg) {
      v.Int = -v.Int;
      v.Float = -v.Float;
      return v;
    }
    if (u->Op == UnaryOp::BitNot && !v.IsFloat) {
      v.Int = ~v.Int;
      v.Float = static_cast<double>(v.Int);
      return v;
    }
    return c;
  }
  if (auto *b = dyn_cast<BinaryExpr>(e)) {
    VariantConstant l = variantConstant(b->LHS.get(), earlier);
    VariantConstant r = variantConstant(b->RHS.get(), earlier);
    if (!l.Ok || !r.Ok)
      return c;
    c.Ok = true;
    c.Suffix = !l.Suffix.empty() ? l.Suffix : r.Suffix;
    if (l.IsFloat || r.IsFloat) {
      c.IsFloat = true;
      double x = l.IsFloat ? l.Float : static_cast<double>(l.Int);
      double y = r.IsFloat ? r.Float : static_cast<double>(r.Int);
      switch (b->Op) {
      case BinaryOp::Add: c.Float = x + y; break;
      case BinaryOp::Sub: c.Float = x - y; break;
      case BinaryOp::Mul: c.Float = x * y; break;
      case BinaryOp::Div: c.Float = x / y; break;
      default: return VariantConstant{};
      }
      return c;
    }
    // Two's complement arithmetic, as the tag will hold it; a shift past
    // the width, or a division by zero, is not a value.
    uint64_t x = static_cast<uint64_t>(l.Int), y = static_cast<uint64_t>(r.Int);
    switch (b->Op) {
    case BinaryOp::Add: c.Int = static_cast<int64_t>(x + y); break;
    case BinaryOp::Sub: c.Int = static_cast<int64_t>(x - y); break;
    case BinaryOp::Mul: c.Int = static_cast<int64_t>(x * y); break;
    case BinaryOp::Div:
    case BinaryOp::Rem:
      if (r.Int == 0 || (l.Int == INT64_MIN && r.Int == -1))
        return VariantConstant{};
      c.Int = b->Op == BinaryOp::Div ? l.Int / r.Int : l.Int % r.Int;
      break;
    case BinaryOp::BitAnd: c.Int = l.Int & r.Int; break;
    case BinaryOp::BitOr: c.Int = l.Int | r.Int; break;
    case BinaryOp::BitXor: c.Int = l.Int ^ r.Int; break;
    case BinaryOp::Shl:
    case BinaryOp::Shr:
      if (r.Int < 0 || r.Int > 63)
        return VariantConstant{};
      c.Int = b->Op == BinaryOp::Shl ? static_cast<int64_t>(x << r.Int)
                                     : l.Int >> r.Int;
      break;
    default:
      return VariantConstant{};
    }
    c.Float = static_cast<double>(c.Int);
    return c;
  }
  return c;
}
} // namespace

void Sema::assignVariantValues(EnumDecl *e) {
  // Every value is read first, so the kind of enum is known before any
  // variant is given its tag: one float anywhere makes all of them floats.
  std::vector<VariantConstant> values(e->Variants.size());
  bool anyFloat = false;
  bool f32 = false;
  // What each variant before the current one comes to, for a value that
  // names one: written, or one more than the variant before it.
  std::vector<VariantConstant> settled(e->Variants.size());
  size_t current = 0;
  auto earlier = [&](const DeclRefExpr *r) -> VariantConstant {
    const std::string &name = r->Path.empty() ? std::string() : r->Path.back();
    bool qualified = r->Path.size() == 2 && r->Path[0] == e->Name;
    if (r->Path.size() != 1 && !qualified)
      return VariantConstant{};
    for (size_t j = 0; j < current; ++j)
      if (e->Variants[j]->Name == name)
        return settled[j];
    return VariantConstant{};
  };
  for (size_t i = 0; i < e->Variants.size(); ++i) {
    EnumVariantDecl *v = e->Variants[i].get();
    current = i;
    if (!v->Discriminant) {
      // One more than the variant before it, when that one was a number.
      if (i == 0) {
        settled[i].Ok = true;
      } else if (settled[i - 1].Ok && !settled[i - 1].IsFloat) {
        settled[i] = settled[i - 1];
        settled[i].Int += 1;
        settled[i].Float = static_cast<double>(settled[i].Int);
      }
      continue;
    }
    values[i] = variantConstant(v->Discriminant.get(), earlier);
    settled[i] = values[i];
    if (!values[i].Ok) {
      Diags.error(v->Discriminant->Range,
                  "the value of variant '{}' is not a constant number", v->Name)
          .note("a variant's value is a number, or arithmetic on numbers and "
                "the variants before it: `Low = -1`, `Write = 1 << 1`, "
                "`Both = Read | Write`")
          .code(400);
      continue;
    }
    if (values[i].IsFloat) {
      anyFloat = true;
      if (values[i].Suffix == "f32")
        f32 = true;
    }
  }

  if (anyFloat) {
    // A float cannot be a tag, so the tags count up from zero and each
    // variant keeps its value beside the tag, for `as` to give back.
    e->RawFloat = f32 ? Types.f32() : Types.f64();
    for (size_t i = 0; i < e->Variants.size(); ++i) {
      EnumVariantDecl *v = e->Variants[i].get();
      v->Value = static_cast<int64_t>(i);
      if (!v->Discriminant) {
        Diags.error(v->Range, "variant '{}' needs a value", v->Name)
            .note("the other variants of '{}' have float values, so there is "
                  "no next value to give this one", e->Name)
            .code(401);
        continue;
      }
      v->FloatValue = values[i].Float;
    }
    return;
  }

  int64_t next = 0;
  for (size_t i = 0; i < e->Variants.size(); ++i) {
    EnumVariantDecl *v = e->Variants[i].get();
    if (values[i].Ok)
      next = values[i].Int;
    v->Value = next++;
  }
}

void Sema::registerMethods(NominalDecl *nd) {
  if (!nd->DeclaredType)
    return;
  auto &table = Methods[nd->DeclaredType];
  for (auto &m : nd->Methods) {
    m->OwnerType = nd->DeclaredType;
    if (m->ModulePath.empty())
      m->ModulePath = static_cast<Decl *>(nd)->ModulePath;
    if (table.count(m->Name)) {
      auto b = Diags.error(m->NameRange.isValid() ? m->NameRange : m->Range,
                           "'{}' already has a method named '{}'",
                           static_cast<Decl *>(nd)->Name, m->Name);
      b.code(213);
      noteDeclaredAt(b, table[m->Name], "the earlier definition",
                     "each method name may only be defined once per type");
      continue;
    }
    table[m->Name] = m.get();
    InherentMethods.insert({nd->DeclaredType, m->Name});
    for (const auto &alias : aliasesOf(m.get())) {
      if (table.count(alias.first)) {
        Diags.error(alias.second, "'{}' already has a member named '{}'",
                    static_cast<Decl *>(nd)->Name, alias.first)
            .code(233);
        continue;
      }
      table[alias.first] = m.get();
      InherentMethods.insert({nd->DeclaredType, alias.first});
    }
  }
}

void Sema::applyWeakField(FieldDecl *f) {
  if (!f->IsWeak || !f->Ty || f->Ty->isError())
    return;
  if (Memory == MemoryMode::Zombie) {
    auto d = Diags.error(f->Range, "`weak` needs a reference count, and "
                                   "`--memory zombie` has none");
    d.note("a weak reference reads as empty once its target is gone, which "
           "only a count can know");
    d.note("keep an index into a `std::mem::Arena<T>`, or a `&` borrow that "
           "the checker follows");
    d.code(288);
  }
  // `weak parent: Node` and `weak parent: Node?` both mean the same thing:
  // the reference does not keep the target alive, and reading it may find
  // nothing, so the field's type is always an Option.
  Type *target = isOptionType(f->Ty) ? optionPayload(f->Ty) : f->Ty;
  if (!target || !target->is(TypeKind::Class)) {
    Diags.error(f->TypeAnnotation ? f->TypeAnnotation->Range : f->Range,
                "a `weak` field must refer to a class — '{}' is not one",
                target ? target->toString() : "?")
        .note("only class instances are reference counted, so only they can "
              "be referred to weakly")
        .code(267);
    f->IsWeak = false;
    return;
  }
  f->WeakTarget = target;
  f->Ty = optionOf(target, f->Range);
}

void Sema::layoutClass(ClassDecl *c) {
  if (!c->VTable.empty() || c->FieldBase != 0)
    return; // already laid out
  if (c->Super) {
    layoutClass(c->Super);
    c->VTable = c->Super->VTable;
    c->FieldBase = c->Super->FieldBase +
                   static_cast<unsigned>(c->Super->Fields.size());
  }
  // Re-index this class's own fields so they follow the inherited ones.
  for (size_t i = 0; i < c->Fields.size(); ++i)
    c->Fields[i]->Index = c->FieldBase + static_cast<unsigned>(i);

  for (auto &m : c->Methods) {
    if (m->Flavour == FunctionFlavour::Initialiser ||
        m->Flavour == FunctionFlavour::Deinitialiser)
      continue;
    // A generic method is a different function for every set of type
    // arguments, so there is no single address a slot could hold. It is
    // dispatched statically, from the type at the call site.
    if (!m->Generics.empty())
      continue;
    bool hasSelf = false;
    for (const Param &p : m->Params)
      if (p.IsSelf)
        hasSelf = true;
    if (!hasSelf) {
      m->IsStatic = true;
      continue;
    }
    // An identically named method in a base class is overridden in place.
    int slot = -1;
    for (size_t i = 0; i < c->VTable.size(); ++i)
      if (c->VTable[i]->Name == m->Name)
        slot = static_cast<int>(i);
    if (slot >= 0) {
      m->IsOverride = true;
      m->IsVirtual = true;
      m->VTableIndex = slot;
      c->VTable[slot] = m.get();
    } else {
      m->IsVirtual = true;
      m->VTableIndex = static_cast<int>(c->VTable.size());
      c->VTable.push_back(m.get());
    }
  }
}

/// An `extend` on a generic type, folded into that type.
///
/// `extend<A> List<A> { ... }` and `extend List { ... }` both mean the same
/// thing — these methods belong to `List`, whatever its parameter is called
/// — and the simplest way to make that true is to give the methods to the
/// declaration itself. Every instantiation then clones them along with the
/// ones written inside the type, and signatures, bodies, symbols, `deinit`
/// discovery and the borrow checker all follow without knowing an `extend`
/// was ever involved.
///
/// The parameters have to line up one for one: the target's arguments are the
/// extend's own parameters, in order. Anything else would be a partial
/// specialisation — `extend<A> List<Pair<A>>` — which this language does not
/// have, and which is reported rather than half-applied.
bool Sema::foldGenericExtend(ExtendDecl *e) {
  auto *named = e->TargetType ? dyn_cast<NamedTypeRepr>(e->TargetType.get())
                              : nullptr;
  if (!named)
    return false;
  NominalDecl *tmpl = lookupNominal(named->Path, named->Range, /*quiet=*/true);
  if (!tmpl)
    return false;
  // A name inside an instantiation means that instantiation; the template is
  // what an `extend` adds to.
  if (tmpl->GenericTemplate)
    tmpl = tmpl->GenericTemplate;
  if (tmpl->Generics.empty())
    return false;                 // an ordinary type: the usual path applies
  if (isa<MarkDecl>(static_cast<Decl *>(tmpl)))
    return false;

  const size_t arity = tmpl->Generics.size();
  // `extend List { ... }`: the type's own parameters, under their own names,
  // so there is nothing to map.
  if (named->GenericArgs.empty() && e->Generics.empty()) {
    // nothing to do
  } else {
    if (named->GenericArgs.size() != arity || e->Generics.size() != arity) {
      auto d = Diags.error(named->Range,
                           "'{}' takes {} generic argument(s) — an `extend` "
                           "has to name them all",
                           static_cast<Decl *>(tmpl)->Name, arity);
      d.note("write `extend<{}> {}<{}>`, or `extend {}` to use the names the "
             "type declares",
             tmpl->Generics.front().Name, static_cast<Decl *>(tmpl)->Name,
             tmpl->Generics.front().Name, static_cast<Decl *>(tmpl)->Name)
          .code(206);
      noteDeclaredAt(d, static_cast<Decl *>(tmpl), "declared here",
                     "this is the parameter list to match");
      return true;                // reported; nothing more to do with it
    }
    // Each argument has to be one of the extend's own parameters, and each
    // parameter used once: that is what makes the two lists one list.
    std::set<std::string> params;
    for (const auto &g : e->Generics)
      params.insert(g.Name);
    std::set<std::string> used;
    for (size_t i = 0; i < arity; ++i) {
      auto *arg = dyn_cast<NamedTypeRepr>(named->GenericArgs[i].get());
      const bool bare = arg && arg->Path.size() == 1 && arg->GenericArgs.empty();
      if (!bare || !params.count(arg->Path[0]) ||
          !used.insert(arg->Path[0]).second) {
        Diags.error(named->GenericArgs[i]->Range,
                    "an `extend` on a generic type names its parameters, one "
                    "for each of the type's")
            .note("`extend<{}> {}<{}>` extends every `{}`; a narrower one — "
                  "extending only some of them — is not something this "
                  "language has",
                  tmpl->Generics.front().Name,
                  static_cast<Decl *>(tmpl)->Name,
                  tmpl->Generics.front().Name,
                  static_cast<Decl *>(tmpl)->Name)
            .code(206);
        return true;
      }
      // The type's parameter answers to this name too, wherever these
      // methods are resolved.
      const std::string &alias = arg->Path[0];
      if (alias != tmpl->Generics[i].Name) {
        auto &aliases = tmpl->Generics[i].Aliases;
        if (std::find(aliases.begin(), aliases.end(), alias) == aliases.end())
          aliases.push_back(alias);
      }
      // A bound written on the extend's parameter is a bound on the type's.
      for (const auto &g : e->Generics)
        if (g.Name == alias)
          for (const auto &b : g.Bounds)
            tmpl->Generics[i].Bounds.push_back(cloneTypeRepr(b.get()));
    }
  }

  for (auto &fn : e->Methods) {
    if (fn->ModulePath.empty())
      fn->ModulePath = e->ModulePath;
    // A method already declared inside the type wins; two `extend` blocks
    // adding the same name is the same clash, reported the same way.
    bool clash = false;
    for (const auto &have : tmpl->Methods)
      if (have->Name == fn->Name) {
        auto d = Diags.error(fn->NameRange.isValid() ? fn->NameRange
                                                     : fn->Range,
                             "'{}' already has a method named '{}'",
                             static_cast<Decl *>(tmpl)->Name, fn->Name);
        d.code(213);
        noteDeclaredAt(d, have.get(), "the earlier definition",
                       "an extension cannot replace an existing method");
        clash = true;
      }
    if (clash)
      continue;
    auto cloned = cloneFunction(fn.get());
    cloned->Parent = static_cast<Decl *>(tmpl);
    tmpl->Methods.push_back(std::move(cloned));
  }
  e->FoldedIntoTemplate = true;
  return true;
}

void Sema::resolveShapes(Module *m) {
  CurModule = m;
  CurScope = scopeForModule(m);

  // Superclasses and mark hierarchies first: layout depends on them.
  for (auto &d : m->Decls) {
    if (auto *c = dyn_cast<ClassDecl>(d.get())) {
      if (!c->SuperClass)
        continue;
      Type *st = resolveTypeOrError(c->SuperClass.get(), Types.errorType());
      if (st->is(TypeKind::Class)) {
        c->Super = reinterpret_cast<ClassDecl *>(st->nominal());
        if (c->Super == c) {
          Diags.error(c->SuperClass->Range, "a class cannot inherit from itself")
              .code(214);
          c->Super = nullptr;
        }
      } else if (!st->isError()) {
        auto b = Diags.error(c->SuperClass->Range,
                             "'{}' is not a class, so '{}' cannot extend it",
                             st->toString(), c->Name);
        b.note("only classes participate in inheritance; use `bind` to give a "
               "struct or enum shared behaviour")
            .code(215);
      }
    }
    if (auto *mk = dyn_cast<MarkDecl>(d.get())) {
      for (const auto &sm : mk->SuperMarks) {
        Type *st = resolveTypeOrError(sm.get(), Types.errorType());
        if (st->is(TypeKind::Mark))
          mk->Supers.push_back(reinterpret_cast<MarkDecl *>(st->nominal()));
        else if (!st->isError())
          Diags.error(sm->Range, "'{}' is not a mark", st->toString()).code(216);
      }
      checkAutoMark(mk);
    }
    // A C++ class's base, so a pointer to it converts before any signature
    // that relies on that is checked.
    if (auto *nd = dyn_cast<NominalDecl>(d.get()))
      if (nd->Cxx)
        resolveCxxType(nd);
  }

  // `struct Derived : Base` — the parent's members come first, so they are
  // put in before anything reads the field list or lays the type out.
  for (auto &d : m->Decls)
    if (auto *nd = dyn_cast<NominalDecl>(d.get()))
      spliceInheritance(nd);

  // Field and variant types.
  for (auto &d : m->Decls) {
    auto *nd = dyn_cast<NominalDecl>(d.get());
    if (!nd || !nd->Generics.empty())
      continue; // generic templates are resolved per instantiation

    ActiveSelfType = nd->DeclaredType;
    for (auto &f : nd->Fields) {
      f->Ty = resolveTypeOrError(f->TypeAnnotation.get(), Types.errorType());
      applyWeakField(f.get());
      rejectCxxClassByValue(f->Ty, f->TypeAnnotation ? f->TypeAnnotation->Range
                                                     : f->Range,
                            "the field");
      rejectMarkByValue(f->Ty, f->TypeAnnotation ? f->TypeAnnotation->Range
                                                 : f->Range,
                        "a field");
    }
    if (auto *e = dyn_cast<EnumDecl>(d.get())) {
      assignVariantValues(e);
      for (auto &v : e->Variants) {
        for (auto &tt : v->TupleTypes)
          resolveTypeOrError(tt.get(), Types.errorType());
        for (auto &f : v->Fields)
          f->Ty = resolveTypeOrError(f->TypeAnnotation.get(), Types.errorType());
      }
    }
    resolveFieldAnnotations(nd);
    ActiveSelfType = nullptr;
    registerMethods(nd);
  }

  for (auto &d : m->Decls)
    if (auto *c = dyn_cast<ClassDecl>(d.get()))
      if (c->Generics.empty())
        layoutClass(c);

  // An `extend` on a generic type has already been folded into that type,
  // for every module at once, before any shapes were resolved — see the
  // caller. The loop below handles the ordinary case.

  // `extend` and `bind` contribute methods to types declared anywhere. Their
  // own generic parameters have to be in scope while the target type is
  // resolved, so that `bind<T> Show to Wrapper<T>` can name `T`.
  for (auto &d : m->Decls) {
    if (auto *e = dyn_cast<ExtendDecl>(d.get())) {
      if (e->FoldedIntoTemplate)
        continue;
      auto savedGenerics = ActiveGenericParams;
      bindGenerics(e->Generics, Types, ActiveGenericParams);
      Type *t = resolveTypeOrError(e->TargetType.get(), Types.errorType());
      ActiveGenericParams = savedGenerics;
      e->ResolvedTarget = t;
      if (t->isError())
        continue;
      auto &table = Methods[t];
      for (auto &fn : e->Methods) {
        fn->Parent = t->isNominal() ? static_cast<Decl *>(t->nominal()) : nullptr;
        fn->OwnerType = t;
        if (fn->ModulePath.empty())
          fn->ModulePath = e->ModulePath;
        if (table.count(fn->Name)) {
          auto b = Diags.error(fn->NameRange, "'{}' already has a method '{}'",
                               t->toString(), fn->Name);
          b.code(213);
          noteDeclaredAt(b, table[fn->Name], "the earlier definition",
                         "an extension cannot replace an existing method");
          continue;
        }
        table[fn->Name] = fn.get();
        InherentMethods.insert({t, fn->Name});
        for (const auto &alias : aliasesOf(fn.get())) {
          if (table.count(alias.first)) {
            Diags.error(alias.second, "'{}' already has a member named '{}'",
                        t->toString(), alias.first)
                .code(233);
            continue;
          }
          table[alias.first] = fn.get();
          InherentMethods.insert({t, alias.first});
        }
      }
    }

    // The bind itself is prepared elsewhere: an instantiation may need it
    // before this pass reaches the module it was written in.
    if (auto *b = dyn_cast<BindDecl>(d.get()))
      prepareBind(m, b);
  }
}

//===----------------------------------------------------------------------===//
// Binds
//===----------------------------------------------------------------------===//

void Sema::prepareBind(Module *m, BindDecl *b) {
  // Whoever needs the bind first does the work; everyone after finds it done.
  // Inserting before any of it also stops a bind that reaches itself — a
  // concrete target instantiates the very type the bind is written for — from
  // registering its methods twice.
  if (!PreparedBinds.insert(b).second)
    return;
  // A bind resolves in the module it was written in, with only its own type
  // parameters in scope. That is already true when the shapes pass calls this,
  // and has to be made true when an instantiation does: there, the live scope
  // is the one the *template* is being checked in, with the instantiation's
  // arguments bound.
  Scope *savedScope = CurScope;
  Module *savedModule = CurModule;
  auto savedOuterGenerics = ActiveGenericParams;
  Type *savedOuterSelf = ActiveSelfType;
  if (Scope *own = scopeForModule(m)) {
    CurScope = own;
    CurModule = m;
  }
  ActiveGenericParams.clear();
  ActiveSelfType = nullptr;
  struct Restore {
    Sema &S;
    Scope *Sc;
    Module *M;
    std::map<std::string, Type *> G;
    Type *Self;
    ~Restore() {
      S.CurScope = Sc;
      S.CurModule = M;
      S.ActiveGenericParams = G;
      S.ActiveSelfType = Self;
    }
  } restore{*this, savedScope, savedModule, std::move(savedOuterGenerics),
            savedOuterSelf};

  // Generic binds are applied when the target type is instantiated; here
  // the target only has to resolve far enough to name the template.
  auto savedGenerics = ActiveGenericParams;
  bindGenerics(b->Generics, Types, ActiveGenericParams);
  Type *t = resolveTypeOrError(b->TargetType.get(), Types.errorType());
  ActiveGenericParams = savedGenerics;
  b->ResolvedTarget = t;
  // A builtin may be *extended* but never overridden: `String + String`
  // is the language's and stays the language's, while `String +
  // Character` is a meaning it does not have and so is yours to give. The
  // pair is checked below, once the overload's right-hand type is known.
  if (t->isError())
    return;

  if (b->IsOperatorBinding) {
    std::string op = canonicalOperatorName(b->OperatorName);
    b->OperatorName = op;
    // `@alias("*")` on the block registers that spelling globally, so
    // `bind operator::"*"` elsewhere means the same operator.
    for (const auto &alias : aliasesOf(b))
      OperatorAliases[alias.first] = op;
    for (auto &fn : b->Methods) {
      fn->IsOperatorImpl = true;
      fn->Parent = t->isNominal() ? static_cast<Decl *>(t->nominal())
                                  : nullptr;
      fn->OwnerType = t;
      if (fn->ModulePath.empty())
        fn->ModulePath = b->ModulePath;
      std::string slot = fn->Name == op ? op : canonicalOperatorName(fn->Name);
      // Refuse an overload that would shadow a meaning the language
      // already gives this pair. The rule is the same for every type; it
      // only ever bites on builtins, since nothing else has one.
      // Parameter types are resolved in the signature pass, which has
      // not run yet, so the annotation is resolved here — with the bind's
      // own generics in scope, the same way its target was.
      Type *rhs = nullptr;
      for (const Param &p : fn->Params) {
        if (p.IsSelf || !p.TypeAnnotation)
          continue;
        auto savedG = ActiveGenericParams;
        bindGenerics(b->Generics, Types, ActiveGenericParams);
        rhs = resolveTypeOrError(p.TypeAnnotation.get(), nullptr);
        ActiveGenericParams = savedG;
        while (rhs && rhs->is(TypeKind::Pointer) && !rhs->isRawPointer())
          rhs = rhs->pointee();
        break;
      }
      if (rhs && builtinOperatorApplies(slot, t, rhs)) {
        auto d = Diags.error(
            fn->NameRange.isValid() ? fn->NameRange : fn->Range,
            "'{}' already has a meaning for '{}' and '{}'",
            assignOrOperatorSpelling(slot), t->toString(),
            rhs->toString());
        d.note("an overload may teach a type to work with another type, "
               "but never replace what the language already does");
        d.code(218);
        continue;
      }
      Operators[{t, slot}].push_back(fn.get());
      Methods[t].emplace(fn->Name, fn.get());
      // A method may answer to more than one operator spelling.
      for (const auto &alias : aliasesOf(fn.get())) {
        Operators[{t, canonicalOperatorName(alias.first)}].push_back(fn.get());
        Methods[t].emplace(alias.first, fn.get());
      }
    }
    return;
  }

  Symbol *msym = lookupPath(b->MarkPath, b->MarkRange, /*quiet=*/true);
  MarkDecl *mark = msym && msym->D ? dyn_cast<MarkDecl>(msym->D) : nullptr;
  // `bind As<Fahrenheit> to Celsius` binds an *instantiation* of the mark,
  // so that `As<Fahrenheit>` and `As<Kelvin>` are different conformances.
  // `bind Celsius into Fahrenheit` arrives here already in that shape.
  if (mark && !b->MarkGenericArgs.empty()) {
    auto savedGenerics = ActiveGenericParams;
    bindGenerics(b->Generics, Types, ActiveGenericParams);
    std::vector<Type *> markArgs;
    for (const auto &ga : b->MarkGenericArgs)
      markArgs.push_back(resolveTypeOrError(ga.get(), Types.errorType()));
    ActiveGenericParams = savedGenerics;
    if (markArgs.size() != mark->Generics.size()) {
      auto d = Diags.error(b->MarkRange,
                           "'{}' expects {} type argument(s) — {} given",
                           mark->Name, mark->Generics.size(),
                           markArgs.size());
      d.code(206);
      noteDeclaredAt(d, mark, "declared here", "the counts must match");
      return;
    }
    if (NominalDecl *inst = instantiateNominal(mark, markArgs, b->MarkRange))
      mark = dyn_cast<MarkDecl>(static_cast<Decl *>(inst));
  } else if (mark && !mark->Generics.empty()) {
    auto d = Diags.error(b->MarkRange,
                         "'{}' is generic; say what it converts to",
                         mark->Name);
    d.note(fmt("write `bind {}<...> to {}`", mark->Name, t->toString())
               .c_str())
        .code(206);
    noteDeclaredAt(d, mark, "declared here", "it takes type arguments");
    return;
  }
  if (!mark) {
    std::string joined;
    for (size_t i = 0; i < b->MarkPath.size(); ++i) {
      if (i) joined += "::";
      joined += b->MarkPath[i];
    }
    Diags.error(b->MarkRange, "cannot find mark '{}'", joined)
        .note("`bind X to T` implements the mark X for the type T")
        .code(219);
    return;
  }
  // `Send` and `Sync` are answers, not promises. Letting one be bound by
  // hand would put the whole guarantee behind a line of code nobody has
  // to justify — and the honest way to say "this one is safe" is
  // `@sync`, which at least asks for a reason.
  if (mark == SendDecl || mark == SyncDecl) {
    auto d = Diags.error(b->MarkRange, "'{}' cannot be bound by hand",
                         static_cast<Decl *>(mark)->Name);
    d.note("the compiler reads it off the type, so that it stays true as "
           "the type changes");
    d.note("a type that synchronises its own access says so with "
           "`@sync(\"reason\")`, which is what `thread::Mutex` does");
    d.code(219);
    return;
  }
  b->ResolvedMark = mark;
  Conformances[t].push_back(mark);
  // What this binding chose for the mark's associated types. Resolved
  // with the bind's own parameters in scope, so `type Item = T` works.
  {
    auto savedGenerics = ActiveGenericParams;
    Type *savedSelf = ActiveSelfType;
    bindGenerics(b->Generics, Types, ActiveGenericParams);
    ActiveSelfType = t;
    auto &chosen = AssocTypes[{t, mark}];
    for (auto &at : b->AssociatedTypes) {
      bool wanted = false;
      for (auto &req : mark->AssociatedTypes)
        if (req->Name == at->Name)
          wanted = true;
      if (!wanted) {
        auto d = Diags.error(at->NameRange,
                             "mark '{}' has no associated type '{}'",
                             mark->Name, at->Name);
        d.code(243);
        noteDeclaredAt(d, mark, "the mark declared here",
                       "only the types it declares may be chosen");
        continue;
      }
      at->Resolved = at->Value
                         ? resolveTypeOrError(at->Value.get(),
                                              Types.errorType())
                         : Types.errorType();
      chosen[at->Name] = at->Resolved;
      // `type Item: Display` on the requirement constrains what may be
      // chosen here. Resolve the bound now, while this scope is the live
      // one, and decide once every binding in the program is in.
      for (auto &req : mark->AssociatedTypes)
        if (req->Name == at->Name)
          for (const auto &bound : req->Bounds) {
            resolveType(bound.get());
            DeferredBounds.push_back({at->Resolved, bound.get(), at->Range,
                                      req->Range, mark->Name});
          }
    }
    // Every one the mark asks for has to be answered.
    for (auto &req : mark->AssociatedTypes) {
      if (chosen.count(req->Name))
        continue;
      auto d = Diags.error(b->TargetType ? b->TargetType->Range : b->Range,
                           "'{}' does not choose '{}' required by mark "
                           "'{}'",
                           t->toString(), req->Name, mark->Name);
      d.note(fmt("add `type {} = ...` to this bind block", req->Name)
                 .c_str())
          .code(243);
      noteDeclaredAt(d, req.get(), "required here",
                     "each binding decides what this type is");
    }
    ActiveGenericParams = savedGenerics;
    ActiveSelfType = savedSelf;
  }
  if (t->isNominal())
    t->nominal()->Bindings.push_back(b);

  auto &table = Methods[t];
  auto &supplied = MarkImpls[{t, mark}];
  for (auto &fn : b->Methods) {
    fn->Parent = t->isNominal() ? static_cast<Decl *>(t->nominal()) : nullptr;
    fn->OwnerType = t;
    fn->FromMark = mark;
    fn->Bind = b;
    if (fn->ModulePath.empty())
      fn->ModulePath = b->ModulePath;
    supplied[fn->Name] = fn.get();
    addMarkImpl(t, mark, fn->Name, fn.get());
    for (const auto &alias : aliasesOf(fn.get())) {
      supplied[alias.first] = fn.get();
      addMarkImpl(t, mark, alias.first, fn.get());
    }
    // A method the type declares itself always answers `value.name()`; the
    // mark's version stays reachable as `value::Mark.name()`.
    if (InherentMethods.count({t, fn->Name})) {
      auto w = Diags.warn(
          fn->NameRange.isValid() ? fn->NameRange : fn->Range,
          "'{}' already declares '{}', so this implementation is not what "
          "`value.{}()` calls",
          t->toString(), fn->Name, fn->Name);
      w.note(fmt("reach it explicitly with `value::{}.{}()`", mark->Name,
                    fn->Name)
                 .c_str())
          .code(223);
      continue;
    }
    // A bind may override an inherited default, but not something narrower.
    //
    // A *generic* bind is only staged here: `t` is the template's own type,
    // and which binding wins is decided per instantiation, where what the
    // parameters stand for is finally known. Deciding it here would report
    // two disjoint `where` clauses as a clash before either had a chance to
    // rule itself out.
    if (b->Generics.empty())
      claimMethodSlot(t, fn->Name, fn.get(), b, bindSpecificity(b), table);
    else
      table[fn->Name] = fn.get();
  }
  // Mark defaults fill in whatever the bind did not provide. Each default
  // is *copied* into the bind rather than shared, so that inside its body
  // `Self` is the concrete type and calls to sibling requirements resolve
  // to this type's implementations rather than to the bodyless
  // requirements on the mark.
  for (auto &def : mark->Methods) {
    if (!def->Body || supplied.count(def->Name))
      continue;
    auto copy = cloneFunction(def.get());
    copy->Parent = t->isNominal() ? static_cast<Decl *>(t->nominal()) : nullptr;
    copy->OwnerType = t;
    copy->FromMark = mark;
    copy->Bind = b;
    // The body was written in the mark's file, so the names it can see are
    // the ones visible there — `Map` and `Filter` in `std::iter`, not
    // whatever happens to be in scope where the `bind` was written.
    copy->ModulePath = static_cast<Decl *>(mark)->ModulePath;
    copy->IsPublic = b->IsPublic || def->IsPublic;
    // A binding read back from a `.rul` had its defaults copied in when
    // that library was compiled, so the code is already in its object.
    // Declare them here rather than emitting a second copy.
    copy->IsImported = m->FromLibrary;
    b->Methods.push_back(std::move(copy));
    FunctionDecl *made = b->Methods.back().get();
    supplied[made->Name] = made;
    addMarkImpl(t, mark, made->Name, made);
    if (b->Generics.empty())
      claimMethodSlot(t, made->Name, made, b, bindSpecificity(b), table);
    else if (!InherentMethods.count({t, made->Name}))
      table[made->Name] = made;
  }
}

//===----------------------------------------------------------------------===//
// Pass 3: signatures
//===----------------------------------------------------------------------===//

void Sema::resolveSignatures(Module *m) {
  CurModule = m;
  CurScope = scopeForModule(m);

  // A global's type belongs to the signature pass. A generic instantiated from
  // some other signature has its method bodies checked here, and those bodies
  // may read a global — `mem::allocator` is one — so the type has to be known
  // by now. The initialiser is still checked with the bodies.
  for (auto &d : m->Decls)
    if (auto *g = dyn_cast<GlobalVarDecl>(d.get()))
      if (!g->Ty && g->TypeAnnotation)
        g->Ty = resolveTypeOrError(g->TypeAnnotation.get(), Types.errorType());
  for (auto &d : m->Decls)
    if (auto *ext = dyn_cast<ExternDecl>(d.get()))
      for (auto &g : ext->Globals)
        if (!g->Ty && g->TypeAnnotation)
          g->Ty = resolveTypeOrError(g->TypeAnnotation.get(),
                                     Types.errorType());

  auto resolveFn = [&](FunctionDecl *fn, Type *selfType) {
    auto savedGenerics = ActiveGenericParams;
    Type *savedSelf = ActiveSelfType;
    Scope *savedScope = CurScope;
    Module *savedModule = CurModule;
    // A method copied from a mark default was written in the mark's file, so
    // the names in its signature are the ones visible there — not the ones in
    // scope where the `bind` happens to be.
    if (fn->ModulePath != m->Name) {
      auto mit = ModulesByName.find(fn->ModulePath);
      if (mit != ModulesByName.end()) {
        CurModule = mit->second;
        CurScope = scopeForModule(mit->second);
      }
    }
    if (selfType)
      ActiveSelfType = selfType;
    bindGenerics(fn->Generics, Types, ActiveGenericParams);

    std::vector<Type *> params;
    for (Param &p : fn->Params) {
      if (p.IsSelf) {
        p.Ty = selfTypeFor(p, ActiveSelfType ? ActiveSelfType
                                             : Types.errorType());
        continue;
      }
      p.Ty = resolveTypeOrError(p.TypeAnnotation.get(), Types.errorType());
      rejectCxxClassByValue(p.Ty, p.TypeAnnotation ? p.TypeAnnotation->Range
                                                   : p.Range,
                            "the parameter");
      rejectMarkByValue(p.Ty, p.TypeAnnotation ? p.TypeAnnotation->Range
                                               : p.Range,
                        "a parameter");
      params.push_back(p.Ty);
    }
    Type *ret = resolveReturnType(fn, ActiveSelfType);
    rejectCxxClassByValue(ret, fn->ReturnType ? fn->ReturnType->Range : fn->Range,
                          "the result");
    rejectMarkByValue(ret, fn->ReturnType ? fn->ReturnType->Range : fn->Range,
                      "a result");
    fn->Ty = Types.functionOf(params, ret, fn->IsVariadic);
    if (fn->MangledName.empty())
      fn->MangledName = mangleFunction(fn, fn->TypeArguments);
    // A signature without a body — an extern, a mark requirement — is only
    // ever seen here, so its clauses are resolved now; one with a body is
    // done when the body is, once the parameters are bound.
    if (!fn->Body)
      resolveSignatureAnnotations(fn);

    ActiveGenericParams = savedGenerics;
    ActiveSelfType = savedSelf;
    CurScope = savedScope;
    CurModule = savedModule;
  };

  for (auto &d : m->Decls) {
    if (auto *fn = dyn_cast<FunctionDecl>(d.get())) {
      if (fn->Generics.empty())
        resolveFn(fn, nullptr);
      continue;
    }
    if (auto *e = dyn_cast<ExternDecl>(d.get())) {
      const bool cxx = e->ABI == "C++";
      if (cxx)
        UsesCxx = true;
      for (auto &fn : e->Functions) {
        resolveFn(fn.get(), nullptr);
        checkForeignSignature(fn.get());
      }
      for (auto &g : e->Globals) {
        g->Ty = resolveTypeOrError(g->TypeAnnotation.get(), Types.errorType());
        if (cxx && !g->CxxScope.empty()) {
          // A namespaced C++ variable has a mangled symbol too; one at the
          // global namespace does not.
          SemaAliasResolver resolver([&](const std::vector<std::string> &path) {
            return lookupPath(path, SourceRange(), /*quiet=*/true);
          });
          CxxMangler mangler(Cxx, resolver);
          g->LinkName = mangler.mangleVariable(g.get());
        }
      }
      continue;
    }
    if (auto *nd = dyn_cast<NominalDecl>(d.get())) {
      if (!nd->Generics.empty())
        continue;
      for (auto &fn : nd->Methods)
        if (fn->Generics.empty())
          resolveFn(fn.get(), nd->DeclaredType);
      continue;
    }
    if (auto *ext = dyn_cast<ExtendDecl>(d.get())) {
      if (ext->FoldedIntoTemplate)
        continue;   // the type carries these now
      for (auto &fn : ext->Methods)
        if (fn->Generics.empty())
          resolveFn(fn.get(), ext->ResolvedTarget);
      continue;
    }
    if (auto *b = dyn_cast<BindDecl>(d.get())) {
      if (!b->Generics.empty())
        continue;
      for (auto &fn : b->Methods)
        if (fn->Generics.empty())
          resolveFn(fn.get(), b->ResolvedTarget);
      continue;
    }
    if (auto *mk = dyn_cast<MarkDecl>(d.get())) {
      for (auto &fn : mk->Methods)
        if (fn->Generics.empty())
          resolveFn(fn.get(), mk->DeclaredType);
      continue;
    }
  }
}

//===----------------------------------------------------------------------===//
// Pass 4: bodies
//===----------------------------------------------------------------------===//

void Sema::checkBodies(Module *m) {
  CurModule = m;
  CurScope = scopeForModule(m);

  // A module read back from a `.rul` is checked for its signatures, but its
  // bodies are already compiled into that library, so nothing here is queued
  // for emission. Generic instantiations still go through `instantiate`, which
  // registers the clones it creates.
  const bool emit = !m->FromLibrary;

  for (auto &d : m->Decls) {
    switch (d->Kind) {
    case NodeKind::Function: {
      auto *fn = cast<FunctionDecl>(d.get());
      if (!fn->Generics.empty())
        continue; // checked at each instantiation
      checkDecorators(fn);
      checkFunction(fn, nullptr, nullptr);
      if (emit) {
        Result.Functions.push_back(fn);
        if (fn->Name == "main" && !fn->Parent)
          Result.EntryPoint = fn;
      }
      break;
    }
    case NodeKind::Struct:
    case NodeKind::Enum:
    case NodeKind::Class:
    case NodeKind::Mark: {
      auto *nd = static_cast<NominalDecl *>(d.get());
      if (!nd->Generics.empty())
        continue;
      checkNominalBodies(nd);
      if (emit)
        Result.Nominals.push_back(nd);
      break;
    }
    case NodeKind::Extend:
      checkExtend(cast<ExtendDecl>(d.get()));
      break;
    case NodeKind::Bind:
      checkBind(cast<BindDecl>(d.get()));
      break;
    case NodeKind::GlobalVar:
      checkGlobal(cast<GlobalVarDecl>(d.get()));
      // Globals are recorded even for library modules: the storage merges at
      // link time, but only the final program runs the initialisers, so the
      // importer has to emit them.
      Result.Globals.push_back(cast<GlobalVarDecl>(d.get()));
      break;
    case NodeKind::Extern: {
      auto *e = cast<ExternDecl>(d.get());
      if (emit) {
        for (auto &fn : e->Functions)
          Result.Functions.push_back(fn.get());
        for (auto &g : e->Globals)
          Result.Globals.push_back(g.get());
      }
      break;
    }
    default:
      break;
    }
  }
}

/// A foreign signature has to be spelled in types C also has. The one that
/// looks right but is not is `@function`: it is a closure, two words wide,
/// and C has nowhere to put the second. `@cfunction` is the bare pointer.
void Sema::checkForeignSignature(FunctionDecl *fn) {
  auto complain = [&](SourceRange where, Type *t, const char *role) {
    if (!t || !t->is(TypeKind::Function))
      return;
    auto d = Diags.error(where, "'{}' is not a C type", t->toString());
    d.note("a function value carries a captured environment alongside its "
           "code; a C function pointer is the code alone");
    d.note("write `@cfunction(...)` for the {}", role);
    d.code(503);
  };
  for (const Param &p : fn->Params)
    complain(p.TypeAnnotation ? p.TypeAnnotation->Range : fn->Range, p.Ty,
             "parameter");
  if (fn->Ty)
    complain(fn->ReturnType ? fn->ReturnType->Range : fn->Range,
             fn->Ty->result(), "result");
}

void Sema::checkFieldDefaults(NominalDecl *nd) {
  auto checkOne = [&](FieldDecl *f) {
    if (!f->DefaultValue || f->DefaultValue->Ty)
      return;
    FnStack.push_back(FunctionContext{});
    pushScope(ScopeKind::Function);
    Type *got = checkExpr(f->DefaultValue.get(), f->Ty);
    popScope();
    FnStack.pop_back();
    requireConvertible(f->DefaultValue.get(), got, f->Ty,
                       fmt("the default for field '{}'", f->Name).c_str());
  };

  Type *savedSelf = ActiveSelfType;
  ActiveSelfType = nd->DeclaredType;
  for (auto &f : nd->Fields)
    checkOne(f.get());
  if (auto *e = dyn_cast<EnumDecl>(static_cast<Decl *>(nd)))
    for (auto &v : e->Variants)
      for (auto &f : v->Fields)
        checkOne(f.get());
  ActiveSelfType = savedSelf;
}

void Sema::checkNominalBodies(NominalDecl *nd) {
  // A mark's own methods are templates: their `self` is the mark, not a
  // concrete type. Each `bind` copies the ones it needs, and those copies are
  // what actually get checked and emitted.
  if (isa<MarkDecl>(static_cast<Decl *>(nd)))
    return;
  checkFieldDefaults(nd);
  ActiveSelfType = nd->DeclaredType;
  auto *cls = dyn_cast<ClassDecl>(static_cast<Decl *>(nd));
  for (auto &fn : nd->Methods) {
    rejectMethodDecorators(fn.get());
    if (!fn->Generics.empty())
      continue;
    if (!fn->Body)
      continue;
    checkFunction(fn.get(), nd->DeclaredType, cls);
    if (!CurModule || !CurModule->FromLibrary)
      Result.Functions.push_back(fn.get());
  }
  ActiveSelfType = nullptr;
}

void Sema::checkExtend(ExtendDecl *e) {
  if (e->FoldedIntoTemplate)
    return;   // checked with the type, once per instantiation
  if (!e->ResolvedTarget || e->ResolvedTarget->isError())
    return;
  if (!e->Generics.empty())
    return;
  ActiveSelfType = e->ResolvedTarget;
  auto *cls = e->ResolvedTarget->isNominal()
                  ? dyn_cast<ClassDecl>(
                        static_cast<Decl *>(e->ResolvedTarget->nominal()))
                  : nullptr;
  for (auto &fn : e->Methods) {
    rejectMethodDecorators(fn.get());
    if (!fn->Generics.empty() || !fn->Body)
      continue;
    checkFunction(fn.get(), e->ResolvedTarget, cls);
    if (!CurModule || !CurModule->FromLibrary)
      Result.Functions.push_back(fn.get());
  }
  ActiveSelfType = nullptr;
}

FunctionDecl *Sema::lookupMarkMethod(Type *receiver, MarkDecl *mark,
                                     const std::string &name) {
  if (!receiver || !mark)
    return nullptr;
  while (receiver->is(TypeKind::Pointer))
    receiver = receiver->pointee();
  if (receiver->isOpaque()) {
    if (!typeConformsTo(receiver, mark))
      return nullptr;
    receiver = seeThroughOpaque(receiver, SourceRange());
    if (receiver->isError())
      return nullptr;
  }
  auto it = MarkImpls.find({receiver, mark});
  if (it != MarkImpls.end()) {
    auto mit = it->second.find(name);
    if (mit != it->second.end())
      return mit->second;
  }
  // `mark Ord: Eq` — asking Ord for `equals` should find Eq's binding.
  for (MarkDecl *sup : mark->Supers)
    if (FunctionDecl *f = lookupMarkMethod(receiver, sup, name))
      return f;
  // A class inherits its superclass's bindings.
  if (receiver->isNominal())
    if (auto *c = dyn_cast<ClassDecl>(static_cast<Decl *>(receiver->nominal())))
      for (ClassDecl *k = c->Super; k; k = k->Super)
        if (k->DeclaredType)
          if (FunctionDecl *f = lookupMarkMethod(k->DeclaredType, mark, name))
            return f;
  return nullptr;
}

/// One level of `Self` substitution: the requirement's own type stands for the
/// implementing type, including behind a single borrow, which covers `Self`,
/// `&self` and `-> Self`.
/// True when `markTy` — the mark standing in for `Self` — appears anywhere
/// inside `t`. Walks type arguments rather than fields, so it terminates on a
/// type that contains itself.
static bool mentions(Type *t, Type *markTy) {
  if (!t || !markTy)
    return false;
  if (t == markTy)
    return true;
  switch (t->kind()) {
  case TypeKind::Pointer:
  case TypeKind::Array:
  case TypeKind::Slice:
    return mentions(t->element(), markTy);
  case TypeKind::Tuple:
    for (Type *e : t->tupleElements())
      if (mentions(e, markTy))
        return true;
    return false;
  case TypeKind::Function:
  case TypeKind::CFunction:
    for (Type *p : t->params())
      if (mentions(p, markTy))
        return true;
    return mentions(t->result(), markTy);
  case TypeKind::Struct:
  case TypeKind::Enum:
  case TypeKind::Class:
  case TypeKind::Mark:
    for (Type *a : t->typeArguments())
      if (mentions(a, markTy))
        return true;
    return false;
  default:
    return false;
  }
}

/// Structural equality that sees past instantiation. A requirement written
/// `-> Adaptor<Self>` reads as the template with arguments; the binding's own
/// annotation resolved to the instantiation cloned from that template. The two
/// describe the same type, and neither is canonical.
static bool sameType(Type *a, Type *b) {
  if (a == b)
    return true;
  if (!a || !b || a->kind() != b->kind())
    return false;
  switch (a->kind()) {
  case TypeKind::Pointer:
    return a->isRawPointer() == b->isRawPointer() &&
           a->isMutablePointer() == b->isMutablePointer() &&
           a->isWeakPointer() == b->isWeakPointer() &&
           sameType(a->pointee(), b->pointee());
  case TypeKind::Array:
    return a->arraySize() == b->arraySize() &&
           sameType(a->element(), b->element());
  case TypeKind::Slice:
    return sameType(a->element(), b->element());
  case TypeKind::Tuple: {
    if (a->tupleElements().size() != b->tupleElements().size())
      return false;
    for (size_t i = 0; i < a->tupleElements().size(); ++i)
      if (!sameType(a->tupleElements()[i], b->tupleElements()[i]))
        return false;
    return true;
  }
  case TypeKind::Function:
  case TypeKind::CFunction: {
    if (a->params().size() != b->params().size() ||
        a->isVariadicFunction() != b->isVariadicFunction())
      return false;
    for (size_t i = 0; i < a->params().size(); ++i)
      if (!sameType(a->params()[i], b->params()[i]))
        return false;
    return sameType(a->result(), b->result());
  }
  case TypeKind::Struct:
  case TypeKind::Enum:
  case TypeKind::Class:
  case TypeKind::Mark: {
    NominalDecl *an = a->nominal();
    NominalDecl *bn = b->nominal();
    NominalDecl *ar = an && an->GenericTemplate ? an->GenericTemplate : an;
    NominalDecl *br = bn && bn->GenericTemplate ? bn->GenericTemplate : bn;
    if (!ar || ar != br)
      return false;
    if (a->typeArguments().size() != b->typeArguments().size())
      return false;
    for (size_t i = 0; i < a->typeArguments().size(); ++i)
      if (!sameType(a->typeArguments()[i], b->typeArguments()[i]))
        return false;
    return a->isUniq() == b->isUniq();
  }
  default:
    return false;
  }
}

/// The requirement's type as the implementing type reads it: every `Self`
/// becomes `target`, however deeply it is buried. `-> Self` and `&self` are
/// the common cases; `-> Map<Self, B>` is what makes an adaptor expressible as
/// a default method on the mark itself.
Type *Sema::readSelfAs(Type *t, Type *markTy, Type *target, SourceRange at) {
  if (!t || !markTy || !target)
    return t;
  if (t == markTy)
    return target;
  if (!mentions(t, markTy))
    return t;
  switch (t->kind()) {
  case TypeKind::Pointer:
    return Types.pointerTo(readSelfAs(t->pointee(), markTy, target, at),
                           t->isMutablePointer(), t->isRawPointer(),
                           t->isWeakPointer());
  case TypeKind::Array:
    return Types.arrayOf(readSelfAs(t->element(), markTy, target, at),
                         t->arraySize());
  case TypeKind::Slice:
    return Types.sliceOf(readSelfAs(t->element(), markTy, target, at));
  case TypeKind::Tuple: {
    std::vector<Type *> elems;
    for (Type *e : t->tupleElements())
      elems.push_back(readSelfAs(e, markTy, target, at));
    return Types.tupleOf(std::move(elems));
  }
  case TypeKind::Function:
  case TypeKind::CFunction: {
    std::vector<Type *> params;
    for (Type *p : t->params())
      params.push_back(readSelfAs(p, markTy, target, at));
    Type *ret = readSelfAs(t->result(), markTy, target, at);
    return t->is(TypeKind::CFunction)
               ? Types.cfunctionOf(std::move(params), ret,
                                   t->isVariadicFunction())
               : Types.functionOf(std::move(params), ret,
                                  t->isVariadicFunction());
  }
  case TypeKind::Struct:
  case TypeKind::Enum:
  case TypeKind::Class:
  case TypeKind::Mark: {
    if (t->typeArguments().empty())
      return t;
    std::vector<Type *> args;
    for (Type *a : t->typeArguments())
      args.push_back(readSelfAs(a, markTy, target, at));
    NominalDecl *nd = t->nominal();
    NominalDecl *tmpl = nd && nd->GenericTemplate ? nd->GenericTemplate : nd;
    if (!tmpl)
      return t;
    // The parameterised form, deliberately: instantiating here would build a
    // type per check, and `-> Adaptor<Self>` on a mark would ask for one
    // wrapping itself, forever. `sameType` compares the two forms instead.
    (void)at;
    return Types.nominalOf(tmpl, std::move(args));
  }
  default:
    return t;
  }
}

void Sema::checkRequirementSignature(FunctionDecl *req, FunctionDecl *impl,
                                     MarkDecl *mark, Type *target) {
  if (!req || !impl || !req->Ty || !impl->Ty || !mark || !target)
    return;
  if (!req->Generics.empty() || !impl->Generics.empty())
    return; // compared per instantiation, not here
  Type *markTy = mark->DeclaredType;
  const auto &want = req->Ty->params();
  const auto &got = impl->Ty->params();
  if (want.size() != got.size()) {
    auto d = Diags.error(impl->NameRange.isValid() ? impl->NameRange
                                                   : impl->Range,
                         "'{}' takes {} parameter(s) — mark '{}' asks for {}",
                         impl->Name, got.size(), mark->Name, want.size());
    d.code(224);
    noteDeclaredAt(d, req, "required here", "the parameter lists must match");
    return;
  }
  for (size_t i = 0; i < want.size(); ++i) {
    Type *expect = readSelfAs(want[i], markTy, target, impl->Range);
    if (!expect || !got[i] || sameType(expect, got[i]) || expect->isError() ||
        got[i]->isError())
      continue;
    // An associated type is whatever the binding chose, so nothing to check.
    if (expect->containsGenericParam())
      continue;
    SourceRange at = i < impl->Params.size() && impl->Params[i].Range.isValid()
                         ? impl->Params[i].Range
                         : impl->Range;
    auto d = Diags.error(at, "expected '{}' — got '{}'", expect->toString(),
                         got[i]->toString());
    d.code(224);
    if (want[i] == markTy || (want[i]->is(TypeKind::Pointer) &&
                              want[i]->pointee() == markTy))
      d.note(fmt("`Self` in mark '{}' means '{}' here", mark->Name,
                    target->toString())
                 .c_str());
    noteDeclaredAt(d, req, "required here", "this parameter's type is fixed");
    return;
  }
  Type *wantRet = readSelfAs(req->Ty->result(), markTy, target, impl->Range);
  Type *gotRet = impl->Ty->result();
  if (wantRet && gotRet && !sameType(wantRet, gotRet) && !wantRet->isError() &&
      !gotRet->isError() && !wantRet->containsGenericParam()) {
    auto d = Diags.error(impl->NameRange.isValid() ? impl->NameRange
                                                   : impl->Range,
                         "'{}' returns '{}' — mark '{}' asks for '{}'",
                         impl->Name, gotRet->toString(), mark->Name,
                         wantRet->toString());
    d.code(224);
    if (req->Ty->result() == markTy)
      d.note(fmt("`Self` in mark '{}' means '{}' here", mark->Name,
                    target->toString())
                 .c_str());
    noteDeclaredAt(d, req, "required here", "the return type is fixed");
  }
}

/// The implementation of `req` among everything bound to `target` through
/// `mark`. A mark may be bound more than once, each binding taking something
/// different, and it is the version with the signature the mark asked for
/// that answers the requirement — the rest are extra ways to call the name.
FunctionDecl *Sema::conformingImpl(Type *target, MarkDecl *mark,
                                   FunctionDecl *req, FunctionDecl *fallback) {
  std::vector<FunctionDecl *> cands = markImplSet(target, mark, req->Name);
  if (cands.size() < 2)
    return fallback;
  ensureTemplateSignature(req);
  std::vector<Type *> want;
  for (const Param &p : req->Params)
    if (!p.IsSelf)
      want.push_back(readSelfAs(p.Ty, mark->DeclaredType, target, req->Range));
  FunctionDecl *best = nullptr;
  for (FunctionDecl *fn : cands) {
    if (!fn->Body)
      continue;
    ensureTemplateSignature(fn);
    std::vector<Type *> got;
    for (const Param &p : fn->Params)
      if (!p.IsSelf)
        got.push_back(p.Ty);
    if (got.size() != want.size())
      continue;
    bool fits = true;
    for (size_t i = 0; i < got.size() && fits; ++i)
      fits = !want[i] || !got[i] || want[i]->isError() || got[i]->isError() ||
             want[i]->containsGenericParam() || sameType(want[i], got[i]);
    if (fits && !best)
      best = fn;
  }
  return best ? best : fallback;
}

void Sema::checkMarkConformance(BindDecl *b) {
  MarkDecl *mark = b->ResolvedMark;
  if (!mark || !b->ResolvedTarget || b->ResolvedTarget->isError())
    return;
  auto &table = MarkImpls[{b->ResolvedTarget, mark}];
  for (auto &req : mark->Methods) {
    auto it = table.find(req->Name);
    if (it != table.end() && it->second) {
      // With several bindings in play the one the mark asked for may not be
      // the one that happened to be registered last. Settle that here, so
      // `value::Mark.name()` and `dyn Mark` reach the version the mark
      // declared and the extras stay reachable by their own parameters.
      FunctionDecl *chosen =
          conformingImpl(b->ResolvedTarget, mark, req.get(), it->second);
      if (chosen != it->second)
        it->second = chosen;
    }
    if (it != table.end() && it->second->Body)
      checkRequirementSignature(req.get(), it->second, mark, b->ResolvedTarget);
    if (req->Body)
      continue; // has a default
    if (it == table.end() || it->second->Body == nullptr) {
      auto d = Diags.error(b->TargetType ? b->TargetType->Range : b->Range,
                           "'{}' does not implement '{}' required by mark '{}'",
                           b->ResolvedTarget->toString(), req->Name, mark->Name);
      d.note("add the missing method to this bind block, or give the mark a "
             "default implementation")
          .code(220);
      noteDeclaredAt(d, req.get(), "required here",
                     "this requirement has no default body");
    }
  }
  // Super-marks must be satisfied too.
  for (MarkDecl *sup : mark->Supers) {
    if (typeConformsTo(b->ResolvedTarget, sup))
      continue;
    auto d = Diags.error(b->MarkRange,
                         "mark '{}' requires '{}', but '{}' is not bound to it",
                         mark->Name, sup->Name, b->ResolvedTarget->toString());
    d.note(fmt("add `bind {} to {}`", sup->Name,
                  b->ResolvedTarget->toString())
               .c_str())
        .code(221);
    noteDeclaredAt(d, mark, "the mark declared here",
                   "super-marks must be satisfied first");
  }
}

void Sema::checkBind(BindDecl *b) {
  if (!b->Generics.empty() || !b->ResolvedTarget)
    return;
  if (!b->IsOperatorBinding)
    checkMarkConformance(b);

  ActiveSelfType = b->ResolvedTarget;
  auto *cls = b->ResolvedTarget->isNominal()
                  ? dyn_cast<ClassDecl>(
                        static_cast<Decl *>(b->ResolvedTarget->nominal()))
                  : nullptr;
  for (auto &fn : b->Methods) {
    if (!fn->Generics.empty() || !fn->Body)
      continue;
    checkFunction(fn.get(), b->ResolvedTarget, cls);
    if (!CurModule || !CurModule->FromLibrary)
      Result.Functions.push_back(fn.get());
  }
  ActiveSelfType = nullptr;
}

void Sema::checkGlobal(GlobalVarDecl *g) {
  // The annotation was resolved in the signature pass; only an unannotated
  // global still needs its type inferred from the initialiser.
  Type *declared = g->Ty;
  if (!declared && g->TypeAnnotation)
    declared = resolveTypeOrError(g->TypeAnnotation.get(), nullptr);
  if (g->Init) {
    FnStack.push_back(FunctionContext{});
    pushScope(ScopeKind::Function);
    Type *init = checkExpr(g->Init.get(), declared);
    popScope();
    FnStack.pop_back();
    if (declared)
      requireConvertible(g->Init.get(), init, declared, "this initialiser");
    else
      declared = init;
  }
  if (!declared) {
    Diags.error(g->Range, "global '{}' needs a type or an initialiser", g->Name)
        .note("write `global {}: i64 = 0` or give it a value to infer from")
        .code(222);
    declared = Types.errorType();
  }
  g->Ty = declared;
}

/// What an `async fn` may take. Its body runs as a task, on a stack of its
/// own, and may still be running after the call that started it has
/// returned — so every parameter is captured by value into the task, and a
/// borrow of something the caller owns would point at a slot the caller may
/// have left. A shared borrow of a heap handle — `&Counter` for a class,
/// `&String` — is the handle itself, kept alive by the capture, and is fine;
/// a `&var` of anything, or a `&` of a value type, is refused.
void Sema::checkAsyncSignature(FunctionDecl *fn) {
  for (const Param &p : fn->Params) {
    Type *t = p.Ty;
    if (!t || t->isError() || !t->is(TypeKind::Pointer) || t->isRawPointer())
      continue;
    bool sharedHandle = !t->isMutablePointer() && !t->isWeakPointer() &&
                        t->pointee() && t->pointee()->isHeapHandle();
    if (sharedHandle)
      continue;
    if (p.IsSelf) {
      auto d = Diags.error(p.Range, "an `async` method on a '{}' cannot "
                                    "borrow `self`",
                           t->pointee()->toString());
      d.note("the body runs as a task that may outlive the call, so `&self` "
             "would point at a slot the caller has left; take `self` by "
             "value, or make the type a class")
          .code(284);
      continue;
    }
    auto d = Diags.error(p.Range, "an `async fn` cannot take '{}' by borrow",
                         p.Name);
    if (t->isMutablePointer())
      d.note("the body runs as a task that may outlive the call, and a "
             "`&var` points at the caller's own slot; take it by value, or "
             "share a class — a `mem::Checked<T>` for something that "
             "has to change");
    else
      d.note(fmt("the body runs as a task that may outlive the call, and "
                 "a `&{}` points at the caller's own slot; take it by value",
                 t->pointee()->toString())
                 .c_str());
    d.code(284);
  }
}

void Sema::checkFunction(FunctionDecl *fn, Type *selfType, ClassDecl *selfClass) {
  if (!fn->Body)
    return;
  // A body is checked once. Every instantiation works on its own clone, so
  // reaching the same declaration twice means two passes have both claimed
  // it — and the second would re-resolve names against locals the first
  // already bound.
  if (!BodiesChecked.insert(fn).second)
    return;
  // What the signature says about borrows — `from` clauses and views — is
  // resolved here, where every parameter has its type and its binding.
  resolveSignatureAnnotations(fn);

  auto savedGenerics = ActiveGenericParams;
  Type *savedSelf = ActiveSelfType;
  Scope *savedScope = CurScope;
  Module *savedModule = CurModule;
  // A body resolves the names visible where it was *written*. That is the
  // current module for almost everything, and the mark's module for a default
  // copied into a binding somewhere else.
  if (!fn->ModulePath.empty() && (!CurModule || fn->ModulePath != CurModule->Name)) {
    auto mit = ModulesByName.find(fn->ModulePath);
    if (mit != ModulesByName.end()) {
      CurModule = mit->second;
      CurScope = scopeForModule(mit->second);
    }
  }
  bindGenerics(fn->Generics, Types, ActiveGenericParams);
  if (selfType)
    ActiveSelfType = selfType;

  FunctionContext ctx;
  ctx.Fn = fn;
  ctx.ReturnType = fn->Ty ? fn->Ty->result() : Types.voidType();
  ctx.SelfType = selfType;
  ctx.SelfClass = selfClass;
  // @unsafe and @safe("...") both silence unsafe-operation reports inside.
  ctx.InUnsafeContext = fn->IsUnsafe || fn->IsSafeJustified ||
                        Safety == SafetyLevel::None;
  FnStack.push_back(ctx);

  pushScope(ScopeKind::Function);
  unsigned depth = static_cast<unsigned>(FnStack.size());

  for (Param &p : fn->Params) {
    if (!p.Ty)
      p.Ty = Types.errorType();
    VarDecl *v = declareLocal(p.Name, p.Ty, p.IsSelf ? p.SelfMutable : p.IsMutable,
                              p.Range, /*isParam=*/true);
    p.Binding = v;
    VarDepth[v] = depth;
    if (p.IsSelf)
      FnStack.back().SelfVar = v;
    if (p.DefaultValue) {
      Type *dt = checkExpr(p.DefaultValue.get(), p.Ty);
      requireConvertible(p.DefaultValue.get(), dt, p.Ty, "this default value");
    }
  }

  if (fn->IsAsync)
    checkAsyncSignature(fn);

  Type *bodyType = checkBlock(fn->Body.get(), FnStack.back().ReturnType);
  Type *want = FnStack.back().ReturnType;
  if (fn->Body->Tail && !want->isVoid()) {
    if (insertImplicitConversion(fn->Body->Tail, bodyType, want))
      bodyType = fn->Body->Tail->Ty;
    requireConvertible(fn->Body->Tail.get(), bodyType, want,
                       "this function's result");
  }

  // A `some` the body never fixed: nothing returned a value. Reported here
  // rather than left as an unresolved type for the callers to trip over.
  if (want->isOpaque() && want->opaqueOwner() == fn &&
      !want->opaqueUnderlying()) {
    if (!Diags.hadError())
      Diags.error(fn->ReturnType ? fn->ReturnType->Range : fn->Range,
                  "'{}' never returns a value, so its `{}` cannot be fixed",
                  fn->Name, want->toString())
          .note("a `some` is the type of what the body returns; a body that "
                "only diverges decides nothing")
          .code(215);
    Types.resolveOpaque(want, Types.errorType());
  }

  // A non-void function whose body cannot fall off the end is fine; otherwise
  // point at the closing brace.
  if (!want->isVoid() && !want->isError() && !fn->Body->Tail &&
      !bodyType->isNever()) {
    auto d = Diags.error(fn->Body->Range,
                         "'{}' must produce a value of type '{}'", fn->Name,
                         want->toString());
    d.note("end the body with an expression, or use `return`").code(223);
    if (fn->ReturnType)
      d.related(fn->ReturnType->Range, "the declared result type",
                "either return a value or drop the `->` clause");
  }

  // Types are resolved and every name is bound, so the body can be read for
  // what it does with what it owns: borrows that outlive their source,
  // borrows that overlap when one can write, and — for the code generator —
  // which locals never leave this scope at all.
  //
  // Nothing here is checked in a generic template. Its `T` is not a type yet,
  // so whether a value owns a resource has no answer; the instantiation is
  // checked instead, where it does.
  // Queued rather than run: see `OwnershipQueue`. Nothing later in checking
  // reads what it records — the code generator does — so it can wait until
  // there is a whole set of them to do at once.
  if (fn->Generics.empty() && !fn->IsImported &&
      (!CurModule || !CurModule->FromLibrary) &&
      OwnershipQueued.insert(fn).second)
    OwnershipQueue.push_back(fn);

  popScope();
  FnStack.pop_back();
  ActiveGenericParams = savedGenerics;
  ActiveSelfType = savedSelf;
  CurScope = savedScope;
  CurModule = savedModule;
}

//===----------------------------------------------------------------------===//
// Helpers
//===----------------------------------------------------------------------===//

VarDecl *Sema::declareLocal(const std::string &name, Type *ty, bool isMutable,
                            SourceRange range, bool isParam) {
  Synthesised.push_back(std::make_unique<VarDecl>());
  auto *v = static_cast<VarDecl *>(Synthesised.back().get());
  v->Name = name;
  v->NameRange = range;
  v->Range = range;
  v->Ty = ty;
  v->IsMutable = isMutable;
  v->IsParam = isParam;

  Symbol s;
  s.Kind = SymbolKind::Value;
  s.Name = name;
  s.D = v;
  s.Ty = ty;
  if (name != "_")
    CurScope->overwrite(s);
  VarDepth[v] = static_cast<unsigned>(FnStack.size());
  return v;
}

void Sema::noteCapture(VarDecl *v, SourceRange range) {
  auto it = VarDepth.find(v);
  if (it == VarDepth.end())
    return;
  unsigned declDepth = it->second;
  unsigned here = static_cast<unsigned>(FnStack.size());
  if (declDepth >= here)
    return; // local to the current function

  v->IsCaptured = true;
  // Thread the capture through every closure between the declaration and use.
  for (unsigned d = declDepth; d < here; ++d) {
    FunctionContext &fc = FnStack[d];
    if (!fc.Closure)
      continue;
    bool present = false;
    for (const Capture &c : fc.Closure->Captures)
      if (c.Var == v)
        present = true;
    if (present)
      continue;
    Capture c;
    c.Var = v;
    c.Name = v->Name;
    c.Ty = v->Ty;
    c.Index = static_cast<unsigned>(fc.Closure->Captures.size());
    fc.Closure->Captures.push_back(c);
  }
}

/// True when the expression is an explicit `move`.
static bool isa_or_null_move(const Expr *e) {
  return e && isa<MoveExpr>(e);
}

void Sema::requireConvertible(Expr *e, Type *from, Type *to,
                              const char *context) {
  if (!from || !to || from->isError() || to->isError())
    return;

  // Inside the function that returns it, a `some Mark` is the type the
  // body produces: the first value fixes it, and every later one has to be
  // the same. Nowhere else is the type behind it reachable.
  if (to->isOpaque() && fn() && fn()->Fn == to->opaqueOwner()) {
    if (from->isNever())
      return;
    if (!to->opaqueUnderlying()) {
      MarkDecl *mark = to->opaqueMark();
      Type *chosen = from;
      if (chosen == to) {
        // `fn f() -> some Shape { f() }`: the only value is its own result.
        auto d = Diags.error(e ? e->Range : SourceRange(),
                             "the result type of '{}' depends on itself",
                             fn()->Fn->Name);
        d.note("a `some` is fixed by the first value the body returns, and "
               "this one returns itself before anything else")
            .code(215);
        chosen = Types.errorType();
      } else if (chosen->isOpaque() && chosen->opaqueUnderlying()) {
        chosen = chosen->opaqueUnderlying();
      }
      if (chosen->isError()) {
        // reported just above, or earlier
      } else if (chosen->isUniq()) {
        Diags.error(e ? e->Range : SourceRange(),
                    "a `Unique` reference cannot be a `some`: its one owner "
                    "would be hidden")
            .code(216);
        chosen = Types.errorType();
      } else if (mark && !typeConformsTo(chosen, mark)) {
        auto d = Diags.error(e ? e->Range : SourceRange(),
                             "'{}' is not bound to mark '{}'",
                             chosen->toString(),
                             static_cast<Decl *>(mark)->Name);
        d.note(fmt("this is what '{}' returns, and its result is declared "
                   "`some {}`",
                   fn()->Fn->Name, static_cast<Decl *>(mark)->Name)
                   .c_str());
        d.note(fmt("add `bind {} to {}`, or return something that has it",
                   static_cast<Decl *>(mark)->Name, chosen->toString())
                   .c_str())
            .code(241);
        chosen = Types.errorType();
      }
      Types.resolveOpaque(to, chosen);
      return;
    }
    Type *fixed = to->opaqueUnderlying();
    if (from == fixed || fixed->isError())
      return;
    if (from->isOpaque() || !isImplicitlyConvertible(from, fixed)) {
      auto d = Diags.error(e ? e->Range : SourceRange(),
                           "expected '{}' — got '{}'", fixed->toString(),
                           from->toString());
      d.note(fmt("every value '{}' returns has to be the same type, because "
                 "its `some {}` stands for exactly one",
                 fn()->Fn->Name,
                 to->opaqueMark()
                     ? static_cast<Decl *>(to->opaqueMark())->Name
                     : std::string("?"))
                 .c_str())
          .code(230);
      return;
    }
    to = fixed;
  }

  // A `uniq` reference may be handed over, or looked through, but never
  // duplicated. This is the funnel every store passes through — a binding's
  // initialiser, an assignment, a call argument, a return — so it is where
  // the single-owner rule is enforced.
  if (from->isUniq() && !inGenericBody()) {
    // Borrowing looks through the owner without becoming a second one, so it
    // takes nothing away.
    const bool borrowing = to->is(TypeKind::Pointer) && !to->isRawPointer();
    if (!borrowing && !isFreshConstruction(e) && !isa_or_null_move(e)) {
      // The destination is a `Unique` slot, possibly an optional one —
      // `next: Unique<Node>?` is how a link in a list is written.
      Type *dest = isOptionType(to) ? optionPayload(to) : to;
      if (dest && dest->isUniq()) {
        // Landing in another `Unique` slot *is* the transfer, so it happens
        // on its own. Nothing else may hold the value afterwards.
        markMoved(e);
      } else {
        auto d = Diags.error(e ? e->Range : SourceRange(),
                             "cannot copy a `Unique` reference into {}",
                             context);
        d.note("a `Unique` reference has exactly one owner — that is what "
               "keeps it out of a reference cycle");
        d.note(fmt("borrow it instead — declare this `&{}` — or hand it over "
                   "with `.$move()`",
                   TypeContext::stripUniq(from)->toString())
                   .c_str());
        d.code(239);
        return;
      }
    }
  }

  // A value that owns something a reference count cannot see — a struct with
  // a `deinit` — is handed over rather than copied when it is stored
  // somewhere that outlives the binding it came from. The binding stops
  // owning it, so the scope it lives in no longer destroys it.
  markOwnedMove(e, from, to);

  // An `Any` takes a copy of whatever it is handed, and copying is exactly
  // what a `uniq` reference does not do.
  if (to->isAny() && !from->isAny() && !from->isNever() &&
      !isStorableInAny(from)) {
    auto d = Diags.error(e ? e->Range : SourceRange(),
                         "'{}' cannot be stored in an `Any`", from->toString());
    if (from->isUniq())
      d.note("an `Any` takes a reference of its own, and a `Unique` has room "
             "for exactly one owner")
          .note("hold it as a plain class reference if it has to go into an "
                "`Any`");
    else
      d.note("only a value with a run-time representation can go in; `()` "
             "and `!` have none");
    d.code(244);
    return;
  }

  // Boxing into a mark object is only allowed once the mark is actually bound
  // to the value's type.
  if (to->is(TypeKind::DynMark) && !from->is(TypeKind::DynMark)) {
    MarkDecl *mk = to->mark();
    if (mk && !typeConformsTo(from, mk)) {
      auto d = Diags.error(e ? e->Range : SourceRange(),
                           "'{}' is not bound to mark '{}'", from->toString(),
                           mk->Name);
      d.note(fmt("add `bind {} to {}` before using it as `dyn {}`", mk->Name,
                 from->toString(), mk->Name)
                 .c_str())
          .code(241);
      noteDeclaredAt(d, mk, fmt("'{}' declared here", mk->Name),
                     "every method the mark requires must be implemented");
    }
    return;
  }

  if (isImplicitlyConvertible(from, to))
    return;
  auto d = Diags.error(e ? e->Range : SourceRange(),
                       "expected '{}' — got '{}'", to->toString(),
                       from->toString());
  d.note(fmt("in {}", context).c_str()).code(230);
  if (from->isNumeric() && to->isNumeric())
    d.note(fmt("numeric conversions that can lose information must be "
                  "written out: `value as {}`", to->toString())
               .c_str());
  if (isOptionType(to) && !isOptionType(from))
    d.note("wrap it with `Option::Some(...)`, or use `nil` for the empty case");
}

bool Sema::isLValue(const Expr *e) const {
  if (!e)
    return false;
  switch (e->Kind) {
  case NodeKind::DeclRef:
  case NodeKind::SelfRef:
    return true;
  case NodeKind::Member:
    return isLValue(cast<MemberExpr>(e)->Base.get()) ||
           (cast<MemberExpr>(e)->Base->Ty &&
            cast<MemberExpr>(e)->Base->Ty->isPointerLike());
  case NodeKind::Index:
    // A character read out of a String is a value: there is no slot.
    if (cast<IndexExpr>(e)->StringChar)
      return false;
    return isLValue(cast<IndexExpr>(e)->Base.get()) ||
           (cast<IndexExpr>(e)->Base->Ty &&
            cast<IndexExpr>(e)->Base->Ty->isPointerLike());
  case NodeKind::Deref:
    return true;
  default:
    return false;
  }
}

bool Sema::requireMutable(Expr *e, const char *action) {
  // Walk to the root binding; that is what carries mutability.
  const Expr *root = e;
  for (;;) {
    if (const auto *m = dyn_cast<MemberExpr>(root)) {
      if (m->Base->Ty && m->Base->Ty->is(TypeKind::Class))
        return true; // class fields are reachable through a shared reference
      // A field reached through a borrow: it is the borrow's mutability that
      // decides, not the binding holding it. `t: &var Table` may write
      // `t.a` however `t` itself was bound.
      Type *bt = m->Base->Ty;
      if (m->AutoDerefs > 0 && bt && bt->is(TypeKind::Pointer) &&
          !bt->isRawPointer()) {
        if (!bt->isMutablePointer()) {
          auto d = Diags.error(e->Range, "cannot {} a value behind '{}'",
                               action, bt->toString());
          // The borrow may be one the compiler put in to reach through a
          // stand-in. Saying which stand-in, and that it lends for reading
          // only, is more use than talking about a `&` nobody wrote.
          const auto *call = dyn_cast<CallExpr>(m->Base.get());
          if (call && call->PointeeAccess) {
            Type *holder = nullptr;
            if (const auto *callee = dyn_cast<MemberExpr>(call->Callee.get()))
              holder = callee->Base ? callee->Base->Ty : nullptr;
            d.note(fmt("'{}' lends what it holds for reading; it has no "
                       "`touch`, so there is no way to write through it",
                       holder ? holder->toString() : std::string("this value"))
                       .c_str());
            d.note("a shared value changes through a `std::mem::Checked<T>` "
                   "kept inside it, which decides at run time that the write "
                   "is the only one");
          } else {
            d.note("borrow it mutably with `&var` to allow writes");
          }
          d.code(231);
          return false;
        }
        return true;
      }
      root = m->Base.get();
      continue;
    }
    if (const auto *i = dyn_cast<IndexExpr>(root)) {
      // `p[n] = v` writes through the pointer, so it is the pointer's own
      // mutability that matters, not that of the binding holding it.
      Type *bt = i->Base->Ty;
      if (i->ThroughRawPointer && bt && bt->is(TypeKind::Pointer)) {
        if (!bt->isMutablePointer()) {
          Diags.error(e->Range, "cannot {} through '{}'", action,
                      bt->toString())
              .note("a `*T` is read-only; write `*var T` to allow stores")
              .code(231);
          return false;
        }
        return true;
      }
      root = i->Base.get();
      continue;
    }
    if (const auto *d = dyn_cast<DerefExpr>(root)) {
      Type *pt = d->Operand->Ty;
      if (pt && pt->is(TypeKind::Pointer) && !pt->isMutablePointer()) {
        Diags.error(e->Range, "cannot {} through '{}'", action, pt->toString())
            .note("borrow it mutably with `&var` to allow writes")
            .code(231);
        return false;
      }
      return true;
    }
    break;
  }

  if (const auto *r = dyn_cast<DeclRefExpr>(root)) {
    auto *v = dyn_cast<VarDecl>(r->Resolved);
    if (v && !v->IsMutable) {
      auto d = Diags.error(e->Range, "cannot {} immutable binding '{}'", action,
                           v->Name);
      d.note("bindings are immutable by default").code(232);
      d.related(v->NameRange, fmt("'{}' is bound here", v->Name),
                fmt("declare it as `var {}` to allow mutation", v->Name));
      return false;
    }
    if (auto *g = dyn_cast<GlobalVarDecl>(r->Resolved)) {
      if (!g->IsMutable) {
        auto d = Diags.error(e->Range, "cannot {} immutable global '{}'", action,
                             g->Name);
        d.code(232);
        d.related(g->NameRange, fmt("'{}' is declared here", g->Name),
                  fmt("declare it as `global var {}` to allow mutation",
                         g->Name));
        return false;
      }
    }
    return true;
  }
  if (const auto *s = dyn_cast<SelfExpr>(root)) {
    if (s->Binding && !s->Binding->IsMutable) {
      Diags.error(e->Range, "cannot {} through an immutable `self`", action)
          .note("declare the method with `&var self` to allow mutation")
          .code(232);
      return false;
    }
    return true;
  }
  return true;
}

void Sema::reportUnsafe(SourceRange range, const std::string &what,
                        const std::string &suggestion) {
  if (Safety == SafetyLevel::None)
    return;
  FunctionContext *f = fn();
  if (f && f->InUnsafeContext)
    return;
  auto d = Diags.warn(range, "{} in a safe context", what);
  d.note(suggestion.c_str()).code(1);
  if (f && f->Fn && f->Fn->NameRange.isValid())
    d.related(f->Fn->NameRange,
              fmt("'{}' is not marked unsafe", f->Fn->Name),
              "annotate it with @unsafe, or justify the use with "
              "@safe(\"reason\")");
}

Type *Sema::unifyBranches(Type *a, Type *b, Expr *second, const char *context) {
  if (!a || a->isError())
    return b ? b : Types.errorType();
  if (!b || b->isError())
    return a;
  if (a->isNever())
    return b;
  if (b->isNever())
    return a;
  if (a == b)
    return a;
  if (isImplicitlyConvertible(b, a))
    return a;
  if (isImplicitlyConvertible(a, b))
    return b;
  auto d = Diags.error(second ? second->Range : SourceRange(),
                       "expected '{}' — got '{}'", a->toString(), b->toString());
  d.note(fmt("every branch of {} must produce the same type", context).c_str())
      .code(233);
  return a;
}

//===----------------------------------------------------------------------===//
// Method lookup, marks and builtins
//===----------------------------------------------------------------------===//

/// `As<Fahrenheit>` rather than a bare `As`, so a message about a generic
/// mark names the one that was actually meant.
static std::string markName(const MarkDecl *mk) {
  std::string out = static_cast<const Decl *>(mk)->Name;
  if (mk->TypeArguments.empty())
    return out;
  out += "<";
  for (size_t i = 0; i < mk->TypeArguments.size(); ++i) {
    if (i) out += ", ";
    out += mk->TypeArguments[i]->toString();
  }
  return out + ">";
}

bool Sema::checkGenericBound(Type *arg, TypeRepr *bound, SourceRange at,
                             SourceRange declaredAt, const std::string &owner) {
  if (!arg || !bound || arg->isError())
    return true;
  // `T: operator::cmp` — the argument has to overload that operator rather
  // than implement a mark.
  if (auto *named = dyn_cast<NamedTypeRepr>(bound)) {
    if (named->Path.size() == 2 && named->Path[0] == "operator") {
      std::string op = canonicalOperatorName(named->Path[1]);
      if (lookupOperator(arg, op))
        return true;
      // A builtin type does not `bind` the operators the language already
      // gives it — `3 < 9` needs nobody's permission. The bound asks whether
      // the operator *works* on `T`, not whether someone wrote it out, so a
      // type the compiler handles itself satisfies it.
      if (builtinOperatorApplies(op, arg, arg))
        return true;
      auto d = Diags.error(at, "'{}' does not overload `operator::{}`",
                           arg->toString(), op);
      d.note(fmt("add `bind operator::{} to {}` to satisfy this bound", op,
                    arg->toString())
                 .c_str())
          .code(242);
      if (declaredAt.isValid())
        d.related(declaredAt, fmt("'{}' is required here", owner),
                  "the bound on this parameter was not met by the call above");
      return false;
    }
  }
  Type *bt = resolveTypeOrError(bound, Types.errorType());
  if (!bt->is(TypeKind::Mark))
    return true;
  auto *mk = reinterpret_cast<MarkDecl *>(bt->nominal());
  if (typeConformsTo(arg, mk))
    return true;
  // `Send` and `Sync` are not bound, they are read off the type, so telling
  // someone to bind one would be telling them to defeat the check.
  if (mk == SendDecl || mk == SyncDecl) {
    const bool sync = mk == SyncDecl;
    auto d = Diags.error(
        at, "'{}' cannot {} threads", arg->toString(),
        sync ? "be shared between" : "cross between");
    d.note("`{}` is not declared — the compiler reads it off the type, so "
           "that it cannot go stale", markName(mk));
    d.note("{}", whyNotThreadSafe(arg));
    d.code(241);
    if (declaredAt.isValid())
      d.related(declaredAt, fmt("'{}' is required here", owner),
                "the bound on this parameter was not met by the call above");
    return false;
  }
  // An automatic mark is not bound either: it is read off the type, so the
  // answer is which part of the type does not have it.
  if (mk->IsAuto) {
    auto d = Diags.error(at, "'{}' does not have the mark '{}'",
                         arg->toString(), markName(mk));
    d.note("`{}` is automatic: a type has it when every part of it has it",
           markName(mk));
    std::string why = whyNotAutoMark(arg, mk);
    if (!why.empty())
      d.note("{}", why);
    bool refused = false;
    if (arg->isNominal())
      for (MarkDecl *m : refusedMarks(arg->nominal()))
        refused = refused || m == mk;
    if (refused)
      d.note("drop the `@never({})` if it should have it after all",
             markName(mk));
    else
      d.note("`bind {} to {}` claims it where the structure cannot say — "
             "which is how `String` and the rest come by theirs",
             markName(mk), arg->toString());
    d.code(241);
    if (declaredAt.isValid())
      d.related(declaredAt, fmt("'{}' is required here", owner),
                "the bound on this parameter was not met by the call above");
    return false;
  }
  auto d = Diags.error(at, "'{}' is not bound to mark '{}'", arg->toString(),
                       markName(mk));
  if (arg->isOpaque() && arg->opaqueMark())
    // Nothing can be bound to a `some`; what it has is what its mark has.
    d.note(fmt("a `some {}` has only what `{}` declares; declare the result "
               "`some {}` instead, or as the concrete type",
               static_cast<Decl *>(arg->opaqueMark())->Name,
               static_cast<Decl *>(arg->opaqueMark())->Name, markName(mk))
               .c_str())
        .code(241);
  else
    d.note(fmt("add `bind {} to {}` to satisfy this bound", markName(mk),
               arg->toString())
               .c_str())
        .code(241);
  if (declaredAt.isValid())
    d.related(declaredAt, fmt("'{}' is required here", owner),
              "the bound on this parameter was not met by the call above");
  return false;
}

/// Checks a `where` list against whatever the generic parameters are
/// currently bound to.
///
/// A `where` clause is a bound like any other; what it adds is a subject that
/// need not be a parameter name. Resolving the subject in the scope the
/// arguments are already bound in is what substitutes them, so
/// `where T::Item: Display` asks about the item type of the argument that was
/// actually passed.
bool Sema::checkWhereClauses(const std::vector<WhereClause> &clauses,
                             SourceRange at, const std::string &owner) {
  bool ok = true;
  for (const WhereClause &w : clauses) {
    if (!w.Subject)
      continue;
    // On a copy of the annotation: the template's own node would cache the
    // first instantiation's answer and hand it to every one after.
    TypeReprPtr copy = cloneTypeRepr(w.Subject.get());
    Type *subject = resolveTypeOrError(copy.get(), nullptr);
    // A subject that does not resolve has been reported already, and a
    // still-generic one belongs to an enclosing template whose own
    // instantiation will ask again.
    if (!subject || subject->isError() || subject->is(TypeKind::Generic))
      continue;
    for (const auto &bound : w.Bounds)
      if (!checkGenericBound(subject, bound.get(), at, w.Range, owner))
        ok = false;
  }
  return ok;
}

bool Sema::methodWhereHolds(FunctionDecl *fn, SourceRange at, bool report) {
  if (!fn || fn->WhereClauses.empty())
    return true;
  auto *nd = fn->Parent ? dyn_cast<NominalDecl>(fn->Parent) : nullptr;
  if (!nd || !nd->GenericTemplate)
    return true; // the template itself: its parameters are still open
  if (!report) {
    auto it = MethodWhereAnswers.find(fn);
    if (it != MethodWhereAnswers.end())
      return it->second;
  }
  Scope *savedScope = CurScope;
  Module *savedModule = CurModule;
  auto savedGenerics = ActiveGenericParams;
  auto mit = ModulesByName.find(fn->ModulePath);
  if (mit != ModulesByName.end()) {
    CurModule = mit->second;
    CurScope = scopeForModule(mit->second);
  }
  ActiveGenericParams.clear();
  bindOwnerGenerics(fn, ActiveGenericParams);
  if (!report)
    Diags.beginSpeculation();
  bool ok = checkWhereClauses(fn->WhereClauses, at, fn->Name);
  if (!report)
    Diags.endSpeculation();
  ActiveGenericParams = savedGenerics;
  CurScope = savedScope;
  CurModule = savedModule;
  MethodWhereAnswers[fn] = ok;
  fn->WhereUnmet = !ok;
  return ok;
}

Type *Sema::associatedTypeFor(Type *owner, MarkDecl *mark,
                              const std::string &name) {
  if (!owner || !mark)
    return nullptr;
  while (owner->is(TypeKind::Pointer))
    owner = owner->pointee();
  // The associated types of a `some Mark` are part of the mark's own
  // interface — `Item` on an iterator — and are answered by the type behind
  // it. That says what comes out, not what the iterator is.
  if (owner->isOpaque()) {
    if (!typeConformsTo(owner, mark))
      return nullptr;
    owner = seeThroughOpaque(owner, SourceRange());
    if (owner->isError())
      return nullptr;
  }
  auto it = AssocTypes.find({owner, mark});
  if (it != AssocTypes.end()) {
    auto nit = it->second.find(name);
    if (nit != it->second.end())
      return nit->second;
  }
  for (MarkDecl *sup : mark->Supers)
    if (Type *t = associatedTypeFor(owner, sup, name))
      return t;
  return nullptr;
}

//===----------------------------------------------------------------------===//
// Strong cycles
//
// Reference counting frees an object when the last reference goes. A ring of
// objects holding each other never reaches zero, so it is never freed. That is
// the one way a fully safe program can still leak, and the only way to rule it
// out without a collector is to refuse the shape that allows it: a class that
// can reach itself through fields that hold strong references.
//
// The walk follows what an object *owns* — its fields, and through structs,
// tuples, arrays, enum payloads and Options — and stops at anything that does
// not keep its target alive: `weak` fields, borrows and raw pointers.
//===----------------------------------------------------------------------===//

void Sema::collectStrongEdges(Type *t, std::vector<ClassDecl *> &out,
                              std::set<Type *> &seen) {
  if (!t || !seen.insert(t).second)
    return;
  switch (t->kind()) {
  case TypeKind::Class:
    if (auto *c = dyn_cast<ClassDecl>(static_cast<Decl *>(t->nominal())))
      out.push_back(c);
    return;
  case TypeKind::Array:
  case TypeKind::Slice:
    collectStrongEdges(t->element(), out, seen);
    return;
  case TypeKind::Tuple:
    for (Type *e : t->tupleElements())
      collectStrongEdges(e, out, seen);
    return;
  case TypeKind::DynMark: {
    // A mark object may hold any type bound to that mark.
    MarkDecl *mk = t->mark();
    for (auto &entry : Conformances)
      for (MarkDecl *m : entry.second)
        if (m == mk)
          collectStrongEdges(entry.first, out, seen);
    return;
  }
  case TypeKind::Struct:
  case TypeKind::Enum: {
    NominalDecl *nd = t->nominal();
    if (!nd)
      return;
    for (auto &f : nd->Fields)
      if (!f->IsWeak)
        collectStrongEdges(f->Ty, out, seen);
    if (auto *e = dyn_cast<EnumDecl>(static_cast<Decl *>(nd)))
      for (auto &v : e->Variants) {
        for (auto &tt : v->TupleTypes)
          collectStrongEdges(tt->Resolved, out, seen);
        for (auto &f : v->Fields)
          if (!f->IsWeak)
            collectStrongEdges(f->Ty, out, seen);
      }
    return;
  }
  default:
    // Borrows and raw pointers do not keep their target alive, and a closure's
    // captures are not part of its type.
    return;
  }
}

/// True when a field's type owns through `uniq` — directly, or as the payload
/// of an Option, which is how a link in a list is written.
static bool uniqEdge(Type *t) {
  if (!t)
    return false;
  if (t->isUniq())
    return true;
  if (isOptionType(t))
    return uniqEdge(optionPayload(t));
  return false;
}

bool Sema::findCycle(ClassDecl *start, ClassDecl *at,
                     std::set<ClassDecl *> &visiting,
                     std::vector<ClassDecl *> &path) {
  std::vector<ClassDecl *> next;
  for (ClassDecl *k = at; k; k = k->Super)
    for (auto &f : k->Fields) {
      if (f->IsWeak)
        continue;
      // A `uniq` edge cannot join a ring: its target has exactly one owner,
      // and a ring needs a second reference to close it.
      if (f->Ty && uniqEdge(f->Ty))
        continue;
      std::set<Type *> seen;
      collectStrongEdges(f->Ty, next, seen);
    }
  for (ClassDecl *to : next) {
    if (to == start) {
      path.push_back(to);
      return true;
    }
    if (!visiting.insert(to).second)
      continue;
    path.push_back(to);
    if (findCycle(start, to, visiting, path))
      return true;
    path.pop_back();
  }
  return false;
}

//===----------------------------------------------------------------------===//
// Decorators
//
// The compiler answers a fixed handful itself. Everything else is a name the
// program has to have declared: a function whose last parameter is a function
// type. `@route("/health")` on `fn health()` means `route("/health", health)`,
// called once before `main`.
//===----------------------------------------------------------------------===//

/// `@zombie_unavailable("alternative")`: the declaration exists so that using
/// it under `--memory zombie` says what to reach for instead.
void Sema::checkAvailableUnderZombie(Decl *d, SourceRange at) {
  if (Memory != MemoryMode::Zombie || !d)
    return;
  const Attribute *a = d->findAttr("zombie_unavailable");
  if (!a)
    return;
  auto e = Diags.error(at, "'{}' is only available with reference counting",
                       d->Name);
  e.note("under `--memory zombie` nothing is counted, so a value has one "
         "owner and cannot be shared");
  if (!a->Args.empty())
    if (auto *s = dyn_cast<StringLitExpr>(a->Args[0].get()))
      e.note("{}", s->Value);
  e.code(292);
}

/// True when a field of type `t` means the same thing to C: a number, a
/// `bool`, a `Character` (a `uint32_t`), a raw pointer, a `CString`, a C
/// function pointer, an array of those, or a type that is `@Convention("C")`
/// itself. `why` names what is not.
static bool representableInC(Type *t, std::string &why) {
  if (!t)
    return true;
  t = t->canonical();
  switch (t->kind()) {
  case TypeKind::Bool:
  case TypeKind::Int:
  case TypeKind::Float:
  case TypeKind::Char:
  case TypeKind::CString:
  case TypeKind::CFunction:
  case TypeKind::Error:
    return true;
  case TypeKind::Pointer:
    if (t->isRawPointer())
      return true;
    why = "a borrow is checked by Rune and means nothing to C; use `*T`";
    return false;
  case TypeKind::Array:
    return representableInC(t->element(), why);
  case TypeKind::Struct:
  case TypeKind::Enum:
    if (NominalDecl *nd = t->nominal())
      if (static_cast<Decl *>(nd)->findAttr("Convention"))
        return true;
    why = "'" + t->toString() + "' is laid out the way Rune likes; give it "
          "`@Convention(\"C\")` too";
    return false;
  default:
    why = "a '" + t->toString() + "' is a Rune value C cannot read";
    return false;
  }
}

/// `@Convention("C")` on a struct or an enum: laid out, and passed by value,
/// exactly as C would — fields in the order written with C's padding and
/// alignment, a payload-free enum as a C `int`, an enum with payloads as a
/// tag followed by a union aligned for its widest member. What is promised
/// has to be possible, so every part must be something C has (E0544).
void Sema::checkConventions() {
  for (Module *m : Modules)
    for (auto &d : m->Decls) {
      auto *nd = dyn_cast<NominalDecl>(d.get());
      if (!nd)
        continue;
      Decl *decl = static_cast<Decl *>(nd);
      const Attribute *a = decl->findAttr("Convention");
      if (!a)
        continue;
      auto *lit = a->Args.size() == 1 ? dyn_cast<StringLitExpr>(a->Args[0].get())
                                      : nullptr;
      if (!lit || lit->Value != "C") {
        Diags.error(a->Range, "`@Convention` takes one convention, and the one "
                              "there is is \"C\"")
            .note("write `@Convention(\"C\")`")
            .code(543);
        continue;
      }
      if (isa<ClassDecl>(decl) || isa<MarkDecl>(decl)) {
        Diags.error(a->Range, "`@Convention(\"C\")` is for a struct or an enum")
            .note("a class is an object Rune allocates and frees; C reaches it "
                  "only through a pointer")
            .code(543);
        continue;
      }
      if (!nd->Generics.empty()) {
        Diags.error(a->Range, "a `@Convention(\"C\")` type cannot be generic")
            .note("C sees one layout per type, so its fields need one type each")
            .code(543);
        continue;
      }
      auto check = [&](Type *t, SourceRange where, const std::string &what) {
        std::string why;
        if (representableInC(t, why))
          return;
        Diags.error(where, "{} cannot be laid out the way C would", what)
            .note("{}", why)
            .note("'{}' is `@Convention(\"C\")`", decl->Name)
            .code(544);
      };
      for (auto &f : nd->Fields)
        check(f->Ty, f->Range, "field '" + f->Name + "'");
      if (auto *e = dyn_cast<EnumDecl>(decl)) {
        if (e->RawFloat)
          Diags.error(a->Range, "a C enum's values are integers")
              .note("'{}' has float values", e->Name)
              .code(544);
        for (auto &v : e->Variants) {
          for (auto &tt : v->TupleTypes)
            check(tt->Resolved, v->Range, "variant '" + v->Name + "'");
          for (auto &fd : v->Fields)
            check(fd->Ty, fd->Range, "variant '" + v->Name + "'");
        }
      }
    }
}

bool Sema::isBuiltinDecorator(const std::string &name) {
  static const std::set<std::string> kBuiltin = {
      "unsafe", "safe",  "inline", "noinline", "export",
      // `@macro` marks a procedural macro inside a macro package. By the
      // time anything is checked the compiler has already read it; here it
      // only has to be a name the checker knows.
      "macro",
      "alias",  "intrinsic", "link", "linkpath", "type",
      "Doc",    "doc",   "sync",   "as",     "resource",
      // `extern "C++"`: the size and alignment of an opaque class, and the
      // C++ operator a member stands for.
      "size",   "align", "operator",
      // Automatic marks: `@auto` on the mark, `@never` on a type that must
      // not have one.
      "auto",   "never",
      // The Zombie borrow checker's own: `@zombie("reason")` trusts a body,
      // `@zombie_unavailable("alternative")` marks a declaration that only
      // exists under reference counting.
      "zombie", "zombie_unavailable",
      // Tasks: `@suspend` marks a function a cancelled task may leave from;
      // `@sendable` asks that a closure handed to it capture only `Send`
      // values, because it will run on another thread.
      "suspend", "sendable",
      // Answered before checking, by `applyConfig`. A declaration still
      // carrying one here is one this build kept, so there is nothing left to
      // do but recognise the name.
      "Config", "config",
      // `@Convention("C")` on a struct or enum: checked by `checkConventions`.
      "Convention",
      // The linter's: `@lint(allow(unused-variable))`. The compiler reads
      // nothing in it — the arguments are rule names, not expressions to
      // check — and only has to know the name.
      "lint",
      // Bare metal: the hooks a freestanding program gives the runtime it is
      // compiled with, and a definition another may replace.
      "panicHandler", "allocator", "deallocator", "output", "weak",
  };
  return kBuiltin.count(name) != 0;
}

/// `@Doc("...")` — prose the compiler keeps rather than runs.
///
/// It is a builtin rather than a user-written decorator because documentation
/// has to be readable without executing the program: a library that is only
/// ever linked against still has to be able to describe itself.
void Sema::collectDoc(Decl *d) {
  if (!d)
    return;
  bool sawDoc = false;
  for (const Attribute &a : d->Attrs) {
    if (a.Name != "Doc" && a.Name != "doc")
      continue;
    if (a.Args.size() != 1) {
      Diags.error(a.Range, "`@{}` takes one string — the text", a.Name)
          .note("write `@Doc(\"...\")`, or a `\"\"\"` block for several lines")
          .code(240);
      continue;
    }
    const auto *lit = dyn_cast<StringLitExpr>(a.Args[0].get());
    if (!lit) {
      Diags.error(a.Args[0]->Range, "`@{}` takes a string literal", a.Name)
          .note("the text is read at compile time, so it cannot be computed")
          .code(240);
      continue;
    }
    // `@Doc` replaces whatever a `///` comment left, and several `@Doc`
    // decorators on one declaration join with a blank line between them.
    if (!sawDoc) {
      d->Doc.clear();
      sawDoc = true;
    } else {
      d->Doc += "\n\n";
    }
    d->Doc += lit->Value;
  }
}

/// `@panicHandler`, `@allocator` and `@deallocator` are called by the
/// generated code and by the freestanding runtime, through a C signature
/// fixed in advance; a function that does not match it would be called with
/// the wrong arguments rather than refused, so it is refused here.
void Sema::checkRuntimeHook(FunctionDecl *fn, const Attribute &a) {
  if (!fn->Ty)
    return;
  Type *bytes = Types.pointerTo(Types.u8(), /*isMutable=*/true, /*isRaw=*/true);
  std::vector<Type *> want;
  Type *wantResult = nullptr;
  const char *spelled = "";
  if (a.Name == "panicHandler") {
    want = {Types.cstringType(), Types.cstringType()};
    wantResult = Types.neverType();
    spelled = "fn(message: CString, location: CString) -> Never";
  } else if (a.Name == "allocator") {
    want = {Types.usize(), Types.usize()};
    wantResult = bytes;
    spelled = "fn(size: usize, align: usize) -> *var u8";
  } else if (a.Name == "output") {
    want = {Types.pointerTo(Types.u8(), /*isMutable=*/false, /*isRaw=*/true),
            Types.usize()};
    wantResult = Types.voidType();
    spelled = "fn(bytes: *u8, count: usize)";
  } else {
    want = {bytes, Types.usize(), Types.usize()};
    wantResult = Types.voidType();
    spelled = "fn(block: *var u8, size: usize, align: usize)";
  }
  bool ok = fn->Params.size() == want.size() && !fn->Parent &&
            fn->Generics.empty();
  for (size_t i = 0; ok && i < want.size(); ++i)
    ok = fn->Params[i].Ty &&
         fn->Params[i].Ty->canonical() == want[i]->canonical();
  Type *result = fn->Ty->result();
  if (ok)
    ok = result && result->canonical() == wantResult->canonical();
  if (!ok) {
    Diags.error(fn->NameRange, "`@{}` needs the signature `{}`", a.Name,
                spelled)
        .note("the generated code calls it through that C signature, and "
              "nothing else would be passed what it expects")
        .code(248);
    return;
  }
  // One of each per program: the linker would take whichever it met first.
  auto [it, inserted] = RuntimeHooks.insert({a.Name, fn});
  if (!inserted && it->second != fn) {
    auto d = Diags.error(fn->NameRange, "a second `@{}`", a.Name);
    d.note("a program has one; the freestanding runtime would call only one "
           "of them")
        .code(249);
    noteDeclaredAt(d, it->second, "the first is here", "");
  }
}

void Sema::checkDecorators(FunctionDecl *fn) {
  if (!fn)
    return;
  collectDoc(fn);
  for (Attribute &a : fn->Attrs) {
    // `@as` renames a foreign declaration. Anywhere else it would silently do
    // nothing, which is worse than saying so: a Rune function's name is
    // already whatever it was written as.
    if (a.Name == "as" && !fn->IsExtern) {
      Diags.error(a.Range, "`@as` only applies inside an `extern` block")
          .note("it renames a foreign declaration for Rune's side, leaving the "
                "symbol the C library exports alone")
          .note("to give a Rune declaration a second name, use `@alias`")
          .code(234);
      continue;
    }
    if (a.Name == "panicHandler" || a.Name == "allocator" ||
        a.Name == "deallocator" || a.Name == "output")
      checkRuntimeHook(fn, a);
    if (isBuiltinDecorator(a.Name))
      continue;

    Symbol *sym = CurScope->find(a.Name);
    auto *dec = sym && sym->D ? dyn_cast<FunctionDecl>(sym->D) : nullptr;
    if (!dec) {
      auto d = Diags.error(a.Range, "no decorator named '{}'", a.Name);
      d.note("a decorator is either one the compiler provides — `@inline`, "
             "`@export`, `@safe` and the rest — or a function you declared "
             "whose last parameter is a function")
          .code(237);
      if (sym)
        d.note(fmt("'{}' is in scope but is not a function", a.Name).c_str());
      continue;
    }
    ensureTemplateSignature(dec);
    if (!dec->Ty)
      continue;
    if (!dec->Generics.empty()) {
      auto d = Diags.error(a.Range, "'{}' is generic, so it cannot be a "
                                    "decorator",
                           a.Name);
      d.note("a decorator is called before `main`, where there is nothing to "
             "infer its type arguments from")
          .code(237);
      noteDeclaredAt(d, dec, "declared here", "drop the type parameters");
      continue;
    }

    const std::vector<Type *> &params = dec->Ty->params();
    if (params.empty() || !params.back()->is(TypeKind::Function)) {
      auto d = Diags.error(a.Range, "'{}' cannot be used as a decorator",
                           a.Name);
      d.note("its last parameter has to be a function — that is where the "
             "decorated function is passed")
          .code(237);
      noteDeclaredAt(d, dec, "declared here",
                     params.empty()
                         ? "it takes no parameters at all"
                         : "the last parameter is not a function type");
      continue;
    }

    const size_t wanted = params.size() - 1;
    if (a.Args.size() != wanted) {
      auto d = Diags.error(a.Range,
                           "'@{}' takes {} argument(s) — {} given", a.Name,
                           wanted, a.Args.size());
      d.code(237);
      noteDeclaredAt(d, dec, "declared here",
                     "the decorated function fills the last parameter");
      continue;
    }

    // The arguments are ordinary expressions, checked against the decorator's
    // own parameters. They run before `main`, so a global is as far as they
    // may reach.
    bool ok = true;
    FnStack.push_back(FunctionContext{});
    pushScope(ScopeKind::Function);
    for (size_t i = 0; i < a.Args.size(); ++i) {
      Type *got = checkExpr(a.Args[i].get(), params[i]);
      if (got->isError() || !isImplicitlyConvertible(got, params[i])) {
        requireConvertible(a.Args[i].get(), got, params[i],
                           fmt("argument {} of `@{}`", i + 1, a.Name).c_str());
        ok = false;
      }
    }
    popScope();
    FnStack.pop_back();
    if (!ok)
      continue;

    // And the decorated function has to be the shape the decorator asks for.
    Type *want = params.back();
    if (fn->Ty && want != fn->Ty) {
      auto d = Diags.error(fn->NameRange.isValid() ? fn->NameRange : fn->Range,
                           "'{}' is '{}', but `@{}` decorates '{}'", fn->Name,
                           fn->Ty->toString(), a.Name, want->toString());
      d.note("a decorator receives the function it is written on, so the two "
             "have to agree")
          .code(237);
      noteDeclaredAt(d, dec, "the decorator", "this is the shape it takes");
      continue;
    }

    fn->Decorators.push_back({dec, &a});
    Result.DecoratorCalls.push_back({dec, &a, fn});
  }
}

/// A decorator is applied to a free function. A method carries `self`, so
/// there is no shape a decorator could agree with, and a decorator runs before
/// `main` when no instance exists yet.
void Sema::rejectMethodDecorators(FunctionDecl *fn) {
  if (!fn)
    return;
  // `@Doc` is prose, not behaviour: it applies to a method as readily as to a
  // free function, so it is collected before the rest are turned away.
  collectDoc(fn);
  for (const Attribute &a : fn->Attrs) {
    if (isBuiltinDecorator(a.Name))
      continue;
    Diags.error(a.Range, "`@{}` cannot decorate a method", a.Name)
        .note("decorators run once before `main`, so they apply to free "
              "functions; call the method from one of those instead")
        .code(237);
  }
}

void Sema::checkStrongCycles() {
  // Reporting only. A type that *can* reach itself is not a program that
  // does, and refusing the shape outright refused every owning list and tree
  // as well. `uniq` is how a program says a structure cannot cycle; this is
  // what tells you when nothing has said it.
  const bool strict = false;

  std::vector<ClassDecl *> classes;
  for (Module *m : Modules)
    for (auto &d : m->Decls)
      if (auto *c = dyn_cast<ClassDecl>(d.get()))
        if (c->Generics.empty() && c->DeclaredType)
          classes.push_back(c);

  for (ClassDecl *c : classes) {
    std::set<ClassDecl *> visiting{c};
    std::vector<ClassDecl *> path;
    if (!findCycle(c, c, visiting, path))
      continue;

    std::string route = c->Name;
    for (ClassDecl *step : path)
      route += " → " + step->Name;

    // Name a field that would break it, so the fix is concrete.
    std::string suggestion;
    for (auto &f : c->Fields) {
      if (f->IsWeak || !f->Ty)
        continue;
      std::vector<ClassDecl *> reach;
      std::set<Type *> seen;
      collectStrongEdges(f->Ty, reach, seen);
      for (ClassDecl *r : reach)
        if (suggestion.empty() && (r == c || !path.empty()))
          suggestion = f->Name;
    }

    SourceRange at = c->NameRange.isValid() ? c->NameRange : c->Range;
    if (strict) {
      auto d = Diags.error(at, "'{}' can reach itself through strong "
                               "references",
                           c->Name);
      d.note(fmt("the route is {}", route).c_str());
      d.note("reference counting frees an object when the last reference "
             "goes, so a ring of them never is");
      if (!suggestion.empty())
        d.note(fmt("mark one edge `weak` to break it, e.g. `weak {}`",
                      suggestion)
                   .c_str());
      d.note("`--safety minimal` and `--safety none` allow this and report "
             "whatever leaks at exit");
      d.code(235);
    } else {
      auto w = Diags.warn(at, "'{}' can reach itself through strong "
                              "references",
                          c->Name);
      w.note(fmt("the route is {}", route).c_str());
      if (!suggestion.empty())
        w.note(fmt("a ring built this way never gets freed; `weak {}` "
                      "would break it", suggestion)
                   .c_str());
      w.code(235);
    }
  }
}

/// Whether a value of `t` may cross a thread boundary, and whether one may be
/// shared across several at once.
///
/// Neither is declared: both are read off the type, the way `mem::is_counted`
/// is, so they stay true by construction as a program changes. The rules are
/// the ones Rust and Swift settled on, said in Rune's terms:
///
///  * A scalar is both. There is nothing to share.
///  * A `String` is both: reference counted, but its contents never change,
///    and the count itself is atomic.
///  * An aggregate is whatever its parts are.
///  * A class is a *shared, mutable* reference: `let` fixes the binding, not
///    the object, so any holder can write to it. Passing one to a thread
///    hands out a second way to reach the same object, so a class is neither
///    unless it says it synchronises itself.
///  * A closure carries an environment, and a raw pointer carries no promise
///    at all; neither is either.
///  * `@sync("reason")` says a type synchronises access itself. It is how
///    `Mutex` becomes `Sync` while holding something that is not, and it is
///    the one place the compiler takes a claim on trust.
bool typeIsThreadSafe(Type *t, bool wantSync,
                      std::set<Type *> &visiting) {
  if (!t || t->isError())
    return false;
  // A type may reach itself through a field; assume the best and let the
  // fields decide, exactly as the layout walk does.
  if (!visiting.insert(t).second)
    return true;
  struct Pop {
    std::set<Type *> &S; Type *T;
    ~Pop() { S.erase(T); }
  } pop{visiting, t};

  switch (t->kind()) {
  case TypeKind::Void:
  case TypeKind::Bool:
  case TypeKind::Int:
  case TypeKind::Float:
  case TypeKind::Char:
  case TypeKind::String:
    return true;
  case TypeKind::Tuple: {
    for (Type *e : t->tupleElements())
      if (!typeIsThreadSafe(e, wantSync, visiting))
        return false;
    return true;
  }
  case TypeKind::Array:
  case TypeKind::Slice:
    return typeIsThreadSafe(t->element(), wantSync, visiting);
  case TypeKind::Struct:
  case TypeKind::Enum:
  case TypeKind::Class: {
    NominalDecl *nd = t->nominal();
    if (!nd)
      return false;
    // The escape hatch, and the only one: a type that says it synchronises
    // its own access is taken at its word.
    if (nd->hasAttr("sync"))
      return true;
    // A class is a reference, and `let` fixes the binding rather than the
    // object: any holder of one can write to its fields. So passing a class
    // to a thread does not hand it over, it hands out a second way to reach
    // the same mutable object — which is exactly what must not happen by
    // accident. `@sync` above is the only way a class gets through, and
    // `Mutex` is what wears it.
    if (t->is(TypeKind::Class))
      return false;
    for (const auto &f : nd->Fields)
      if (!typeIsThreadSafe(f->Ty, wantSync, visiting))
        return false;
    if (auto *e = dyn_cast<EnumDecl>(static_cast<Decl *>(nd)))
      for (const auto &v : e->Variants) {
        for (const auto &tt : v->TupleTypes)
          if (!typeIsThreadSafe(tt->Resolved, wantSync, visiting))
            return false;
        for (const auto &fd : v->Fields)
          if (!typeIsThreadSafe(fd->Ty, wantSync, visiting))
            return false;
      }
    return true;
  }
  case TypeKind::Pointer: {
    // A raw or `weak` pointer carries no promise about its target.
    if (t->isRawPointer() || t->isWeakPointer() || !t->pointee())
      return false;
    // A shared `&T` may be sent or shared exactly when `T` is safe to reach
    // from several threads at once — i.e. `T: Sync` — for both `Send` and
    // `Sync`. A `&var T` follows the referent for whichever was asked:
    // `Send` when `T: Send`, `Sync` when `T: Sync`.
    if (t->isMutablePointer())
      return typeIsThreadSafe(t->pointee(), wantSync, visiting);
    return typeIsThreadSafe(t->pointee(), /*wantSync=*/true, visiting);
  }
  default:
    // Closures, `Any`, `dyn Mark`, `CString`: what they reach is not
    // something the compiler can see, so it will not vouch for it.
    return false;
  }
}

/// The reason `t` is not thread-safe, said in terms of what to do about it.
std::string Sema::whyNotThreadSafe(Type *t) {
  if (!t)
    return "the type is not one the compiler can vouch for";
  switch (t->kind()) {
  case TypeKind::Class:
    return "a class is a shared, mutable reference: passing one hands out a "
           "second way to reach the same object. Put it in a "
           "`thread::Mutex`, or pass what the thread needs by value";
  case TypeKind::Function:
    return "a closure carries the values it captured, and the other thread "
           "would reach into them. Pass a top-level `fn` and give it what it "
           "needs as an argument";
  case TypeKind::Pointer:
    return "a raw pointer carries no promise about what it points at";
  case TypeKind::Any:
  case TypeKind::DynMark:
    return "what is inside is not known here, so the compiler cannot say "
           "whether it is safe to share";
  case TypeKind::CString:
    return "a `CString` borrows bytes that something else owns; copy it into "
           "a `String` first";
  case TypeKind::Struct:
  case TypeKind::Enum:
  case TypeKind::Tuple:
  case TypeKind::Array:
  case TypeKind::Slice: {
    // Name the part that is at fault — the aggregate itself is fine.
    std::vector<Type *> parts;
    if (t->is(TypeKind::Tuple))
      parts = t->tupleElements();
    else if (t->is(TypeKind::Array) || t->is(TypeKind::Slice))
      parts.push_back(t->element());
    else if (NominalDecl *nd = t->nominal())
      for (const auto &f : nd->Fields)
        parts.push_back(f->Ty);
    for (Type *p : parts) {
      std::set<Type *> visiting;
      if (p && !typeIsThreadSafe(p, false, visiting))
        return fmt("it holds a '{}', and {}", p->toString(),
                   whyNotThreadSafe(p));
    }
    return "one of its parts is not safe to share";
  }
  default:
    return "the compiler cannot see what it holds";
  }
}

/// `@auto mark Plain { }` — a mark the compiler answers for.
///
/// An automatic mark says something *about* a type rather than asking
/// anything of it, so it carries no requirements: there would be nobody to
/// implement them. What it does carry is a rule — a type has the mark when
/// every part of it has the mark — and two ways to override that rule where
/// the structure cannot say: `bind Plain to Handle<T> {}` grants it, and
/// `@never(Plain)` refuses it.
void Sema::checkAutoMark(MarkDecl *mk) {
  const Attribute *a = static_cast<Decl *>(mk)->findAttr("auto");
  if (!a)
    return;
  mk->IsAuto = true;
  if (!a->Args.empty())
    Diags.error(a->Range, "`@auto` takes no arguments").code(243);
  if (!mk->Methods.empty()) {
    auto d = Diags.error(mk->Methods.front()->NameRange.isValid()
                             ? mk->Methods.front()->NameRange
                             : a->Range,
                         "an `@auto` mark has no requirements");
    d.note("the compiler decides which types have it, and nobody writes an "
           "implementation — so there is nowhere for '{}' to be written",
           mk->Methods.front()->Name);
    d.note("drop `@auto` to make this an ordinary mark that types bind");
    d.code(243);
  }
  if (!mk->AssociatedTypes.empty())
    Diags.error(a->Range, "an `@auto` mark has no associated types")
        .note("nothing implements it, so there is nobody to choose them")
        .code(243);
  if (!mk->Generics.empty())
    Diags.error(a->Range, "an `@auto` mark takes no generic parameters")
        .note("it is a question asked of one type at a time")
        .code(243);
}

/// The marks `@never(...)` on `d` refuses, resolved once.
const std::vector<MarkDecl *> &Sema::refusedMarks(NominalDecl *nd) {
  auto it = RefusedMarks.find(nd);
  if (it != RefusedMarks.end())
    return it->second;
  std::vector<MarkDecl *> marks;
  for (const Attribute &a : static_cast<Decl *>(nd)->Attrs) {
    if (a.Name != "never")
      continue;
    if (a.Args.size() != 1) {
      Diags.error(a.Range, "`@never` names one mark — `@never(mem::Clone)`")
          .code(244);
      continue;
    }
    std::vector<std::string> path;
    if (auto *ref = dyn_cast<DeclRefExpr>(a.Args[0].get()))
      path = ref->Path;
    MarkDecl *mark = nullptr;
    if (!path.empty()) {
      Scope *savedScope = CurScope;
      Module *savedModule = CurModule;
      auto mit = ModulesByName.find(static_cast<Decl *>(nd)->ModulePath);
      if (mit != ModulesByName.end())
        if (Scope *own = scopeForModule(mit->second)) {
          CurScope = own;
          CurModule = mit->second;
        }
      if (Symbol *sym = lookupPath(path, a.Range, /*quiet=*/true))
        mark = sym->D ? dyn_cast<MarkDecl>(sym->D) : nullptr;
      CurScope = savedScope;
      CurModule = savedModule;
    }
    if (!mark) {
      Diags.error(a.Range, "`@never` names a mark")
          .note("write `@never(mem::Clone)` above the type, with a mark that "
                "is in scope where the type is declared")
          .code(244);
      continue;
    }
    if (!mark->IsAuto) {
      auto d = Diags.error(a.Range, "'{}' is not an `@auto` mark",
                           static_cast<Decl *>(mark)->Name);
      d.note("only an automatic mark has to be refused; an ordinary one is "
             "had by binding it, and not had by not binding it");
      d.code(244);
      continue;
    }
    marks.push_back(mark);
  }
  return RefusedMarks[nd] = marks;
}

bool Sema::conditionalAutoBindHolds(Type *t, MarkDecl *mark,
                                    std::set<Type *> &seen) {
  NominalDecl *nd = t->nominal();
  if (!nd || !nd->GenericTemplate)
    return false;
  auto known = TemplateBinds.find(nd->GenericTemplate);
  if (known == TemplateBinds.end())
    return false;
  for (auto [m, b] : known->second) {
    if (b->ResolvedMark != mark || !b->ResolvedTarget)
      continue;
    const auto &placeholders = b->ResolvedTarget->typeArguments();
    const auto &args = t->typeArguments();
    if (placeholders.size() != args.size())
      continue;
    std::map<std::string, Type *> bindArgs;
    bool fits = true;
    for (size_t k = 0; k < placeholders.size(); ++k)
      if (!unifyGenericArg(placeholders[k], args[k], bindArgs))
        fits = false;
    if (!fits || bindArgs.size() != b->Generics.size())
      continue;

    Scope *savedScope = CurScope;
    Module *savedModule = CurModule;
    auto savedGenerics = ActiveGenericParams;
    if (Scope *own = scopeForModule(m)) {
      CurScope = own;
      CurModule = m;
    }
    bindGenerics(b->Generics, Types, ActiveGenericParams);
    auto holds = [&](const std::string &param,
                     const std::vector<TypeReprPtr> &bounds) {
      auto arg = bindArgs.find(param);
      if (arg == bindArgs.end())
        return false; // a subject this cannot read: not decided here
      for (const auto &boundRepr : bounds) {
        Diags.beginSpeculation();
        Type *bt = resolveTypeOrError(boundRepr.get(), Types.errorType());
        Diags.endSpeculation();
        if (!bt->is(TypeKind::Mark))
          return false;
        auto *mk = reinterpret_cast<MarkDecl *>(bt->nominal());
        bool ok = mk->IsAuto ? typeHasAutoMark(arg->second, mk, seen)
                             : typeConformsTo(arg->second, mk);
        if (!ok)
          return false;
      }
      return true;
    };
    bool ok = true;
    for (const auto &g : b->Generics)
      ok = ok && (g.Bounds.empty() || holds(g.Name, g.Bounds));
    for (const auto &w : b->WhereClauses) {
      auto *subject = dyn_cast<NamedTypeRepr>(w.Subject.get());
      ok = ok && subject && subject->Path.size() == 1 &&
           holds(subject->Path[0], w.Bounds);
    }
    ActiveGenericParams = savedGenerics;
    CurScope = savedScope;
    CurModule = savedModule;
    if (ok)
      return true;
  }
  return false;
}

/// Whether `t` has the automatic mark `mark`.
///
/// Every part has to have it: a struct's fields, an enum's payloads, a
/// tuple's elements, what an array or a slice holds. A type that destroys
/// something when it goes — anything with a `deinit` — does not get one
/// automatically: a destructor is an invariant the compiler cannot read, and
/// an automatic claim about such a type would be a guess. Nor does anything
/// the compiler cannot see into: a closure's captures, an `Any`, a `dyn`, a
/// raw or `weak` pointer, a `CString`. Each of those is a `bind` away when
/// the claim really does hold.
bool Sema::typeHasAutoMark(Type *t, MarkDecl *mark, std::set<Type *> &seen) {
  if (!t || t->isError() || !mark)
    return false;
  t = t->canonical();
  // A type may reach itself through a field; assume the best and let the
  // rest of the fields decide, as the layout walk does.
  if (!seen.insert(t).second)
    return true;

  // An explicit `bind` is the answer wherever there is one, whatever the
  // structure would have said.
  ensureStructuralBinds(t);
  auto conf = Conformances.find(t);
  if (conf != Conformances.end())
    for (MarkDecl *m : conf->second)
      if (m == mark)
        return true;

  switch (t->kind()) {
  case TypeKind::Void:
  case TypeKind::Never:
  case TypeKind::Bool:
  case TypeKind::Int:
  case TypeKind::Float:
  case TypeKind::Char:
    return true;
  case TypeKind::Tuple:
    for (Type *e : t->tupleElements())
      if (!typeHasAutoMark(e, mark, seen))
        return false;
    return true;
  case TypeKind::Array:
  case TypeKind::Slice:
    return typeHasAutoMark(t->element(), mark, seen);
  case TypeKind::Struct:
  case TypeKind::Enum:
  case TypeKind::Class: {
    NominalDecl *nd = t->nominal();
    if (!nd)
      return false;
    for (MarkDecl *refused : refusedMarks(nd))
      if (refused == mark)
        return false;
    // `bind<T> Clone to Vector<T> where T: Clone` claims the mark for the
    // instantiations that meet it. Asked here rather than only read off the
    // conformances the instantiation registered, because the question may
    // arrive *while* that instantiation is deciding: a `Token` holding a
    // `Vector<Token>` is `Clone` exactly when the vector is, and the answer
    // for a cycle, as everywhere in this walk, is yes.
    if (conditionalAutoBindHolds(t, mark, seen))
      return true;
    // Something with a `deinit` keeps a promise of its own; the compiler
    // does not know what the mark would mean for it.
    if (nd->Deinit)
      return false;
    for (const auto &f : nd->Fields)
      if (!typeHasAutoMark(f->Ty, mark, seen))
        return false;
    if (auto *e = dyn_cast<EnumDecl>(static_cast<Decl *>(nd)))
      for (const auto &v : e->Variants) {
        for (const auto &tt : v->TupleTypes)
          if (tt->Resolved && !typeHasAutoMark(tt->Resolved, mark, seen))
            return false;
        for (const auto &fd : v->Fields)
          if (!typeHasAutoMark(fd->Ty, mark, seen))
            return false;
      }
    if (auto *c = dyn_cast<ClassDecl>(static_cast<Decl *>(nd)))
      for (ClassDecl *sup = c->Super; sup; sup = sup->Super) {
        if (sup->DeclaredType && !typeHasAutoMark(sup->DeclaredType, mark, seen))
          return false;
      }
    return true;
  }
  case TypeKind::Pointer:
    // A borrow reaches something else; what it reaches is what matters, and
    // a raw or `weak` one promises nothing about it.
    if (t->isRawPointer() || t->isWeakPointer() || !t->pointee())
      return false;
    return typeHasAutoMark(t->pointee(), mark, seen);
  default:
    // `String`, `CString`, a closure, `Any`, `dyn Mark`: nothing to walk.
    // The standard library binds the ones that do hold.
    return false;
  }
}

/// Why `t` does not have the automatic mark `mark`, said in terms of the
/// part that does not have it.
std::string Sema::whyNotAutoMark(Type *t, MarkDecl *mark) {
  if (!t || !mark)
    return "";
  t = t->canonical();
  if (t->isNominal()) {
    NominalDecl *nd = t->nominal();
    for (MarkDecl *refused : refusedMarks(nd))
      if (refused == mark)
        return "'" + t->toString() + "' refuses it: `@never(" +
               static_cast<Decl *>(mark)->Name + ")` is written on the type";
    if (nd->Deinit)
      return "'" + t->toString() +
             "' runs a `deinit` when it goes, and what that promises is not "
             "something the compiler can read";
    std::set<Type *> seen;
    for (const auto &f : nd->Fields)
      if (f->Ty && !typeHasAutoMark(f->Ty, mark, seen)) {
        seen.clear();
        return "its field `" + f->Name + "` is a '" + f->Ty->toString() +
               "', which does not have it — " + whyNotAutoMark(f->Ty, mark);
      }
    if (auto *e = dyn_cast<EnumDecl>(static_cast<Decl *>(nd)))
      for (const auto &v : e->Variants) {
        for (const auto &tt : v->TupleTypes)
          if (tt->Resolved && !typeHasAutoMark(tt->Resolved, mark, seen)) {
            seen.clear();
            return "its variant `" + v->Name + "` holds a '" +
                   tt->Resolved->toString() + "', which does not have it";
          }
        for (const auto &fd : v->Fields)
          if (fd->Ty && !typeHasAutoMark(fd->Ty, mark, seen)) {
            seen.clear();
            return "its variant `" + v->Name + "` holds a '" +
                   fd->Ty->toString() + "', which does not have it";
          }
      }
    return "";
  }
  std::set<Type *> seen;
  switch (t->kind()) {
  case TypeKind::Tuple:
    for (Type *e : t->tupleElements())
      if (!typeHasAutoMark(e, mark, seen))
        return "'" + e->toString() + "' in it does not have it";
    return "";
  case TypeKind::Array:
  case TypeKind::Slice:
    return "what it holds — '" + t->element()->toString() +
           "' — does not have it";
  case TypeKind::Pointer:
    if (t->isRawPointer())
      return "a raw pointer promises nothing about what it points at";
    if (t->isWeakPointer())
      return "a `weak` reference may point at nothing at all";
    return "what it points at — '" + t->pointee()->toString() +
           "' — does not have it";
  case TypeKind::String:
  case TypeKind::CString:
  case TypeKind::Function:
  case TypeKind::DynMark:
  case TypeKind::Any:
    return "'" + t->toString() +
           "' is not something the compiler can look into, so it has no "
           "automatic mark of its own";
  default:
    return "";
  }
}

/// The part of `t` whose destruction a memberwise copy would duplicate, or
/// null when there is none.
///
/// A `deinit` is an obligation — close this, free that — and a copy made
/// field by field hands the same obligation to two values.
///
/// The walk stops at a class. A class is a *reference*: what a copy of one
/// means is the memory model's business — a second reference under counting,
/// a second object under single ownership — and neither is a memberwise copy
/// of the value in hand. A class that owns something says what copying it
/// means by writing `clone`, exactly as `std::io::File` does.
NominalDecl *Sema::cloneDuplicatesObligation(Type *t, std::set<Type *> &seen) {
  if (!t || t->isError())
    return nullptr;
  t = t->canonical();
  if (!seen.insert(t).second)
    return nullptr;
  switch (t->kind()) {
  case TypeKind::Tuple:
    for (Type *e : t->tupleElements())
      if (NominalDecl *bad = cloneDuplicatesObligation(e, seen))
        return bad;
    return nullptr;
  case TypeKind::Array:
    return cloneDuplicatesObligation(t->element(), seen);
  case TypeKind::Class:
    return nullptr;
  case TypeKind::Struct:
  case TypeKind::Enum: {
    NominalDecl *nd = t->nominal();
    if (!nd)
      return nullptr;
    // A type that writes its own `clone` decides what a copy of it means,
    // and everything below it is that method's business.
    if (userClone(t))
      return nullptr;
    if (nd->Deinit)
      return nd;
    for (const auto &f : nd->Fields)
      if (NominalDecl *bad = cloneDuplicatesObligation(f->Ty, seen))
        return bad;
    if (auto *e = dyn_cast<EnumDecl>(static_cast<Decl *>(nd)))
      for (const auto &v : e->Variants) {
        for (const auto &tt : v->TupleTypes)
          if (tt->Resolved)
            if (NominalDecl *bad = cloneDuplicatesObligation(tt->Resolved, seen))
              return bad;
        for (const auto &fd : v->Fields)
          if (NominalDecl *bad = cloneDuplicatesObligation(fd->Ty, seen))
            return bad;
      }
    return nullptr;
  }
  default:
    // A borrow, a raw pointer, a `String`, a closure: nothing whose
    // destruction a copy of this value takes on.
    return nullptr;
  }
}

/// `value.$clone()` — refused when the copy would hand one obligation to two
/// values. The type is the one place that knows what its destructor promises,
/// so the way out is to write `clone` there.
void Sema::checkClonable(Type *t, SourceRange at) {
  if (!t || t->isError() || userClone(t))
    return;
  std::set<Type *> seen;
  NominalDecl *bad = cloneDuplicatesObligation(t, seen);
  if (!bad)
    return;
  const std::string owner = static_cast<Decl *>(bad)->Name;
  auto d = Diags.error(at, "'{}' cannot be copied by the compiler", t->toString());
  if (t->isNominal() && t->nominal() == bad)
    d.note("it runs a `deinit`, so a copy of it would run that twice — "
           "closing one descriptor twice, freeing one block twice");
  else
    d.note("it holds a '{}', which runs a `deinit`: a copy would run that "
           "twice", owner);
  d.note("write `fn clone(&self) -> Self` on '{}' to say what a copy of it "
         "means — `std::collections::Vector` and `std::io::File` both do",
         owner);
  d.code(271);
  noteDeclaredAt(d, bad, "declared here", "this is what owns something");
}

bool Sema::typeConformsTo(Type *t, MarkDecl *mark) {
  if (!t || !mark)
    return false;
  // Nobody binds these; the type itself decides.
  if (mark == SendDecl || mark == SyncDecl) {
    std::set<Type *> visiting;
    return typeIsThreadSafe(t, mark == SyncDecl, visiting);
  }
  // A `some Mark` has that mark, and whatever the mark itself has; the type
  // behind it may have more, but nobody outside its function gets to know.
  if (t->isOpaque()) {
    std::function<bool(MarkDecl *)> reaches = [&](MarkDecl *m) -> bool {
      if (!m)
        return false;
      if (m == mark)
        return true;
      for (MarkDecl *sup : m->Supers)
        if (reaches(sup))
          return true;
      return false;
    };
    return reaches(t->opaqueMark());
  }
  if (t->is(TypeKind::DynMark))
    return t->mark() == mark;
  // An automatic mark is answered from the type rather than looked up: see
  // `typeHasAutoMark`.
  if (mark->IsAuto) {
    std::set<Type *> seen;
    return typeHasAutoMark(t, mark, seen);
  }
  // A `bind<T> Show to [T]` names a shape, so there is no instantiation to
  // have applied it. The first question asked about the shape is what applies
  // it; everything after finds it done.
  ensureStructuralBinds(t);
  auto it = Conformances.find(t);
  if (it != Conformances.end())
    for (MarkDecl *m : it->second)
      if (m == mark)
        return true;
  // Inherited conformances from a base class.
  if (t->isNominal())
    if (auto *c = dyn_cast<ClassDecl>(static_cast<Decl *>(t->nominal())))
      for (ClassDecl *s = c->Super; s; s = s->Super) {
        if (!s->DeclaredType)
          continue;
        auto sit = Conformances.find(s->DeclaredType);
        if (sit != Conformances.end())
          for (MarkDecl *m : sit->second)
            if (m == mark)
              return true;
      }
  return false;
}

FunctionDecl *Sema::lookupMethod(Type *receiver, const std::string &name,
                                 NominalDecl **ownerOut) {
  if (!receiver)
    return nullptr;
  // Look through borrows and optionals so `p.foo()` works on `&Point` too.
  while (receiver->is(TypeKind::Pointer))
    receiver = receiver->pointee();
  // A `some Mark` answers to the mark and nothing else: the method is the
  // one the type behind it supplies for that mark, found without ever
  // naming the type.
  if (receiver->isOpaque()) {
    Type *under = seeThroughOpaque(receiver, SourceRange());
    if (under->isError() || !receiver->opaqueMark())
      return nullptr;
    if (ownerOut)
      *ownerOut = nullptr;
    return lookupMarkMethod(under, receiver->opaqueMark(), name);
  }
  ensureStructuralBinds(receiver);
  if (receiver->is(TypeKind::DynMark)) {
    auto *mk = receiver->mark();
    if (!mk)
      return nullptr;
    for (auto &m : mk->Methods)
      if (m->Name == name) {
        if (ownerOut) *ownerOut = mk;
        return m.get();
      }
    return nullptr;
  }
  auto it = Methods.find(receiver);
  if (it != Methods.end()) {
    auto mit = it->second.find(name);
    if (mit != it->second.end()) {
      if (ownerOut)
        *ownerOut = receiver->isNominal() ? receiver->nominal() : nullptr;
      return mit->second;
    }
  }
  // Walk the superclass chain.
  if (receiver->isNominal())
    if (auto *c = dyn_cast<ClassDecl>(static_cast<Decl *>(receiver->nominal())))
      for (ClassDecl *s = c->Super; s; s = s->Super) {
        if (!s->DeclaredType)
          continue;
        auto sit = Methods.find(s->DeclaredType);
        if (sit == Methods.end())
          continue;
        auto mit = sit->second.find(name);
        if (mit != sit->second.end()) {
          if (ownerOut) *ownerOut = s;
          return mit->second;
        }
      }
  // `struct Derived : Base` — the parent's methods work on the child, because
  // the child *is* the parent with more on the end: the fields the method
  // reaches are at the same offsets.
  if (receiver->isNominal())
    for (NominalDecl *p = receiver->nominal()->InheritsDecl; p;
         p = p->InheritsDecl) {
      if (!p->DeclaredType)
        continue;
      auto sit = Methods.find(p->DeclaredType);
      if (sit == Methods.end())
        continue;
      auto mit = sit->second.find(name);
      if (mit != sit->second.end()) {
        if (ownerOut) *ownerOut = p;
        return mit->second;
      }
    }
  // A C++ class reaches its bases' members the way C++ does: `this` is the
  // same address, so the base's method is called on it unchanged.
  if (receiver->isNominal())
    for (NominalDecl *base : cxxBasesOf(receiver->nominal())) {
      if (!base->DeclaredType)
        continue;
      auto sit = Methods.find(base->DeclaredType);
      if (sit == Methods.end())
        continue;
      auto mit = sit->second.find(name);
      if (mit != sit->second.end()) {
        if (ownerOut) *ownerOut = base;
        return mit->second;
      }
    }
  return nullptr;
}

std::vector<FunctionDecl *> Sema::lookupOverloads(Type *receiver,
                                                 const std::string &name) {
  std::vector<FunctionDecl *> out;
  if (!receiver)
    return out;
  while (receiver->is(TypeKind::Pointer))
    receiver = receiver->pointee();
  if (receiver->isOpaque()) {
    Type *under = seeThroughOpaque(receiver, SourceRange());
    if (!under->isError() && receiver->opaqueMark())
      out = markImplSet(under, receiver->opaqueMark(), name);
    return out;
  }
  ensureStructuralBinds(receiver);
  auto it = MethodOverloads.find({receiver, name});
  if (it != MethodOverloads.end())
    out = it->second;
  // A class inherits its base's overloads, unless it supplies the name itself.
  if (out.size() < 2 && receiver->isNominal())
    if (auto *c = dyn_cast<ClassDecl>(static_cast<Decl *>(receiver->nominal())))
      for (ClassDecl *sup = c->Super; sup && out.size() < 2; sup = sup->Super) {
        if (!sup->DeclaredType)
          continue;
        auto sit = MethodOverloads.find({sup->DeclaredType, name});
        if (sit != MethodOverloads.end() && sit->second.size() > out.size())
          out = sit->second;
      }
  return out;
}

/// How well one candidate accepts the arguments. -1 means it cannot: an
/// argument with nowhere to go, a parameter with nothing to fill it and no
/// default, or a value that would not convert. Otherwise higher is better,
/// and an exact type is worth more than one reached by conversion.
///
/// `labels` runs alongside `args` when the call wrote any — a labelled
/// argument goes to the parameter of that name, and the positional ones fill
/// what is left in order, exactly as an ordinary call does.
int Sema::overloadScore(FunctionDecl *fn, const std::vector<Type *> &args,
                        const std::vector<std::string> *labels) {
  if (!fn)
    return -1;
  ensureTemplateSignature(fn);
  std::vector<const Param *> params;
  for (const Param &p : fn->Params)
    if (!p.IsSelf)
      params.push_back(&p);

  // Which argument fills each parameter. Labelled ones claim their own slot;
  // the rest go into what is left, in order.
  std::vector<int> filled(params.size(), -1);
  size_t next = 0;
  for (size_t a = 0; a < args.size(); ++a) {
    const std::string *label =
        labels && a < labels->size() && !(*labels)[a].empty() ? &(*labels)[a]
                                                              : nullptr;
    if (label) {
      size_t found = params.size();
      for (size_t i = 0; i < params.size(); ++i)
        if (params[i]->Name == *label)
          found = i;
      if (found == params.size() || filled[found] >= 0)
        return -1;
      filled[found] = static_cast<int>(a);
      continue;
    }
    while (next < params.size() && filled[next] >= 0)
      ++next;
    if (next >= params.size()) {
      // A variadic tail takes whatever is left over; anything else does not.
      if (fn->IsVariadic)
        continue;
      return -1;
    }
    filled[next] = static_cast<int>(a);
    ++next;
  }
  // A parameter nothing filled has to have brought its own value.
  for (size_t i = 0; i < params.size(); ++i)
    if (filled[i] < 0 && !params[i]->DefaultValue)
      return -1;

  std::vector<Type *> formals;
  std::vector<Type *> given;
  for (size_t i = 0; i < params.size(); ++i) {
    if (filled[i] < 0)
      continue;
    formals.push_back(params[i]->Ty);
    given.push_back(args[static_cast<size_t>(filled[i])]);
  }

  int score = 0;
  for (size_t i = 0; i < formals.size(); ++i) {
    Type *want = formals[i];
    Type *got = given[i];
    // Nothing known about this argument — every candidate is still in play,
    // and none of them earns anything for it.
    if (!want || !got || want->isError() || got->isError()) {
      score += 1;
      continue;
    }
    // A borrow in the signature is written `&T` but handed a `T`, which is
    // how every `&self`-style parameter is called.
    Type *bare = want;
    while (bare->is(TypeKind::Pointer) && !bare->isRawPointer())
      bare = bare->pointee();
    Type *gotBare = got;
    while (gotBare->is(TypeKind::Pointer) && !gotBare->isRawPointer())
      gotBare = gotBare->pointee();
    if (sameType(want, got) || sameType(bare, gotBare))
      score += 4;
    else if (isImplicitlyConvertible(got, want) ||
             isImplicitlyConvertible(gotBare, bare))
      score += 1;
    else
      return -1;
  }
  return score;
}

FunctionDecl *Sema::pickOverload(const std::vector<FunctionDecl *> &candidates,
                                 const std::vector<Type *> &args,
                                 bool &ambiguous,
                                 const std::vector<std::string> *labels) {
  ambiguous = false;
  FunctionDecl *best = nullptr;
  int bestScore = -1;
  bool tied = false;
  for (FunctionDecl *fn : candidates) {
    int score = overloadScore(fn, args, labels);
    if (score < 0)
      continue;
    if (score > bestScore) {
      best = fn;
      bestScore = score;
      tied = false;
    } else if (score == bestScore && best && !sameParameterList(best, fn)) {
      tied = true;
    }
  }
  if (tied) {
    ambiguous = true;
    return nullptr;
  }
  return best;
}

void Sema::reportNoOverload(const std::vector<FunctionDecl *> &candidates,
                            const std::vector<Type *> &args,
                            const std::string &name, SourceRange at,
                            bool ambiguous) {
  std::string given;
  for (size_t i = 0; i < args.size(); ++i) {
    if (i)
      given += ", ";
    given += args[i] ? args[i]->toString() : "?";
  }
  auto d = ambiguous
               ? Diags.error(at, "which '{}' is meant by ({}) is ambiguous",
                             name, given)
               : Diags.error(at, "no version of '{}' takes ({})", name, given);
  d.code(ambiguous ? 226 : 225);
  if (ambiguous)
    d.note("more than one binding accepts these arguments equally well");
  for (FunctionDecl *fn : candidates) {
    ensureTemplateSignature(fn);
    std::string sig;
    for (const Param &p : fn->Params) {
      if (p.IsSelf)
        continue;
      if (!sig.empty())
        sig += ", ";
      sig += p.Ty ? p.Ty->toString() : "?";
    }
    noteDeclaredAt(d, fn, fmt("takes ({})", sig), "one of the bindings");
  }
}

/// The type of an overload's right-hand operand, looking through the borrow
/// most of them take.
static Type *rhsTypeOf(const FunctionDecl *fn) {
  if (!fn)
    return nullptr;
  for (const Param &p : fn->Params) {
    if (p.IsSelf)
      continue;
    Type *t = p.Ty;
    while (t && t->is(TypeKind::Pointer) && !t->isRawPointer())
      t = t->pointee();
    return t;
  }
  return nullptr;
}

FunctionDecl *Sema::lookupOperator(Type *receiver, const std::string &op) {
  if (!receiver)
    return nullptr;
  while (receiver->is(TypeKind::Pointer))
    receiver = receiver->pointee();
  ensureStructuralBinds(receiver);
  auto it = Operators.find({receiver, op});
  if (it != Operators.end() && !it->second.empty())
    return it->second.front();
  // Operators may also arrive through a mark bound to the type.
  return lookupMethod(receiver, op);
}

std::vector<FunctionDecl *> Sema::operatorOverloads(Type *receiver,
                                                   const std::string &op) {
  if (receiver && receiver->isOpaque())
    return {};
  std::vector<FunctionDecl *> out;
  if (!receiver)
    return out;
  while (receiver->is(TypeKind::Pointer))
    receiver = receiver->pointee();
  ensureStructuralBinds(receiver);
  auto it = Operators.find({receiver, op});
  if (it != Operators.end())
    out = it->second;
  if (out.empty())
    out = lookupOverloads(receiver, op);
  if (out.empty())
    if (FunctionDecl *only = lookupMethod(receiver, op))
      out.push_back(only);
  return out;
}

FunctionDecl *Sema::lookupOperator(Type *receiver, const std::string &op,
                                   Type *rhs) {
  if (!receiver)
    return nullptr;
  while (receiver->is(TypeKind::Pointer))
    receiver = receiver->pointee();
  if (rhs)
    while (rhs->is(TypeKind::Pointer) && !rhs->isRawPointer())
      rhs = rhs->pointee();
  ensureStructuralBinds(receiver);

  auto it = Operators.find({receiver, op});
  if (it != Operators.end() && !it->second.empty()) {
    if (!rhs)
      return it->second.front();
    // An exact match on the right-hand type wins; otherwise the first
    // overload that would accept the value.
    for (FunctionDecl *fn : it->second) {
      ensureTemplateSignature(fn);
      if (rhsTypeOf(fn) == rhs)
        return fn;
    }
    for (FunctionDecl *fn : it->second) {
      Type *want = rhsTypeOf(fn);
      if (want && isImplicitlyConvertible(rhs, want))
        return fn;
    }
    return nullptr;
  }
  return lookupMethod(receiver, op);
}

bool Sema::builtinOperatorApplies(const std::string &op, Type *lhs, Type *rhs) {
  if (!lhs || !rhs)
    return false;
  // Comparison is defined for every pair the language can compare directly.
  if (op == "eq" || op == "cmp")
    return (lhs->isNumeric() && rhs->isNumeric()) ||
           (lhs->is(TypeKind::String) && rhs->is(TypeKind::String)) ||
           (lhs->is(TypeKind::Char) && rhs->is(TypeKind::Char)) ||
           (lhs->isBool() && rhs->isBool()) ||
           (lhs->is(TypeKind::CString) && rhs->is(TypeKind::CString));
  if (op == "add" && lhs->is(TypeKind::String) && rhs->is(TypeKind::String))
    return true;
  if (op == "add" || op == "sub" || op == "mul" || op == "div" || op == "rem")
    return lhs->isNumeric() && rhs->isNumeric();
  if (op == "bitand" || op == "bitor" || op == "bitxor")
    return (lhs->isInt() && rhs->isInt()) || (lhs->isBool() && rhs->isBool());
  if (op == "shl" || op == "shr")
    return lhs->isInt() && rhs->isInt();
  // Unary: `rhs` is the operand again, which is how a bound asks about one.
  if (op == "neg")
    return lhs->isNumeric();
  if (op == "not")
    return lhs->isBool();
  if (op == "bitnot")
    return lhs->isInt() || lhs->isBool();
  // Indexing is builtin for the things that already hold elements in a row.
  if (op == "index")
    return lhs->is(TypeKind::Array) || lhs->is(TypeKind::Slice) ||
           lhs->is(TypeKind::Pointer) || lhs->is(TypeKind::String);
  if (op == "deref")
    return lhs->is(TypeKind::Pointer);
  return false;
}

/// True when `$clone()` can build a second copy of a value of `t`: nothing
/// borrowed, no closure, no mark object or `Any`, and every part likewise.
FunctionDecl *Sema::userClone(Type *t) {
  if (!t || !t->isNominal())
    return nullptr;
  NominalDecl *nd = t->nominal();
  if (!nd)
    return nullptr;
  // Whatever `resolveDeinitialisers` settled on: the method a call to
  // `clone()` would reach, from wherever it was written.
  if (nd->CloneFn)
    return nd->CloneFn;
  auto isClone = [](FunctionDecl *m) {
    if (!m || m->Name != "clone")
      return false;
    int nonSelf = 0;
    bool self = false;
    for (const Param &p : m->Params) {
      if (p.IsSelf) self = true; else ++nonSelf;
    }
    return self && nonSelf == 0;
  };
  for (auto &m : nd->Methods)
    if (isClone(m.get()))
      return m.get();
  return nullptr;
}

bool Sema::typeIsClonable(Type *t) {
  std::set<Type *> seen;
  std::function<bool(Type *)> go = [&](Type *x) -> bool {
    if (!x || x->isError())
      return false;
    if (!seen.insert(x).second)
      return true;
    switch (x->kind()) {
    case TypeKind::Bool: case TypeKind::Int: case TypeKind::Float:
    case TypeKind::Char: case TypeKind::String: case TypeKind::Void:
    case TypeKind::CString:
      return true;
    case TypeKind::Array:
      return go(x->element());
    case TypeKind::Tuple:
      for (Type *e : x->tupleElements())
        if (!go(e))
          return false;
      return true;
    case TypeKind::Struct:
    case TypeKind::Enum:
    case TypeKind::Class: {
      NominalDecl *nd = x->nominal();
      if (!nd || x->isOpaque())
        return false;
      // A type that writes its own `clone(&self) -> Self` is clonable
      // whatever it holds — a container over raw memory is the case.
      if (lookupMethod(x, "clone"))
        return true;
      std::vector<NominalDecl *> chain{nd};
      if (auto *c = dyn_cast<ClassDecl>(static_cast<Decl *>(nd)))
        for (ClassDecl *sc = c->Super; sc; sc = sc->Super)
          chain.push_back(sc);
      for (NominalDecl *n : chain)
        for (const auto &f : n->Fields)
          if (f->IsWeak || !go(f->Ty))
            return false;
      if (auto *e = dyn_cast<EnumDecl>(static_cast<Decl *>(nd)))
        for (const auto &v : e->Variants) {
          for (const auto &tt : v->TupleTypes)
            if (!go(tt->Resolved))
              return false;
          for (const auto &f : v->Fields)
            if (!go(f->Ty))
              return false;
        }
      return true;
    }
    default:
      return false; // borrows, raw pointers, closures, `dyn`, `Any`
    }
  };
  return go(t);
}

BuiltinMethod Sema::lookupBuiltinMethod(Type *receiver, const std::string &name,
                                        std::vector<Type *> &params,
                                        Type *&result) {
  params.clear();
  if (!receiver)
    return BuiltinMethod::None;
  // `$clone()` on an opaque `some Mark` is a share: there is no concrete type
  // to deep-copy, so it behaves like cloning a `dyn` — a retain under counting,
  // a second owner under single ownership. Every other builtin member is
  // hidden behind the opaque veil, so this is settled before that guard.
  if (name == "clone" && receiver->isOpaque()) {
    result = receiver;
    return BuiltinMethod::Clone;
  }
  if (receiver->isOpaque())
    return BuiltinMethod::None;

  if (receiver->is(TypeKind::String)) {
    if (name == "length" || name == "len") { result = Types.i64(); return BuiltinMethod::StringLength; }
    if (name == "charCount") { result = Types.i64(); return BuiltinMethod::StringCharCount; }
    if (name == "isEmpty") { result = Types.boolType(); return BuiltinMethod::StringIsEmpty; }
    if (name == "at") { params = {Types.i64()}; result = Types.charType(); return BuiltinMethod::StringAt; }
    if (name == "byteAt") { params = {Types.i64()}; result = Types.u8(); return BuiltinMethod::StringByteAt; }
    if (name == "substring") { params = {Types.i64(), Types.i64()}; result = Types.stringType(); return BuiltinMethod::StringSubstring; }
    if (name == "find") { params = {Types.pointerTo(Types.stringType(), false, false)}; result = Types.i64(); return BuiltinMethod::StringFind; }
    if (name == "repeat") { params = {Types.i64()}; result = Types.stringType(); return BuiltinMethod::StringRepeat; }
    if (name == "toInt") {
      result = optionOf(Types.i64(), SourceRange());
      return BuiltinMethod::StringToInt;
    }
    if (name == "toFloat") {
      result = optionOf(Types.f64(), SourceRange());
      return BuiltinMethod::StringToFloat;
    }
    if (name == "cstr") { result = Types.cstringType(); return BuiltinMethod::StringCStr; }
    if (name == "hash") { result = Types.u64(); return BuiltinMethod::StringHash; }
  }
  if (receiver->is(TypeKind::Array) || receiver->is(TypeKind::Slice)) {
    if (name == "length" || name == "len") { result = Types.i64(); return BuiltinMethod::SequenceLength; }
    if (name == "isEmpty") { result = Types.boolType(); return BuiltinMethod::SequenceIsEmpty; }
  }
  // Arithmetic that names its overflow behaviour. `+` follows the build —
  // a debug build traps, an optimised one wraps — and these are for the
  // places where the program means one of them in particular.
  if (receiver->isInt()) {
    struct { const char *Name; BuiltinMethod Which; bool Checked; } table[] = {
        {"wrappingAdd", BuiltinMethod::IntWrappingAdd, false},
        {"wrappingSub", BuiltinMethod::IntWrappingSub, false},
        {"wrappingMul", BuiltinMethod::IntWrappingMul, false},
        {"saturatingAdd", BuiltinMethod::IntSaturatingAdd, false},
        {"saturatingSub", BuiltinMethod::IntSaturatingSub, false},
        {"saturatingMul", BuiltinMethod::IntSaturatingMul, false},
        {"checkedAdd", BuiltinMethod::IntCheckedAdd, true},
        {"checkedSub", BuiltinMethod::IntCheckedSub, true},
        {"checkedMul", BuiltinMethod::IntCheckedMul, true},
    };
    for (const auto &entry : table) {
      if (name != entry.Name)
        continue;
      params = {receiver};
      result = entry.Checked ? optionOf(receiver, SourceRange()) : receiver;
      return entry.Which;
    }
  }
  // `$clone()` asks for a second, independent value. It applies to anything
  // with a value to copy — a number, a String, a tuple, an array, or a
  // nominal type (a struct, enum or class); a class that owns raw storage
  // writes its own `clone`, which codegen calls. It does not apply to a
  // borrow, a raw pointer, a closure, a `dyn` or an `Any`, none of which
  // owns a value to copy.
  // Everything that owns a value can be copied. A number, a String, a tuple,
  // an array, a struct/enum/class are cloned deeply; a closure, a `dyn Mark`
  // or an `Any` are shared (they have no deep copy), which under counting is
  // a retain and under single ownership is the one thing that cannot be done
  // — a `$clone()` of one of those is an error the borrow checker will
  // report where it is reached. What cannot be cloned at all is a bare
  // reference, a raw pointer, a `CString` or an unresolved type parameter.
  // `()` clones to `()`: it has nothing in it, and a generic `T` that turns
  // out to be `()` — a `Future<()>`, an `Option<()>` — asks all the same.
  // A shared borrow is copied as it is: the copy looks at the same value.
  // (One is reached only where the receiver is a borrow *as a value* —
  // `Option<&T>` cloning its payload; an ordinary `r.$clone()` on a borrow
  // looks through it first.)
  if (name == "clone" && receiver->is(TypeKind::Pointer) &&
      !receiver->isRawPointer() && !receiver->isWeakPointer() &&
      !receiver->isMutablePointer()) {
    result = receiver;
    return BuiltinMethod::Clone;
  }
  if (name == "clone" && receiver &&
      !(receiver->is(TypeKind::Pointer) || receiver->is(TypeKind::CString) ||
        receiver->isGeneric() || receiver->is(TypeKind::Mark) ||
        receiver->is(TypeKind::Never))) {
    result = receiver;
    return BuiltinMethod::Clone;
  }
  // Every primitive can render itself, which is what makes string building
  // with `+` practical without overloaded functions.
  if (name == "str" &&
      (receiver->isNumeric() || receiver->isBool() ||
       receiver->is(TypeKind::Char) || receiver->is(TypeKind::CString) ||
       receiver->is(TypeKind::String) || receiver->is(TypeKind::Pointer))) {
    result = Types.stringType();
    return BuiltinMethod::ToString;
  }
  return BuiltinMethod::None;
}

//===----------------------------------------------------------------------===//
// Generic instantiation
//===----------------------------------------------------------------------===//

void Sema::ensureVariantPayloadTypes(EnumDecl *e) {
  if (!e || e->Generics.empty())
    return;
  bool missing = false;
  for (auto &v : e->Variants) {
    for (auto &tt : v->TupleTypes)
      if (!tt->Resolved)
        missing = true;
    for (auto &f : v->Fields)
      if (!f->Ty)
        missing = true;
  }
  if (!missing)
    return;

  // Resolve in the template's own module, with its parameters bound, so the
  // payload types come out as `T` rather than as errors.
  Scope *savedScope = CurScope;
  Module *savedModule = CurModule;
  auto savedGenerics = ActiveGenericParams;
  auto mit = ModulesByName.find(e->ModulePath);
  if (mit != ModulesByName.end()) {
    CurModule = mit->second;
    CurScope = scopeForModule(mit->second);
  }
  bindGenerics(e->Generics, Types, ActiveGenericParams);
  for (auto &v : e->Variants) {
    for (auto &tt : v->TupleTypes)
      resolveTypeOrError(tt.get(), Types.errorType());
    for (auto &f : v->Fields)
      if (!f->Ty)
        f->Ty = resolveTypeOrError(f->TypeAnnotation.get(), Types.errorType());
  }
  ActiveGenericParams = savedGenerics;
  CurScope = savedScope;
  CurModule = savedModule;
}

EnumDecl *Sema::instantiateVariantOwner(
    EnumDecl *tmpl, unsigned variantIndex,
    const std::vector<TypeReprPtr> &explicitArgs,
    const std::vector<Type *> &argTypes, Type *expected, SourceRange range) {
  if (tmpl->Generics.empty())
    return tmpl;

  std::vector<Type *> targs;
  if (!explicitArgs.empty()) {
    for (const auto &a : explicitArgs)
      targs.push_back(resolveTypeOrError(a.get(), Types.errorType()));
  } else {
    // Infer from the constructor arguments first, then fall back to the type
    // the surrounding context is asking for.
    std::map<std::string, Type *> bindings;
    ensureVariantPayloadTypes(tmpl);
    const auto &variant = tmpl->Variants[variantIndex];
    for (size_t i = 0; i < argTypes.size() && i < variant->TupleTypes.size(); ++i)
      if (variant->TupleTypes[i]->Resolved)
        Types.unify(variant->TupleTypes[i]->Resolved, argTypes[i], bindings);

    Type *contextual = expected;
    if (contextual && contextual->is(TypeKind::Enum) && contextual->nominal()) {
      NominalDecl *cn = contextual->nominal();
      bool sameFamily = cn == static_cast<NominalDecl *>(tmpl) ||
                        cn->GenericTemplate == static_cast<NominalDecl *>(tmpl);
      if (sameFamily)
        for (size_t i = 0; i < tmpl->Generics.size() &&
                           i < contextual->typeArguments().size();
             ++i)
          bindings.emplace(tmpl->Generics[i].Name,
                           contextual->typeArguments()[i]);
    }

    for (const auto &g : tmpl->Generics) {
      auto it = bindings.find(g.Name);
      if (it == bindings.end()) {
        auto d = Diags.error(range,
                             "cannot infer type parameter '{}' of '{}'", g.Name,
                             tmpl->Name);
        d.note(fmt("write it out, e.g. `{}::<...>::{}`", tmpl->Name,
                   tmpl->Variants[variantIndex]->Name)
                   .c_str())
            .code(370);
        d.related(g.Range, fmt("'{}' is declared here", g.Name),
                  "nothing at the use site determines this parameter");
        return nullptr;
      }
      targs.push_back(it->second);
    }
  }

  NominalDecl *inst = instantiateNominal(static_cast<NominalDecl *>(tmpl), targs,
                                         range);
  return inst ? dyn_cast<EnumDecl>(static_cast<Decl *>(inst)) : nullptr;
}

void Sema::checkDeferredMethod(FunctionDecl *fn) {
  if (!fn || !fn->Body || !DeferredMethods.erase(fn))
    return;
  if (!methodWhereHolds(fn, SourceRange(), /*report=*/false))
    return;
  auto *inst = fn->Parent ? dyn_cast<NominalDecl>(fn->Parent) : nullptr;
  if (!inst)
    return;
  // Before the body pass the program is still half-declared, so hold it.
  if (!InBodyPass) {
    PendingMethods.push_back(fn);
    return;
  }
  auto *cls = dyn_cast<ClassDecl>(static_cast<Decl *>(inst));
  Scope *savedScope = CurScope;
  Module *savedModule = CurModule;
  auto savedGenerics = ActiveGenericParams;
  Type *savedSelf = ActiveSelfType;
  auto mit = ModulesByName.find(fn->ModulePath);
  if (mit != ModulesByName.end()) {
    CurModule = mit->second;
    CurScope = scopeForModule(mit->second);
  }
  ActiveGenericParams.clear();
  bindOwnerGenerics(fn, ActiveGenericParams);
  ActiveSelfType = inst->DeclaredType;
  pushScope(ScopeKind::TypeBody);
  checkFunction(fn, inst->DeclaredType, cls);
  Result.Functions.push_back(fn);
  popScope();
  ActiveGenericParams = savedGenerics;
  ActiveSelfType = savedSelf;
  CurScope = savedScope;
  CurModule = savedModule;
}

void Sema::drainPendingMethods() {
  for (size_t i = 0; i < PendingMethods.size(); ++i) {
    FunctionDecl *fn = PendingMethods[i];
    // `checkDeferredMethod` already took it out of the set; put it back so
    // that call does the work rather than returning straight away.
    DeferredMethods.insert(fn);
    checkDeferredMethod(fn);
  }
  PendingMethods.clear();
}

void Sema::checkInstantiatedBodies(NominalDecl *inst) {
  NominalDecl *tmpl = inst->GenericTemplate;
  if (!tmpl)
    return;
  auto *cls = dyn_cast<ClassDecl>(static_cast<Decl *>(inst));

  // The same context the instantiation was built in: the template's module,
  // its parameters bound to this instantiation's arguments, and `Self`.
  Scope *savedScope = CurScope;
  Module *savedModule = CurModule;
  auto savedGenerics = ActiveGenericParams;
  Type *savedSelf = ActiveSelfType;
  auto tmplModule = ModulesByName.find(static_cast<Decl *>(tmpl)->ModulePath);
  if (tmplModule != ModulesByName.end()) {
    CurModule = tmplModule->second;
    CurScope = scopeForModule(tmplModule->second);
  }
  ActiveGenericParams.clear();
  bindTemplateGenerics(tmpl, inst->TypeArguments, ActiveGenericParams);
  ActiveSelfType = inst->DeclaredType;

  pushScope(ScopeKind::TypeBody);
  {
    Symbol selfName;
    selfName.Kind = SymbolKind::TypeName;
    selfName.Name = static_cast<Decl *>(tmpl)->Name;
    selfName.D = static_cast<Decl *>(inst);
    selfName.IsPublic = static_cast<Decl *>(tmpl)->IsPublic;
    CurScope->overwrite(selfName);
  }
  // One entry per method, not per name: an `@alias` puts the same declaration
  // in the table twice, and checking a body twice leaves it resolved against
  // the wrong locals.
  std::set<FunctionDecl *> checked;
  std::vector<FunctionDecl *> supplied;
  for (auto &entry : Methods[inst->DeclaredType])
    supplied.push_back(entry.second);
  // A name a binding overloaded holds only one of its versions in the table
  // above; the rest still have bodies to check and code to emit.
  appendOverloadsFor(inst->DeclaredType, supplied);
  for (FunctionDecl *f : supplied) {
    if (f->Parent != static_cast<Decl *>(inst) || !f->Body)
      continue;
    if (!f->Generics.empty())
      continue; // checked once per set of arguments, at the call site
    if (DeferredMethods.count(f))
      continue; // checked with its signature, at the call site
    if (!checked.insert(f).second)
      continue;
    // `fn at(&self, i: i64) -> T? where T: Clone` is not there for a `T`
    // that is not — its body would only report what the bound already says.
    // A call to it is refused where it is made.
    if (!methodWhereHolds(f, SourceRange(), /*report=*/false))
      continue;
    checkFunction(f, inst->DeclaredType, cls);
    Result.Functions.push_back(f);
  }
  popScope();

  ActiveGenericParams = savedGenerics;
  ActiveSelfType = savedSelf;
  CurScope = savedScope;
  CurModule = savedModule;
}

/// Signatures for everything a binding supplied to `inst`, not only whichever
/// version holds the name: an overload the call site has still to choose
/// between needs its signature and its symbol as much as the one in the table
/// does. Run as the instantiation is built, and again for a conditional bind
/// that only applied once every bind was registered.
void Sema::resolveSuppliedSignatures(NominalDecl *inst) {
  NominalDecl *tmpl = inst->GenericTemplate;
  if (!tmpl)
    return;
  const std::vector<Type *> &args = inst->TypeArguments;
  Scope *savedScope = CurScope;
  Module *savedModule = CurModule;
  auto savedGenerics = ActiveGenericParams;
  Type *savedSelf = ActiveSelfType;
  auto mit = ModulesByName.find(static_cast<Decl *>(tmpl)->ModulePath);
  if (mit != ModulesByName.end()) {
    CurModule = mit->second;
    CurScope = scopeForModule(mit->second);
  }
  ActiveGenericParams.clear();
  bindTemplateGenerics(tmpl, args, ActiveGenericParams);
  ActiveSelfType = inst->DeclaredType;

  std::vector<FunctionDecl *> supplied;
  for (auto &fn : Methods[inst->DeclaredType])
    supplied.push_back(fn.second);
  appendOverloadsFor(inst->DeclaredType, supplied);
  for (FunctionDecl *f : supplied) {
    if (f->Parent != static_cast<Decl *>(inst) || !f->Body || f->Ty)
      continue;
    if (!f->Generics.empty())
      continue; // its own parameters are only known at the call site
    if (signatureWrapsSelf(f)) {
      f->MangledName = mangleFunction(f, args);
      DeferredMethods.insert(f);
      continue;
    }
    resolveMethodSignature(f, inst->DeclaredType, args);
  }

  ActiveGenericParams = savedGenerics;
  ActiveSelfType = savedSelf;
  CurScope = savedScope;
  CurModule = savedModule;
}

void Sema::drainPendingInstantiations() {
  // Checking one body may instantiate another type, which lands at the back of
  // the queue; keep going until nothing new arrives.
  //
  // A body checked here may instantiate a generic function, which drains
  // again once its signature is settled. That inner drain must not start the
  // queue over: it would check every body before it a second time — a body
  // checked twice is resolved against the wrong locals — and then clear the
  // queue out from under this loop. What it would have drained is at the
  // back of the queue already, and this loop reaches it.
  if (DrainingInstantiations)
    return;
  DrainingInstantiations = true;
  for (size_t i = 0; i < PendingInstantiations.size(); ++i)
    checkInstantiatedBodies(PendingInstantiations[i]);
  PendingInstantiations.clear();
  DrainingInstantiations = false;
}

//===----------------------------------------------------------------------===//
// Signatures that wrap `Self`
//
// `fn boxed(self) -> Boxed<Self>` on a mark is a method every type carrying
// that mark gets — including `Boxed<T>` itself, once it carries the mark too.
// Resolving its result for `Boxed<T>` asks for `Boxed<Boxed<T>>`, which asks
// for `Boxed<Boxed<Boxed<T>>>`, and so on: the chain has no end because each
// link is a type nothing has asked for yet.
//
// So these signatures are not resolved when the type is built. They are
// resolved at the call, exactly as a generic method's is, and a chain is then
// only as deep as the one somebody wrote.
//
// Only a *bare* `Self` as an argument does this. `Boxed<Self::Item>` names a
// different type — the item, not the wrapper — so it terminates on its own,
// and `Self::Item?` (which is `Option<Self::Item>`) must stay eager: `for`
// reads what `next` returns without ever calling it.
//===----------------------------------------------------------------------===//

namespace {

bool isBareSelf(const TypeRepr *t) {
  if (!t)
    return false;
  if (t->Kind == NodeKind::SelfType)
    return true;
  const auto *n = dyn_cast<NamedTypeRepr>(t);
  return n && n->Path.size() == 1 && n->Path[0] == "Self" &&
         n->GenericArgs.empty();
}

/// True for `Self::Item`, `Self::Iter::Item` — an associated type read off
/// `Self` rather than `Self` itself.
bool isSelfQualified(const TypeRepr *t) {
  const auto *n = dyn_cast<NamedTypeRepr>(t);
  return n && n->Path.size() > 1 && n->Path[0] == "Self";
}

/// True when `t` uses a bare `Self` as a generic argument, at any depth.
bool wrapsSelf(const TypeRepr *t) {
  if (!t)
    return false;
  switch (t->Kind) {
  case NodeKind::NamedType: {
    const auto *n = cast<NamedTypeRepr>(t);
    // `Vector<Self::Item>` has to wait as much as `Map<Self, B>` does. Both
    // name a type the binding does not fix, and resolving one eagerly for
    // every binding builds a tower: the vector `collect` returns is itself a
    // sequence, whose own `collect` asks for another vector, forever.
    for (const auto &a : n->GenericArgs)
      if (isBareSelf(a.get()) || isSelfQualified(a.get()) || wrapsSelf(a.get()))
        return true;
    return false;
  }
  case NodeKind::PointerType:
    return wrapsSelf(cast<PointerTypeRepr>(t)->Pointee.get());
  case NodeKind::ArrayType:
    return wrapsSelf(cast<ArrayTypeRepr>(t)->Element.get());
  case NodeKind::SliceType:
    return wrapsSelf(cast<SliceTypeRepr>(t)->Element.get());
  case NodeKind::OptionalType: {
    // `Self?` is `Option<Self>`, and `Option` is a type like any other.
    const auto *o = cast<OptionalTypeRepr>(t);
    return isBareSelf(o->Element.get()) || wrapsSelf(o->Element.get());
  }
  case NodeKind::UniqType:
    return wrapsSelf(cast<UniqTypeRepr>(t)->Element.get());
  case NodeKind::TupleType: {
    for (const auto &e : cast<TupleTypeRepr>(t)->Elements)
      if (wrapsSelf(e.get()))
        return true;
    return false;
  }
  case NodeKind::FunctionTypeRepr: {
    const auto *f = cast<FunctionTypeReprNode>(t);
    for (const auto &p : f->Params)
      if (wrapsSelf(p.get()))
        return true;
    return wrapsSelf(f->ReturnType.get());
  }
  default:
    return false;
  }
}

} // namespace

/// True when `t` names `owner`'s own template with an argument built *out
/// of* one of its parameters rather than being one: `Option<&T>` on
/// `Option<T>`, `Vector<(T, T)>` on `Vector<T>`. Resolving such a signature
/// for every instantiation builds a tower — `Option<&T>` has a `peek` of its
/// own, which asks for `Option<&&T>` — so it waits for a call, like a
/// signature that wraps `Self`.
static bool growsOwner(const TypeRepr *t, const std::string &owner,
                       const std::set<std::string> &params) {
  if (!t)
    return false;
  std::function<bool(const TypeRepr *)> mentions = [&](const TypeRepr *r) {
    if (!r)
      return false;
    switch (r->Kind) {
    case NodeKind::NamedType: {
      const auto *n = cast<NamedTypeRepr>(r);
      if (n->Path.size() == 1 && params.count(n->Path[0]))
        return true;
      for (const auto &a : n->GenericArgs)
        if (mentions(a.get()))
          return true;
      return false;
    }
    case NodeKind::PointerType:
      return mentions(cast<PointerTypeRepr>(r)->Pointee.get());
    case NodeKind::ArrayType:
      return mentions(cast<ArrayTypeRepr>(r)->Element.get());
    case NodeKind::SliceType:
      return mentions(cast<SliceTypeRepr>(r)->Element.get());
    case NodeKind::OptionalType:
      return mentions(cast<OptionalTypeRepr>(r)->Element.get());
    case NodeKind::TupleType:
      for (const auto &e : cast<TupleTypeRepr>(r)->Elements)
        if (mentions(e.get()))
          return true;
      return false;
    default:
      return false;
    }
  };
  auto grownArg = [&](const TypeRepr *a) {
    const auto *n = dyn_cast<NamedTypeRepr>(a);
    bool bare = n && n->Path.size() == 1 && n->GenericArgs.empty() &&
                params.count(n->Path[0]);
    return !bare && mentions(a);
  };
  switch (t->Kind) {
  case NodeKind::NamedType: {
    const auto *n = cast<NamedTypeRepr>(t);
    if (!n->Path.empty() && n->Path.back() == owner)
      for (const auto &a : n->GenericArgs)
        if (grownArg(a.get()))
          return true;
    for (const auto &a : n->GenericArgs)
      if (growsOwner(a.get(), owner, params))
        return true;
    return false;
  }
  case NodeKind::OptionalType: {
    const auto *o = cast<OptionalTypeRepr>(t);
    if (owner == "Option" && grownArg(o->Element.get()))
      return true;
    return growsOwner(o->Element.get(), owner, params);
  }
  case NodeKind::PointerType:
    return growsOwner(cast<PointerTypeRepr>(t)->Pointee.get(), owner, params);
  case NodeKind::ArrayType:
    return growsOwner(cast<ArrayTypeRepr>(t)->Element.get(), owner, params);
  case NodeKind::SliceType:
    return growsOwner(cast<SliceTypeRepr>(t)->Element.get(), owner, params);
  case NodeKind::TupleType:
    for (const auto &e : cast<TupleTypeRepr>(t)->Elements)
      if (growsOwner(e.get(), owner, params))
        return true;
    return false;
  default:
    return false;
  }
}

bool Sema::signatureWrapsSelf(const FunctionDecl *fn) {
  if (!fn)
    return false;
  if (wrapsSelf(fn->ReturnType.get()))
    return true;
  for (const Param &p : fn->Params)
    if (wrapsSelf(p.TypeAnnotation.get()))
      return true;
  if (auto *nd = fn->Parent ? dyn_cast<NominalDecl>(fn->Parent) : nullptr) {
    NominalDecl *tmpl = nd->GenericTemplate ? nd->GenericTemplate : nd;
    if (!tmpl->Generics.empty()) {
      std::set<std::string> params;
      for (const auto &g : tmpl->Generics)
        params.insert(g.Name);
      const std::string &owner = static_cast<Decl *>(tmpl)->Name;
      if (growsOwner(fn->ReturnType.get(), owner, params))
        return true;
      for (const Param &p : fn->Params)
        if (growsOwner(p.TypeAnnotation.get(), owner, params))
          return true;
    }
  }
  return false;
}

/// Resolves a method's signature in the module the method was written in.
/// Almost always the current one; a mark default copied into a binding
/// elsewhere is the exception, and its names live in the mark's file.
void Sema::resolveMethodSignature(FunctionDecl *fn, Type *selfType,
                                  const std::vector<Type *> &typeArgs) {
  Scope *savedScope = CurScope;
  Module *savedModule = CurModule;
  if (!fn->ModulePath.empty() &&
      (!CurModule || fn->ModulePath != CurModule->Name)) {
    auto mit = ModulesByName.find(fn->ModulePath);
    if (mit != ModulesByName.end()) {
      CurModule = mit->second;
      CurScope = scopeForModule(mit->second);
    }
  }
  std::vector<Type *> params;
  for (Param &p : fn->Params) {
    if (p.IsSelf) {
      p.Ty = selfTypeFor(p, selfType);
      continue;
    }
    p.Ty = resolveTypeOrError(p.TypeAnnotation.get(), Types.errorType());
    params.push_back(p.Ty);
  }
  Type *ret = resolveReturnType(fn, selfType);
  fn->Ty = Types.functionOf(params, ret, fn->IsVariadic);
  fn->MangledName = mangleFunction(fn, typeArgs);
  CurScope = savedScope;
  CurModule = savedModule;
}

NominalDecl *Sema::instantiateNominal(NominalDecl *tmpl,
                                      const std::vector<Type *> &args,
                                      SourceRange range) {
  // Keyed on identity, not on how the arguments print: two modules may each
  // declare an `Item`, and `Vector<Item>` means a different type in each. A
  // key built out of spellings would hand the second one the first one's
  // instantiation, and every value would then be the wrong type by the name
  // it goes by.
  std::string key = instantiationKey(static_cast<Decl *>(tmpl), args);
  auto it = NominalInstances.find(key);
  if (it != NominalInstances.end())
    return it->second;

  // A type parameterised by itself — `struct Nest<T> { deeper: Nest<Nest<T>> }`
  // — asks for an instantiation that asks for another, forever. Stop, and say
  // where, rather than running out of stack.
  if (NominalDepth > 24) {
    Diags.error(range, "generic instantiation is too deeply nested")
        .note("each one asks for another: look for a type argument that wraps "
              "the very type being declared")
        .note("a recursive structure holds itself — `Nest<T>?` — rather than a "
              "deeper instantiation of itself")
        .code(240);
    return nullptr;
  }
  ++NominalDepth;
  struct DepthGuard {
    unsigned &D;
    ~DepthGuard() { --D; }
  } depthGuard{NominalDepth};

  DeclPtr cloned = cloneDecl(static_cast<Decl *>(tmpl));
  if (!cloned)
    return nullptr;
  auto *inst = static_cast<NominalDecl *>(cloned.get());
  Synthesised.push_back(std::move(cloned));
  NominalInstances[key] = inst;

  inst->GenericTemplate = tmpl;
  inst->TypeArguments = args;
  inst->Generics.clear();
  // An instantiation of Option or Result is still Option or Result.
  inst->Lang = tmpl->Lang;
  tmpl->Instantiations.push_back(inst);
  inst->DeclaredType = Types.nominalOf(inst, args);

  // Check the copy in the template's own module, so its private helpers and
  // extern declarations resolve exactly as they do in the original.
  Scope *savedScope = CurScope;
  Module *savedModule = CurModule;
  auto tmplModule = ModulesByName.find(static_cast<Decl *>(tmpl)->ModulePath);
  if (tmplModule != ModulesByName.end()) {
    CurModule = tmplModule->second;
    CurScope = scopeForModule(tmplModule->second);
  }

  // Bounds written on a type's parameters mean what they do on a function's:
  // `class Arc<T: Sync>` is a promise the arguments have to keep. Checked
  // here so `Arc<SomeClass>` is refused where it is written, rather than at
  // whichever method first needs the bound.
  for (size_t i = 0; i < tmpl->Generics.size() && i < args.size(); ++i)
    for (const auto &bound : tmpl->Generics[i].Bounds)
      checkGenericBound(args[i], bound.get(), range, tmpl->Generics[i].Range,
                        static_cast<Decl *>(tmpl)->Name);

  // Bind the template's parameters to the concrete arguments while resolving.
  auto savedGenerics = ActiveGenericParams;
  Type *savedSelf = ActiveSelfType;
  ActiveGenericParams.clear();
  bindTemplateGenerics(tmpl, args, ActiveGenericParams);
  ActiveSelfType = inst->DeclaredType;

  for (auto &f : inst->Fields)
    f->Ty = resolveTypeOrError(f->TypeAnnotation.get(), Types.errorType());
  if (auto *e = dyn_cast<EnumDecl>(static_cast<Decl *>(inst))) {
    assignVariantValues(e);
    for (auto &v : e->Variants) {
      for (auto &tt : v->TupleTypes)
        resolveTypeOrError(tt.get(), Types.errorType());
      for (auto &f : v->Fields)
        f->Ty = resolveTypeOrError(f->TypeAnnotation.get(), Types.errorType());
    }
  }
  resolveFieldAnnotations(inst);
  checkFieldDefaults(inst);
  registerMethods(inst);
  if (auto *c = dyn_cast<ClassDecl>(static_cast<Decl *>(inst)))
    layoutClass(c);

  // Generic binds whose target matches this template now apply to the clone.
  for (Module *m : Modules)
    for (auto &d : m->Decls) {
      auto *b = dyn_cast<BindDecl>(d.get());
      if (!b || b->Generics.empty() || !b->TargetType)
        continue;
      auto *named = dyn_cast<NamedTypeRepr>(b->TargetType.get());
      if (!named)
        continue;
      // Which template this bind is for was decided when the bind was
      // collected, in the module it was written in. Resolving the path again
      // here would resolve it in *this* scope instead, so a `bind ... to
      // Map<K, V>` in one module would match another module's `Map` — and
      // then apply its methods to the wrong type, with the wrong parameters
      // in scope.
      NominalDecl *targetTmpl = nullptr;
      if (b->ResolvedTarget && b->ResolvedTarget->isNominal()) {
        targetTmpl = b->ResolvedTarget->nominal();
      } else {
        // Falling back to the path means resolving a name, and a name means
        // nothing without a scope: `Map` in this module is not `Map` in the
        // one the bind was written in. Look it up there.
        Scope *savedScope = CurScope;
        Module *savedModule = CurModule;
        if (Scope *own = scopeForModule(m)) {
          CurScope = own;
          CurModule = m;
        }
        targetTmpl = lookupNominal(named->Path, named->Range, true);
        CurScope = savedScope;
        CurModule = savedModule;
      }
      if (targetTmpl != tmpl)
        continue;

      // The bind names this template, but the shapes pass may not have
      // reached the module it was written in yet: a `.rul` interface is
      // parsed ahead of the standard library, and a `Vector<T>` field in one
      // instantiates `Vector` while `bind<T> Sequence to Vector<T>` is still
      // untouched. Applied in that state the bind would hand its methods over
      // without the mark behind them — no conformance, so `for` cannot ask
      // for an `Iterator`, no associated types, no `where` clause to say
      // whether it applies at all, and none of the mark's defaults, which are
      // copied into the bind by the very pass that has not run. So prepare it
      // now, and let the pass find it done.
      prepareBind(m, b);
      if (!b->ResolvedTarget || !b->ResolvedTarget->isNominal() ||
          b->ResolvedTarget->nominal() != tmpl)
        continue;
      {
        auto &known = TemplateBinds[tmpl];
        if (std::find(known.begin(), known.end(), std::make_pair(m, b)) ==
            known.end())
          known.push_back({m, b});
      }

      // What the bind's parameters stand for, read off the target it was
      // written against: `bind<T> Show to Wrapper<T>` applied to
      // `Wrapper<i64>` binds T = i64.
      //
      // Each argument is *matched*, not assumed to be a bare parameter. A
      // target like `Wrapper<Wrapper<T>>` names a shape the instantiation may
      // not have, and a bind that does not fit it does not apply.
      std::map<std::string, Type *> bindArgs;
      if (b->ResolvedTarget) {
        const auto &placeholders = b->ResolvedTarget->typeArguments();
        if (placeholders.size() != args.size())
          continue;
        bool fits = true;
        for (size_t k = 0; k < placeholders.size(); ++k)
          if (!unifyGenericArg(placeholders[k], args[k], bindArgs))
            fits = false;
        if (!fits)
          continue;
        // A parameter the target never mentions would leave a method with a
        // type nobody chose.
        if (bindArgs.size() != b->Generics.size())
          continue;
      }

      // A `where` clause decides whether this bind applies at all, so
      // `bind<T> Show to Wrapper<T> where T: Show` leaves `Wrapper<i64>`
      // alone unless i64 is itself bound to Show.
      if (!bindApplies(m, b, bindArgs)) {
        // Asked before every module's binds are in, the answer may only be
        // "not yet": `Vector<String>` instantiated by a module shaped ahead
        // of the one that binds `Display` to `String`. Ask again once they
        // are all registered.
        if (!ShapesDone)
          DeferredConditionalBinds.push_back(
              {static_cast<NominalDecl *>(inst), m, b, bindArgs});
        continue;
      }

      registerBindFor(b, inst->DeclaredType, static_cast<Decl *>(inst),
                      bindArgs);
    }

  // Signatures, then bodies. A method with its own type parameters is left
  // alone here: those are only known at its call site, which is where
  // ensureTemplateSignature resolves it.
  for (auto &fn : inst->Methods) {
    if (!fn->Generics.empty())
      continue;
    if (signatureWrapsSelf(fn.get())) {
      // Resolved at the call instead; the name is fixed either way.
      fn->MangledName = mangleFunction(fn.get(), args);
      DeferredMethods.insert(fn.get());
      continue;
    }
    resolveMethodSignature(fn.get(), inst->DeclaredType, args);
  }
  resolveSuppliedSignatures(inst);

  // What this instantiation destroys, before anything that mentions it is
  // checked: a body that stores one of these into a field is only a move if
  // the value owns something, and that is what this answers.
  {
    NominalDecl *owner = nullptr;
    if (FunctionDecl *found =
            lookupMethod(inst->DeclaredType, "deinit", &owner))
      if (owner == inst) {
        inst->Deinit = found;
        checkDeinitSignature(inst, found);
      }
  }

  // Bodies come last. An instantiation asked for during an earlier pass — a
  // `Vector<T>` field on a module-level struct is enough — would otherwise be
  // checked against a half-built program: `mem::allocator` without a type
  // yet, or `bind Allocator to SystemAllocator` not yet registered.
  Result.Nominals.push_back(inst);
  if (InBodyPass)
    checkInstantiatedBodies(inst);
  else
    PendingInstantiations.push_back(inst);

  ActiveGenericParams = savedGenerics;
  ActiveSelfType = savedSelf;
  CurScope = savedScope;
  CurModule = savedModule;
  return inst;
}

FunctionDecl *Sema::instantiate(FunctionDecl *tmpl,
                                const std::vector<Type *> &args,
                                SourceRange range) {
  // An argument that is already an error was reported where it arose. An
  // instance built from it would only report it again, from inside a body
  // the program never wrote — `std::fmt::show` saying '<error>' has no
  // member 'display', under a name that could not be found.
  for (Type *a : args) {
    Type *t = a;
    while (t && t->is(TypeKind::Pointer) && t->pointee())
      t = t->pointee();
    if (!t || t->isError())
      return nullptr;
  }

  std::string key = instantiationKey(tmpl, args);
  auto it = FunctionInstances.find(key);
  if (it != FunctionInstances.end())
    return it->second;

  if (InstantiationStack.size() > 32) {
    auto d = Diags.error(range, "generic instantiation is too deeply nested");
    d.note("this usually means a generic function instantiates itself with an "
           "ever-growing type")
        .code(240);
    return nullptr;
  }

  auto cloned = cloneFunction(tmpl);
  FunctionDecl *inst = cloned.get();
  Synthesised.push_back(std::move(cloned));
  FunctionInstances[key] = inst;

  inst->GenericTemplate = tmpl;
  inst->TypeArguments = args;
  inst->Generics.clear();
  tmpl->Instantiations.push_back(inst);

  auto savedGenerics = ActiveGenericParams;
  Scope *savedScope = CurScope;
  Module *savedModule = CurModule;

  // Instantiate in the template's own module so its private names resolve.
  auto mit = ModulesByName.find(tmpl->ModulePath);
  if (mit != ModulesByName.end()) {
    CurModule = mit->second;
    CurScope = scopeForModule(mit->second);
  }
  ActiveGenericParams.clear();
  // A method of an instantiated generic type also needs that type's arguments.
  bindOwnerGenerics(tmpl, ActiveGenericParams);
  for (size_t i = 0; i < tmpl->Generics.size() && i < args.size(); ++i)
    ActiveGenericParams[tmpl->Generics[i].Name] = args[i];

  // Bounds declared on the template must hold for the arguments given. When
  // one does not, checking the body would only produce cascading noise about
  // methods the argument was never going to have, so stop after reporting.
  bool boundsSatisfied = true;
  for (size_t i = 0; i < tmpl->Generics.size() && i < args.size(); ++i)
    for (const auto &bound : tmpl->Generics[i].Bounds)
      if (!checkGenericBound(args[i], bound.get(), range,
                             tmpl->Generics[i].Range, tmpl->Name))
        boundsSatisfied = false;
  // `where` says the same thing as a bound written on the parameter, about a
  // subject that may be more than a parameter name — `where T::Item: Display`
  // has nowhere else to go. The arguments are already bound here, so
  // resolving the subject substitutes them.
  if (!checkWhereClauses(tmpl->WhereClauses, range, tmpl->Name))
    boundsSatisfied = false;

  std::vector<Type *> params;
  Type *selfType = nullptr;
  if (tmpl->Parent)
    if (auto *nd = dyn_cast<NominalDecl>(tmpl->Parent))
      selfType = nd->DeclaredType;
  Type *savedSelf = ActiveSelfType;
  if (selfType)
    ActiveSelfType = selfType;

  // Resolving this signature instantiates whatever generic types it mentions
  // — the return type usually is one — and an instantiation checks its own
  // method bodies. One of those may call straight back here: `Box<T>` has a
  // `duplicate` that calls `boxed<T>`, whose return type is `Box<T>`. The
  // call would then meet this very instantiation with no signature on it yet,
  // because this is the line that gives it one. So bodies are queued while
  // the signature is worked out and checked immediately afterwards, by which
  // time the call finds a signature that is finished.
  const bool wasBodyPass = InBodyPass;
  InBodyPass = false;
  for (Param &p : inst->Params) {
    if (p.IsSelf) {
      p.Ty = selfTypeFor(p, selfType ? selfType : Types.errorType());
      continue;
    }
    p.Ty = resolveTypeOrError(p.TypeAnnotation.get(), Types.errorType());
    params.push_back(p.Ty);
  }
  Type *ret = resolveReturnType(inst, selfType);
  inst->Ty = Types.functionOf(params, ret, inst->IsVariadic);
  inst->MangledName = mangleFunction(tmpl, args);
  InBodyPass = wasBodyPass;
  if (InBodyPass)
    drainPendingInstantiations();

  if (boundsSatisfied) {
    InstantiationStack.push_back({tmpl, range});
    checkFunction(inst, selfType,
                  selfType && selfType->is(TypeKind::Class)
                      ? reinterpret_cast<ClassDecl *>(selfType->nominal())
                      : nullptr);
    InstantiationStack.pop_back();
    Result.Functions.push_back(inst);
  }

  ActiveGenericParams = savedGenerics;
  ActiveSelfType = savedSelf;
  CurScope = savedScope;
  CurModule = savedModule;
  return inst;
}

//===----------------------------------------------------------------------===//
// Statements
//===----------------------------------------------------------------------===//

void Sema::checkStmt(Stmt *s) {
  if (!s)
    return;
  switch (s->Kind) {
  case NodeKind::ExprStmt:
    // A statement on its own: nothing wants what it produces.
    ValueDiscarded = true;
    checkExpr(cast<ExprStmt>(s)->Value.get(), nullptr);
    ValueDiscarded = false;
    break;
  case NodeKind::VarStmt:
    checkVarStmt(cast<VarStmtNode>(s));
    break;
  case NodeKind::DeferStmt:
    checkExpr(cast<DeferStmtNode>(s)->Body.get(), nullptr);
    break;
  case NodeKind::DeclStmtKind: {
    Decl *d = cast<DeclStmt>(s)->Inner.get();
    if (auto *fn = dyn_cast<FunctionDecl>(d)) {
      // A nested function is a plain top-level function with a private name.
      fn->ModulePath = CurModule ? CurModule->Name : "";
      Symbol sym;
      sym.Kind = SymbolKind::Function;
      sym.Name = fn->Name;
      sym.D = fn;
      CurScope->overwrite(sym);
      if (fn->Generics.empty()) {
        std::vector<Type *> params;
        for (Param &p : fn->Params) {
          p.Ty = resolveTypeOrError(p.TypeAnnotation.get(), Types.errorType());
          params.push_back(p.Ty);
        }
        Type *ret = resolveReturnType(fn, nullptr);
        fn->Ty = Types.functionOf(params, ret);
        fn->MangledName = mangleFunction(fn, {});
        checkFunction(fn, nullptr, nullptr);
        Result.Functions.push_back(fn);
      }
      break;
    }
    // Everything else is a type, and types are collected before any body is
    // checked — one written here is found far too late to be registered, so
    // every use of it would fail with a bare "cannot find" somewhere else.
    const char *what = nullptr;
    switch (d->Kind) {
    case NodeKind::Struct: what = "struct"; break;
    case NodeKind::Enum: what = "enum"; break;
    case NodeKind::Class: what = "class"; break;
    case NodeKind::Mark: what = "mark"; break;
    case NodeKind::Extend: what = "extend"; break;
    case NodeKind::TypeAlias: what = "type"; break;
    case NodeKind::Bind: what = "bind"; break;
    default: break;
    }
    if (what) {
      auto e = Diags.error(d->Range, "`{}` cannot be declared inside a body",
                           what);
      e.note("types are collected before any body is checked, so one written "
             "here is never registered");
      e.note("move it to file scope");
      e.code(203);
    }
    break;
  }
  default:
    break;
  }
}

void Sema::checkVarStmt(VarStmtNode *v) {
  Type *declared = v->TypeAnnotation
                       ? resolveTypeOrError(v->TypeAnnotation.get(), nullptr)
                       : nullptr;
  if (v->TypeAnnotation) {
    resolveLocalAnnotations(v->TypeAnnotation.get());
    rejectCxxClassByValue(declared, v->TypeAnnotation->Range, "the variable");
    rejectMarkByValue(declared, v->TypeAnnotation->Range, "a binding");
  }
  Type *initTy = nullptr;
  if (v->Init) {
    initTy = checkExpr(v->Init.get(), declared);
    if (declared && insertImplicitConversion(v->Init, initTy, declared))
      initTy = v->Init->Ty;
    if (declared)
      requireConvertible(v->Init.get(), initTy, declared, "this initialiser");
    else if (initTy)
      // `requireConvertible` is the funnel every store passes through, and it
      // is what records a move — but an inferred binding never reaches it,
      // because there is nothing to check the initialiser against. The
      // binding still takes ownership of what it is given, so say so here.
      markOwnedMove(v->Init.get(), initTy, initTy);
  }

  Type *final = declared ? declared : initTy;
  if (!final) {
    auto d = Diags.error(v->Range, "cannot infer a type for this binding");
    d.note("add a type annotation (`name: i64`) or an initialiser").code(250);
    final = Types.errorType();
  }
  if (final->isVoid() && v->Init) {
    Diags.error(v->Init->Range, "cannot bind a value of type '()'")
        .note("this expression produces no value")
        .code(251);
    final = Types.errorType();
  }

  // A binding with no initialiser starts as zero bytes. For a value type that
  // is a value; for a reference it is no object at all, and reading through it
  // is a null dereference the safety checks cannot catch, because there is
  // nothing there to check.
  if (!v->Init && final && !final->isError() && final->isRefCounted()) {
    auto d = Diags.error(v->Range,
                         "'{}' has no empty value, so this binding needs one",
                         final->toString());
    d.note("a class, a String, or anything holding one is a reference — "
           "zero bytes is not an object, and reading it would be a null "
           "dereference");
    if (final->is(TypeKind::Class))
      d.note(fmt("give it one now, or declare it `{}?` and start at `nil`",
                 final->toString())
                 .c_str());
    else
      d.note("give it a value now, or make the type optional and start at "
             "`nil`");
    d.code(253);
    final = Types.errorType();
  }

  if (v->IsGlobal) {
    // `global x` inside a function refers to a module-level binding.
    auto *b = dyn_cast<BindingPattern>(v->Binding.get());
    if (!b) {
      Diags.error(v->Range, "a global declaration needs a simple name").code(252);
      return;
    }
    Symbol *existing = ModuleScopes.count(CurModule)
                           ? ModuleScopes[CurModule]->findLocal(b->Name)
                           : nullptr;
    if (existing && existing->D) {
      Symbol s = *existing;
      CurScope->overwrite(s);
      return;
    }
    Synthesised.push_back(std::make_unique<GlobalVarDecl>());
    auto *g = static_cast<GlobalVarDecl *>(Synthesised.back().get());
    g->Name = b->Name;
    g->NameRange = b->Range;
    g->Range = v->Range;
    g->Ty = final;
    g->IsMutable = v->IsMutable;
    g->ModulePath = CurModule ? CurModule->Name : "";
    Result.Globals.push_back(g);
    Symbol s;
    s.Kind = SymbolKind::Value;
    s.Name = g->Name;
    s.D = g;
    s.Ty = final;
    if (ModuleScopes.count(CurModule))
      ModuleScopes[CurModule]->overwrite(s);
    CurScope->overwrite(s);
    return;
  }

  checkPattern(v->Binding.get(), final, /*declaresBindings=*/true, v->IsMutable);
  v->Binding->Ty = final;
  // A binding has no arm to fall through to, so its pattern has to fit
  // every value the initialiser could produce.
  if (!patternIsIrrefutable(v->Binding.get()))
    Diags.error(v->Binding->Range, "a `let` pattern has to match every value")
        .note("this one can fail, and a binding has no other arm to go to")
        .note("test it with `if value is <pattern> { ... }`, or `match` on it")
        .code(394);
}

//===----------------------------------------------------------------------===//
// Patterns
//===----------------------------------------------------------------------===//
namespace {
/// Collects the binding nodes a pattern introduces, keyed by name. Variant
/// tests look like bindings syntactically but introduce nothing.
void collectPatternBindings(Pattern *p,
                            std::map<std::string, BindingPattern *> &out) {
  if (!p)
    return;
  switch (p->Kind) {
  case NodeKind::BindingPat: {
    auto *b = cast<BindingPattern>(p);
    if (!b->isVariantTest())
      out[b->Name] = b;
    collectPatternBindings(b->Sub.get(), out);
    return;
  }
  case NodeKind::TuplePat:
    for (auto &e : cast<TuplePattern>(p)->Elements)
      collectPatternBindings(e.get(), out);
    return;
  case NodeKind::EnumPat:
    for (auto &e : cast<EnumPattern>(p)->Elements)
      collectPatternBindings(e.get(), out);
    return;
  case NodeKind::StructPat:
    for (auto &f : cast<StructPattern>(p)->Fields)
      collectPatternBindings(f.Value.get(), out);
    return;
  case NodeKind::SlicePat: {
    auto *sp = cast<SlicePattern>(p);
    for (auto &e : sp->Prefix)
      collectPatternBindings(e.get(), out);
    collectPatternBindings(sp->Rest.get(), out);
    for (auto &e : sp->Suffix)
      collectPatternBindings(e.get(), out);
    return;
  }
  case NodeKind::RefPat:
    collectPatternBindings(cast<RefPattern>(p)->Sub.get(), out);
    return;
  case NodeKind::OrPat:
    for (auto &a : cast<OrPattern>(p)->Alternatives)
      collectPatternBindings(a.get(), out);
    return;
  default:
    return;
  }
}
} // namespace



bool Sema::patternIsIrrefutable(const Pattern *p) const {
  if (!p)
    return true;
  switch (p->Kind) {
  case NodeKind::WildcardPat:
    return true;
  case NodeKind::BindingPat: {
    const auto *b = cast<BindingPattern>(p);
    if (b->isVariantTest())
      return false;
    if (b->Sub)
      return patternIsIrrefutable(b->Sub.get());
    return true;
  }
  case NodeKind::TuplePat:
    for (const auto &e : cast<TuplePattern>(p)->Elements)
      if (!patternIsIrrefutable(e.get()))
        return false;
    return true;
  case NodeKind::RefPat:
    return patternIsIrrefutable(cast<RefPattern>(p)->Sub.get());
  case NodeKind::SlicePat: {
    // Against a fixed array whose length the pattern accounts for, a slice
    // pattern always matches; against a slice, only `[..]` does, because
    // every other shape asks something of the length.
    const auto *sp = cast<SlicePattern>(p);
    if (sp->Ty && sp->Ty->isError())
      return true; // already reported; do not pile on
    for (const auto &e : sp->Prefix)
      if (!patternIsIrrefutable(e.get()))
        return false;
    for (const auto &e : sp->Suffix)
      if (!patternIsIrrefutable(e.get()))
        return false;
    const size_t named = sp->Prefix.size() + sp->Suffix.size();
    if (sp->Ty && sp->Ty->is(TypeKind::Array))
      return sp->HasRest ? named <= sp->Ty->arraySize()
                         : named == sp->Ty->arraySize();
    return sp->HasRest && named == 0;
  }
  case NodeKind::StructPat:
    // A struct pattern is irrefutable only when it names a struct, not an
    // enum variant; the caller has already resolved that.
    return cast<StructPattern>(p)->VariantIndex < 0;
  default:
    return false;
  }
}

void Sema::checkPattern(Pattern *p, Type *scrutinee, bool declaresBindings,
                        bool isMutable) {
  if (!p)
    return;
  p->Ty = scrutinee;
  if (!scrutinee)
    scrutinee = Types.errorType();

  // A `some` has no shape a pattern may name: it binds whole, or not at all.
  if (scrutinee->isOpaque() && p->Kind != NodeKind::WildcardPat &&
      p->Kind != NodeKind::BindingPat && p->Kind != NodeKind::RefPat) {
    rejectOpaqueUse(scrutinee, p->Range, "taking the value apart");
    p->Ty = Types.errorType();
    return;
  }

  switch (p->Kind) {
  case NodeKind::WildcardPat:
    break;

  case NodeKind::BindingPat: {
    auto *b = cast<BindingPattern>(p);
    // In a match arm, a bare name that happens to be a unit variant of the
    // scrutinee's enum tests for that variant instead of binding.
    if ((!declaresBindings || b->MustBeVariant) &&
        scrutinee->is(TypeKind::Enum) && !scrutinee->isOpaque() && !b->Sub) {
      auto *e = reinterpret_cast<EnumDecl *>(scrutinee->nominal());
      for (const auto &v : e->Variants) {
        if (v->Name != b->Name || v->Shape != VariantShape::Unit)
          continue;
        b->VariantOwner = e;
        b->VariantIndex = static_cast<int>(v->Index);
        return;
      }
    }
    // Written `.Red`: it names something on the type being matched, so a
    // type with no such variant is a mistake rather than a new binding.
    if (b->MustBeVariant) {
      auto d = Diags.error(b->Range, "'{}' has no variant '{}'",
                           scrutinee->toString(), b->Name);
      d.note("a leading `.` names a variant of the type being matched; drop "
             "it to bind the value to a name of your own")
          .code(204);
      if (scrutinee->isNominal() && scrutinee->nominal())
        noteDeclaredAt(d, static_cast<Decl *>(scrutinee->nominal()),
                       "declared here", "no variant of that name");
      break;
    }
    // `ref name` is a borrow of what matched, `ref var name` one that may
    // write through; the binding itself is never reassigned.
    VarDecl *v =
        b->ByRef
            ? declareLocal(b->Name,
                           Types.pointerTo(scrutinee, b->IsMutable, false),
                           /*mutable=*/false, b->Range)
            : declareLocal(b->Name, scrutinee, isMutable || b->IsMutable,
                           b->Range);
    b->Binding = v;
    if (b->Sub)
      checkPattern(b->Sub.get(), scrutinee, declaresBindings, isMutable);
    break;
  }

  case NodeKind::LiteralPat: {
    auto *l = cast<LiteralPattern>(p);
    Type *lt = checkExpr(l->Value.get(), scrutinee);
    if (!isImplicitlyConvertible(lt, scrutinee) && !lt->isError() &&
        !scrutinee->isError())
      Diags.error(l->Range, "expected '{}' — got '{}'", scrutinee->toString(),
                  lt->toString())
          .note("a literal pattern must have the same type as the value being "
                "matched")
          .code(253);
    break;
  }

  case NodeKind::RangePat: {
    auto *r = cast<RangePattern>(p);
    if (r->Lo) checkExpr(r->Lo.get(), scrutinee);
    if (r->Hi) checkExpr(r->Hi.get(), scrutinee);
    if (!scrutinee->isNumeric() && !scrutinee->is(TypeKind::Char) &&
        !scrutinee->isError())
      Diags.error(r->Range, "range patterns need a numeric or Character value, "
                            "not '{}'",
                  scrutinee->toString())
          .code(254);
    break;
  }

  case NodeKind::TuplePat: {
    auto *t = cast<TuplePattern>(p);
    if (!scrutinee->is(TypeKind::Tuple)) {
      if (!scrutinee->isError())
        Diags.error(t->Range, "expected '{}' — got a tuple pattern",
                    scrutinee->toString())
            .code(255);
      for (auto &e : t->Elements)
        checkPattern(e.get(), Types.errorType(), declaresBindings, isMutable);
      break;
    }
    const auto &elems = scrutinee->tupleElements();
    if (elems.size() != t->Elements.size()) {
      Diags.error(t->Range, "expected {} element(s) — got {}", elems.size(),
                  t->Elements.size())
          .note(fmt("the value has type '{}'", scrutinee->toString()).c_str())
          .code(256);
    }
    // An element that is a borrow is looked through by anything that looks
    // inside it, as `match x` looks through a borrowed `x`; a name binds
    // the borrow itself. (CodeGen reads through on the same rule.)
    auto looksInside = [&](Pattern *sub, Type *et) {
      if (!sub || sub->Kind == NodeKind::WildcardPat)
        return false;
      if (sub->Kind != NodeKind::BindingPat)
        return true;
      auto *b = cast<BindingPattern>(sub);
      if (b->Sub || (declaresBindings && !b->MustBeVariant))
        return false;
      if (b->MustBeVariant)
        return true;
      // A bare name that is a unit variant of the enum behind the borrow.
      while (et->is(TypeKind::Pointer) && et->pointee())
        et = et->pointee();
      if (!et->is(TypeKind::Enum) || et->isOpaque())
        return false;
      for (const auto &v :
           reinterpret_cast<EnumDecl *>(et->nominal())->Variants)
        if (v->Name == b->Name && v->Shape == VariantShape::Unit)
          return true;
      return false;
    };
    for (size_t i = 0; i < t->Elements.size(); ++i) {
      Type *et = i < elems.size() ? elems[i] : Types.errorType();
      if (looksInside(t->Elements[i].get(), et))
        while (et->is(TypeKind::Pointer) && !et->isRawPointer() &&
               !et->isWeakPointer() && et->pointee())
          et = et->pointee();
      checkPattern(t->Elements[i].get(), et, declaresBindings, isMutable);
    }
    break;
  }

  case NodeKind::RefPat: {
    auto *r = cast<RefPattern>(p);
    Type *inner = scrutinee->is(TypeKind::Pointer) ? scrutinee->pointee()
                                                   : scrutinee;
    checkPattern(r->Sub.get(), inner, declaresBindings, isMutable);
    break;
  }

  case NodeKind::OrPat: {
    auto *o = cast<OrPattern>(p);
    if (o->Alternatives.empty())
      break;

    // Every alternative has to introduce the same names with the same types,
    // because the body cannot know which one matched. The first alternative's
    // variables are the ones that survive; the rest are pointed at them so a
    // single slot serves all of them at run time.
    checkPattern(o->Alternatives[0].get(), scrutinee, declaresBindings, isMutable);
    std::map<std::string, BindingPattern *> firstBindings;
    collectPatternBindings(o->Alternatives[0].get(), firstBindings);

    for (size_t i = 1; i < o->Alternatives.size(); ++i) {
      Pattern *alt = o->Alternatives[i].get();
      checkPattern(alt, scrutinee, declaresBindings, isMutable);

      std::map<std::string, BindingPattern *> altBindings;
      collectPatternBindings(alt, altBindings);

      for (auto &entry : altBindings) {
        auto it = firstBindings.find(entry.first);
        if (it == firstBindings.end()) {
          auto d = Diags.error(entry.second->Range,
                               "'{}' is bound here but not in every "
                               "alternative of this pattern",
                               entry.first);
          d.note("each alternative of a `|` pattern must bind the same names")
              .code(263);
          d.related(o->Alternatives[0]->Range, "the first alternative",
                    fmt("add '{}' here, or drop it from the other alternatives",
                        entry.first));
          continue;
        }
        VarDecl *shared = it->second->Binding;
        VarDecl *mine = entry.second->Binding;
        if (shared && mine && shared->Ty != mine->Ty && !shared->Ty->isError() &&
            !mine->Ty->isError()) {
          Diags.error(entry.second->Range,
                      "expected '{}' — got '{}'", shared->Ty->toString(),
                      mine->Ty->toString())
              .note(fmt("'{}' must have the same type in every alternative",
                        entry.first).c_str())
              .code(264);
        }
        // Share the first alternative's variable.
        entry.second->Binding = shared;
        if (shared) {
          Symbol sym;
          sym.Kind = SymbolKind::Value;
          sym.Name = entry.first;
          sym.D = shared;
          sym.Ty = shared->Ty;
          CurScope->overwrite(sym);
        }
      }
      for (auto &entry : firstBindings)
        if (!altBindings.count(entry.first))
          Diags.error(alt->Range,
                      "this alternative does not bind '{}'", entry.first)
              .note("each alternative of a `|` pattern must bind the same names")
              .code(263);
    }
    break;
  }

  case NodeKind::PathPat: {
    auto *pp = cast<PathPattern>(p);
    Symbol *sym = lookupPath(pp->Path, pp->Range, false);
    if (!sym)
      break;
    if (sym->Kind == SymbolKind::Variant) {
      EnumDecl *owner = sym->Owner;
      const int variantIndex = sym->VariantIndex;
      if (!owner->Generics.empty()) {
        owner = instantiateVariantOwner(owner,
                                        static_cast<unsigned>(variantIndex), {},
                                        {}, scrutinee, pp->Range);
        if (!owner)
          break;
      }
      pp->ResolvedDecl = owner;
      pp->VariantIndex = variantIndex;
      Type *vt = owner->DeclaredType;
      if (vt && scrutinee && !scrutinee->isError() && vt != scrutinee)
        Diags.error(pp->Range, "expected '{}' — got a '{}' variant",
                    scrutinee->toString(), vt->toString())
            .code(257);
    } else {
      Diags.error(pp->Range, "'{}' is not an enum variant", sym->Name)
          .note("only enum variants and literals may be matched by name")
          .code(258);
    }
    break;
  }

  case NodeKind::EnumPat: {
    auto *ep = cast<EnumPattern>(p);
    // A bare variant name is settled by what is being matched — which is
    // always known here, and is the only thing `.Circle(r)` could mean. That
    // matters most across a module boundary, where a variant's bare name is
    // not in scope and `Shape::Circle` would otherwise have to be written in
    // full.
    Symbol *sym = nullptr;
    if (ep->Path.size() == 1)
      sym = variantOfExpected(ep->Path[0], scrutinee);
    if (!sym)
      sym = lookupPath(ep->Path, ep->Range, ep->Path.size() == 1);
    if (!sym && ep->Path.size() == 1) {
      auto d = Diags.error(ep->Range, "'{}' has no variant '{}'",
                           scrutinee->toString(), ep->Path[0]);
      d.note("a bare name in a pattern is a variant of the type being "
             "matched; write the enum out if it belongs to another one");
      d.code(258);
    }
    if (!sym)
      break;
    if (sym->Kind != SymbolKind::Variant) {
      Diags.error(ep->Range, "'{}' is not an enum variant", sym->Name).code(258);
      break;
    }
    auto *e = sym->Owner;
    const int variantIndex = sym->VariantIndex;
    // A pattern naming a generic enum takes its arguments from the value being
    // matched, which is always known here.
    if (!e->Generics.empty()) {
      e = instantiateVariantOwner(e, static_cast<unsigned>(variantIndex), {}, {},
                                  scrutinee, ep->Range);
      if (!e)
        break;
    }
    ep->ResolvedDecl = e;
    ep->VariantIndex = variantIndex;
    auto *variant = e->Variants[static_cast<size_t>(variantIndex)].get();
    if (variant->Shape != VariantShape::Tuple) {
      auto d = Diags.error(ep->Range, "variant '{}' takes no payload",
                           variant->Name);
      d.code(259);
      noteDeclaredAt(d, variant, "declared here",
                     "match it by name, without parentheses");
      break;
    }
    if (variant->TupleTypes.size() != ep->Elements.size()) {
      auto d = Diags.error(ep->Range, "variant '{}' has {} field(s) — {} given",
                           variant->Name, variant->TupleTypes.size(),
                           ep->Elements.size());
      d.code(260);
      noteDeclaredAt(d, variant, "declared here", "the arity must match");
    }
    for (size_t i = 0; i < ep->Elements.size(); ++i) {
      Type *ft = i < variant->TupleTypes.size()
                     ? variant->TupleTypes[i]->Resolved
                     : Types.errorType();
      checkPattern(ep->Elements[i].get(), ft ? ft : Types.errorType(),
                   declaresBindings, isMutable);
    }
    break;
  }

  case NodeKind::StructPat: {
    auto *sp = cast<StructPattern>(p);
    Symbol *sym = lookupPath(sp->Path, sp->Range, false);
    if (!sym)
      break;

    std::vector<FieldDecl *> fields;
    if (sym->Kind == SymbolKind::Variant) {
      auto *e = sym->Owner;
      const int variantIndex = sym->VariantIndex;
      if (!e->Generics.empty()) {
        e = instantiateVariantOwner(e, static_cast<unsigned>(variantIndex), {},
                                    {}, scrutinee, sp->Range);
        if (!e)
          break;
      }
      sp->ResolvedDecl = e;
      sp->VariantIndex = variantIndex;
      auto *variant = e->Variants[static_cast<size_t>(variantIndex)].get();
      for (auto &f : variant->Fields)
        fields.push_back(f.get());
    } else if (auto *nd = sym->D ? dyn_cast<NominalDecl>(sym->D) : nullptr) {
      sp->ResolvedDecl = static_cast<Decl *>(nd);
      for (auto &f : nd->Fields)
        fields.push_back(f.get());
    } else {
      Diags.error(sp->Range, "'{}' cannot be destructured", sym->Name).code(261);
      break;
    }

    for (auto &pf : sp->Fields) {
      FieldDecl *found = nullptr;
      for (FieldDecl *f : fields)
        if (f->Name == pf.Name)
          found = f;
      if (!found) {
        Diags.error(pf.Range, "no field named '{}' to destructure", pf.Name)
            .code(262);
        continue;
      }
      pf.FieldIndex = found->Index;
      Type *ft = found->Ty ? found->Ty : Types.errorType();
      if (pf.Value) {
        checkPattern(pf.Value.get(), ft, declaresBindings, isMutable);
      } else {
        // Shorthand `Point { x, y }` binds a variable per field.
        auto binding = std::make_unique<BindingPattern>();
        binding->Range = pf.Range;
        binding->Name = pf.Name;
        binding->Ty = ft;
        binding->Binding = declareLocal(pf.Name, ft, isMutable, pf.Range);
        pf.Value = std::move(binding);
      }
    }
    break;
  }

  case NodeKind::SlicePat: {
    auto *sp = cast<SlicePattern>(p);
    const bool isArray = scrutinee->is(TypeKind::Array);
    const bool isSlice = scrutinee->is(TypeKind::Slice);
    if (!isArray && !isSlice) {
      if (!scrutinee->isError())
        Diags.error(sp->Range, "expected '{}' — got a slice pattern",
                    scrutinee->toString())
            .note("`[a, b, ..]` takes apart an array or a slice")
            .code(265);
      for (auto &e : sp->Prefix)
        checkPattern(e.get(), Types.errorType(), declaresBindings, isMutable);
      for (auto &e : sp->Suffix)
        checkPattern(e.get(), Types.errorType(), declaresBindings, isMutable);
      if (sp->Rest)
        checkPattern(sp->Rest.get(), Types.errorType(), declaresBindings,
                     isMutable);
      break;
    }
    Type *elem = scrutinee->element();
    sp->ElementTy = elem;
    const size_t named = sp->Prefix.size() + sp->Suffix.size();
    // A fixed array's length is known, so a pattern that cannot line up with
    // it is a mistake rather than a test that fails.
    if (isArray) {
      const uint64_t size = scrutinee->arraySize();
      if (!sp->HasRest && named != size) {
        Diags.error(sp->Range, "expected {} element(s) — got {}", size, named)
            .note(fmt("the value has type '{}'; write `..` to match a "
                      "run of elements without naming each",
                      scrutinee->toString())
                      .c_str())
            .code(266);
        p->Ty = Types.errorType();
      } else if (sp->HasRest && named > size) {
        Diags.error(sp->Range, "this pattern names {} element(s), but the "
                               "array has only {}",
                    named, size)
            .note(fmt("the value has type '{}'", scrutinee->toString()).c_str())
            .code(266);
        p->Ty = Types.errorType();
      }
    }
    for (auto &e : sp->Prefix)
      checkPattern(e.get(), elem, declaresBindings, isMutable);
    for (auto &e : sp->Suffix)
      checkPattern(e.get(), elem, declaresBindings, isMutable);
    // `..rest` is the elements in between, as a slice into the same storage.
    if (sp->Rest)
      checkPattern(sp->Rest.get(), Types.sliceOf(elem), declaresBindings,
                   isMutable);
    break;
  }

  default:
    break;
  }
}

//===----------------------------------------------------------------------===//
// Symbol dump
//===----------------------------------------------------------------------===//

void Sema::dumpSymbols(std::ostream &os) const {
  for (Module *m : Modules) {
    os << "module " << m->Name << "\n";
    auto it = ModuleScopes.find(m);
    if (it == ModuleScopes.end())
      continue;
    for (const auto &entry : it->second->all()) {
      const Symbol &s = entry.second;
      const char *kind = "value";
      switch (s.Kind) {
      case SymbolKind::Function: kind = "fn"; break;
      case SymbolKind::TypeName: kind = "type"; break;
      case SymbolKind::MarkName: kind = "mark"; break;
      case SymbolKind::ModuleName: kind = "module"; break;
      case SymbolKind::Variant: kind = "variant"; break;
      case SymbolKind::Value: kind = "value"; break;
      }
      os << "  " << (s.IsPublic ? "pub " : "    ") << kind << "  " << s.Name;
      if (auto *fd = s.D ? dyn_cast<FunctionDecl>(s.D) : nullptr) {
        if (fd->Ty)
          os << " : " << fd->Ty->toString();
        if (!fd->MangledName.empty())
          os << "  [" << fd->MangledName << "]";
      } else if (auto *vd = s.D ? dyn_cast<GlobalVarDecl>(s.D) : nullptr) {
        if (vd->Ty)
          os << " : " << vd->Ty->toString();
      } else if (auto *nd = s.D ? dyn_cast<NominalDecl>(s.D) : nullptr) {
        if (nd->DeclaredType)
          os << " : " << nd->DeclaredType->toString();
        if (!nd->Generics.empty())
          os << "  <generic, " << nd->Instantiations.size()
             << " instantiation(s)>";
      }
      os << "\n";
    }
  }
}

} // namespace rune
