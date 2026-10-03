//===- Sema.h - Name resolution and type checking --------------*- C++ -*-===//
//
// Sema walks every module in four passes:
//
//   1. collect    - introduce every top-level name into the module scope
//   2. shapes     - resolve field, variant, superclass and mark relationships
//   3. signatures - resolve function parameter and return types
//   4. bodies     - type-check statements and expressions
//
// Splitting it this way lets declarations appear in any order, which is what
// the language promises ("no need for headers").
//
//===----------------------------------------------------------------------===//
#ifndef RUNE_SEMA_H
#define RUNE_SEMA_H

#include "rune/AST.h"
#include "rune/CxxInterop.h"
#include "rune/Diagnostics.h"
#include "rune/Driver.h"
#include "rune/Type.h"

#include <deque>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace rune {

/// Whether a value of `t` may move to another thread, and whether one may be
/// reached from several at once — `std::thread::Send` and `Sync`.
///
/// Neither is declared: both are read off the type. Sema uses it to answer a
/// bound and the code generator to answer `reflect::isSend`, so there is one
/// rule rather than two that have to be kept in step.
bool typeIsThreadSafe(Type *t, bool wantSync, std::set<Type *> &visiting);

/// The same question, with the bookkeeping supplied.
inline bool typeIsThreadSafe(Type *t, bool wantSync) {
  std::set<Type *> visiting;
  return typeIsThreadSafe(t, wantSync, visiting);
}

//===----------------------------------------------------------------------===//
// Symbol table
//===----------------------------------------------------------------------===//

enum class SymbolKind { Value, Function, TypeName, MarkName, ModuleName, Variant };

struct Symbol {
  SymbolKind Kind = SymbolKind::Value;
  std::string Name;
  Decl *D = nullptr;
  Type *Ty = nullptr;
  Module *Mod = nullptr;
  /// For enum variants reached by a bare name.
  EnumDecl *Owner = nullptr;
  int VariantIndex = -1;
  bool IsPublic = false;
};

enum class ScopeKind { Module, Function, Block, TypeBody, Loop };

class Scope {
public:
  Scope(ScopeKind k, Scope *parent) : Kind(k), Parent(parent) {}

  ScopeKind kind() const { return Kind; }
  Scope *parent() const { return Parent; }

  Symbol *findLocal(const std::string &name) {
    auto it = Symbols.find(name);
    return it == Symbols.end() ? nullptr : &it->second;
  }
  Symbol *find(const std::string &name) {
    for (Scope *s = this; s; s = s->Parent)
      if (Symbol *sym = s->findLocal(name))
        return sym;
    return nullptr;
  }
  /// Returns the previous binding if `name` was already declared here.
  Symbol *insert(Symbol sym) {
    auto it = Symbols.find(sym.Name);
    if (it != Symbols.end())
      return &it->second;
    std::string key = sym.Name;
    Symbols.emplace(std::move(key), std::move(sym));
    return nullptr;
  }
  /// Replaces any existing binding (used for shadowing in nested blocks).
  void overwrite(Symbol sym) {
    std::string key = sym.Name;
    Symbols[key] = std::move(sym);
  }

  const std::map<std::string, Symbol> &all() const { return Symbols; }

  //=== Enum variants reachable by their bare name =======================//
  //
  // A variant's bare name is a convenience, not a declaration: `Wheel` may
  // belong to `Car` and to `Ship` at once, and a `let Wheel = ...` may sit
  // beside both. So variants are held apart from the names a module declares.
  // A bare mention is looked up here only after the ordinary scope chain has
  // nothing, and finding two means the mention has to say which.
  struct VariantEntry {
    Symbol Sym;
    /// Option's and Result's variants, which every module gets for free. They
    /// lose to anything a module declares or imports, so a program with its
    /// own `Some` is not made ambiguous by the prelude's.
    bool FromPrelude = false;
  };

  void addVariant(Symbol sym, bool fromPrelude = false) {
    auto &list = Variants[sym.Name];
    for (const VariantEntry &e : list)
      if (e.Sym.D == sym.D)
        return;                       // the same variant, reached twice
    list.push_back({std::move(sym), fromPrelude});
  }
  const std::vector<VariantEntry> *variantsNamed(const std::string &n) const {
    auto it = Variants.find(n);
    return it == Variants.end() ? nullptr : &it->second;
  }
  const std::map<std::string, std::vector<VariantEntry>> &allVariants() const {
    return Variants;
  }

private:
  ScopeKind Kind;
  Scope *Parent;
  std::map<std::string, Symbol> Symbols;
  std::map<std::string, std::vector<VariantEntry>> Variants;
};

//===----------------------------------------------------------------------===//
// Sema
//===----------------------------------------------------------------------===//

/// Everything CodeGen needs that Sema discovered.
struct SemaResult {
  /// Resolved method tables, keyed by type then method name. CodeGen reads
  /// these when building a mark object's dispatch table.
  std::map<Type *, std::map<std::string, FunctionDecl *>> MethodTables;

  /// The same, but per mark: what each binding supplies for a type, after
  /// conformance has settled which version answers each requirement. A mark
  /// may be bound to one type more than once, and two marks may name a method
  /// alike, so the plain table above cannot answer for a particular mark.
  std::map<std::pair<Type *, MarkDecl *>,
           std::map<std::string, FunctionDecl *>> MarkTables;

  /// The implementation of `name` for `receiver`, or null.
  FunctionDecl *methodFor(Type *receiver, const std::string &name) const {
    // A `some` dispatches exactly as the type behind it does.
    if (receiver && receiver->isOpaque() && receiver->opaqueUnderlying())
      receiver = receiver->opaqueUnderlying();
    auto it = MethodTables.find(receiver);
    if (it == MethodTables.end())
      return nullptr;
    auto mit = it->second.find(name);
    return mit == it->second.end() ? nullptr : mit->second;
  }

  /// What `mark` supplies for `receiver.name` — which is what a `dyn Mark`
  /// dispatches to. A method the type declares itself still wins, exactly as
  /// it does for a direct call.
  FunctionDecl *markMethodFor(MarkDecl *mark, Type *receiver,
                              const std::string &name) const {
    if (receiver && receiver->isOpaque() && receiver->opaqueUnderlying())
      receiver = receiver->opaqueUnderlying();
    FunctionDecl *plain = methodFor(receiver, name);
    if (plain && !plain->Bind)
      return plain;    // the type's own, which a bind never displaces
    auto it = MarkTables.find({receiver, mark});
    if (it != MarkTables.end()) {
      auto mit = it->second.find(name);
      if (mit != it->second.end() && mit->second)
        return mit->second;
    }
    return plain;
  }

  /// Every function that must be emitted, including monomorphised clones and
  /// lifted closures, in dependency-agnostic order.
  std::vector<FunctionDecl *> Functions;
  std::vector<GlobalVarDecl *> Globals;
  /// Nominal types that need a layout, including generic instantiations.
  std::vector<NominalDecl *> Nominals;
  FunctionDecl *EntryPoint = nullptr;

  /// Modules this compilation read in order to understand its own code
  /// rather than to produce them: the standard library, and every imported
  /// `.rul`. What they declare is not this artefact's API, so CodeGen emits
  /// their code only where something here actually reaches it.
  std::set<std::string> AncillaryModules;

  /// True when `modulePath` names one of those.
  bool isAncillary(const std::string &modulePath) const {
    return AncillaryModules.count(modulePath) != 0;
  }

  /// One entry per `@decorator(...)` written on a function: the decorator to
  /// call, the attribute holding its arguments, and the function to hand it.
  /// Called once each, before `main`, in the order they were written.
  struct DecoratorCall {
    FunctionDecl *Decorator = nullptr;
    Attribute *Args = nullptr;
    FunctionDecl *Target = nullptr;
  };
  std::vector<DecoratorCall> DecoratorCalls;
};

/// Introduces `generics` as symbolic parameter types in `out`.
void bindGenerics(const std::vector<GenericParam> &generics, TypeContext &types,
                  std::map<std::string, Type *> &out);

class Sema {
public:
  /// A program with no hosted runtime, or no standard library, has no
  /// `String` to make: there a string literal nothing asks to be a `String`
  /// is a `CString`. The standard library's own sources are unaffected.
  bool CStringLiterals = false;

  Sema(const SourceManager &sm, DiagnosticEngine &diags, TypeContext &types,
       SafetyLevel safety, MemoryMode memory = MemoryMode::Arc,
       DumpKind dump = DumpKind::Nothing, bool zombieStdlib = true);
  ~Sema();

  /// Registers a parsed module so imports can find it. Ownership stays with
  /// the caller.
  void addModule(Module *m);

  /// Runs every pass over all registered modules. Returns false if any error
  /// was reported.
  bool check();

  const SemaResult &result() const { return Result; }
  TypeContext &types() { return Types; }

  /// Prints the resolved module scope (`--dump-symbols`).
  void dumpSymbols(std::ostream &os) const;

  /// How long the Zombie checker took, for `--time`.
  double zombieMillis() const { return ZombieMillis; }

  /// A type's own `clone(&self) -> Self`, or null. Static so the code
  /// generator can call it directly.
  static FunctionDecl *userClone(Type *t);

  /// What `extern "C++"` declarations are mangled and typed for. Set from the
  /// target triple before `check`; the host's conventions otherwise.
  void setCxxTarget(const CxxTarget &t) { Cxx = t; }
  const CxxTarget &cxxTarget() const { return Cxx; }
  /// True once an `extern "C++"` block has been seen: the program needs the
  /// C++ runtime linked in.
  bool usesCxx() const { return UsesCxx; }


private:
  const SourceManager &SM;
  DiagnosticEngine &Diags;
  TypeContext &Types;
  SafetyLevel Safety;
  /// `--memory`: which memory model the program is being compiled for. Under
  /// `Zombie` the queued bodies go to the borrow checker instead of the
  /// ownership pass, and the flat move tracking below stays out of its way.
  MemoryMode Memory;
  DumpKind Dump;
  bool ZombieStdlib;
  SemaResult Result;
  CxxTarget Cxx = cxxTargetFor("");
  bool UsesCxx = false;

  std::vector<Module *> Modules;
  std::map<std::string, Module *> ModulesByName;
  /// Symbols manufactured during path resolution (enum variants, static
  /// methods). A deque keeps previously handed-out pointers valid even when a
  /// nested lookup runs in the middle of an outer one.
  std::deque<Symbol> SyntheticSymbols;
  /// Nodes synthesised during checking (monomorphised clones, lifted
  /// closures); Sema owns them for the rest of the compilation.
  std::vector<std::unique_ptr<Decl>> Synthesised;
  /// Names the next lifted closure; one number per closure, whatever else
  /// was collected in between.
  unsigned ClosureCounter = 0;

  //=== Scopes ===========================================================//
  std::vector<std::unique_ptr<Scope>> ScopeStorage;
  Scope *CurScope = nullptr;
  Scope *ModuleScope = nullptr;

  Scope *pushScope(ScopeKind k);
  void popScope();

  //=== Per-function checking state ======================================//
  /// One entry per enclosing loop, so `break` can carry a value out of a
  /// `loop` and labelled breaks can find their target.
  struct LoopInfo {
    std::string Label;
    Type *BreakType = nullptr; ///< unified type of the loop's `break` values
    bool IsValueLoop = false;  ///< `loop` can produce a value; while/for cannot
    SourceRange Range;
  };

  struct FunctionContext {
    FunctionDecl *Fn = nullptr;
    ClosureExpr *Closure = nullptr;   ///< set when checking a closure body
    Type *ReturnType = nullptr;
    /// The nominal type `self` refers to, if any.
    Type *SelfType = nullptr;
    ClassDecl *SelfClass = nullptr;
    VarDecl *SelfVar = nullptr;
    bool InUnsafeContext = false;
    std::vector<LoopInfo> Loops;
  };
  /// A deque, not a vector: checking a body can instantiate a generic, which
  /// pushes another context, and `FunctionContext *` pointers held across that
  /// call must stay valid.
  std::deque<FunctionContext> FnStack;
  FunctionContext *fn() { return FnStack.empty() ? nullptr : &FnStack.back(); }

  Module *CurModule = nullptr;
  /// Exported scope for each module, used to resolve `module::member`.
  std::map<Module *, Scope *> ModuleScopes;
  /// Nesting depth at which each local was declared; a reference from a deeper
  /// depth is a closure capture.
  std::map<VarDecl *, unsigned> VarDepth;
  /// Generic parameters bound in the enclosing signature, for `Self` and for
  /// resolving bare parameter names to Generic types.
  std::map<std::string, Type *> ActiveGenericParams;
  /// Type currently standing in for `Self`.
  Type *ActiveSelfType = nullptr;
  /// The standard library declarations the language itself depends on.
  EnumDecl *OptionDecl = nullptr;
  EnumDecl *ResultDecl = nullptr;
  /// The `As` mark, which `into` dispatches through.
  MarkDecl *AsDecl = nullptr;
  /// `Iterator`, which `for` drives, and `Sequence`, which hands one over.
  /// `std::dictionary::Map`, which `[K:V]` is sugar for.
  NominalDecl *MapDecl = nullptr;
  MarkDecl *IteratorDecl = nullptr;
  MarkDecl *SequenceDecl = nullptr;
  /// Instantiation chain, so a failure inside a generic can point back at the
  /// call that triggered it.
  std::vector<std::pair<FunctionDecl *, SourceRange>> InstantiationStack;

  //=== Ownership ========================================================//
  /// Bodies waiting to be read for what they do with what they own.
  ///
  /// The pass wants nothing from Sema — it is handed one function and reads
  /// only that function's own tree — so it is the one part of checking whose
  /// units are independent of each other, and it is run over all of them at
  /// once when the rest of checking has finished. Queued in the order the
  /// bodies were checked, which is the order its diagnostics come back in.
  std::vector<FunctionDecl *> OwnershipQueue;
  std::set<FunctionDecl *> OwnershipQueued;
  /// Runs the queue, on as many threads as there are.
  void checkOwnershipOfQueued();
  double ZombieMillis = 0;

  //=== Origins and views (`from` clauses, `{ fields }`) =================//
  // Written annotations the Zombie borrow checker reads. They are resolved
  // in Sema because that is where names become declarations; the checker
  // only ever sees the resolved form. See SemaOrigins.cpp.

  /// Resolves every `from` clause and view in `fn`'s signature. Safe to call
  /// more than once; the second call does nothing.
  void resolveSignatureAnnotations(FunctionDecl *fn);
  /// Resolves `from self.…` on the fields of a struct, class or enum.
  void resolveFieldAnnotations(NominalDecl *nd);
  /// Resolves a `from` clause written on a local binding's type.
  void resolveLocalAnnotations(TypeRepr *repr);
  /// E0292 when `d` carries `@zombie_unavailable` and this is a Zombie build.
  void checkAvailableUnderZombie(Decl *d, SourceRange at);
  /// True when `$clone()` applies to values of `t`.
  bool typeIsClonable(Type *t);
  /// The context a `from` place is resolved against.
  struct OriginContext {
    const std::vector<Param> *Params = nullptr;          ///< a signature
    const FunctionTypeReprNode *FnType = nullptr;        ///< a function type
    NominalDecl *Self = nullptr;                         ///< a field: `self.…`
    /// With `Self`: the fields `self.x` is looked up in when they are not
    /// the nominal's own — an enum variant's.
    const std::vector<std::unique_ptr<FieldDecl>> *SelfFields = nullptr;
    bool Locals = false;                                 ///< a binding
  };
  void resolveOriginClauses(TypeRepr *repr, const OriginContext &ctx);
  bool resolveOriginPlace(OriginPlace &place, const OriginContext &ctx);
  /// Resolves `names[from..]` as fields starting at `root`, appending an
  /// index per step. Reports and returns false at the first name that is
  /// not a field of the type reached so far.
  bool resolveFieldPath(Type *root, const std::vector<std::string> &names,
                        size_t from, std::vector<unsigned> &out,
                        SourceRange at);
  /// The field called `name` on `t` (through borrows and up a class's
  /// superclass chain), or null.
  FieldDecl *fieldOf(Type *t, const std::string &name);

  //=== Method / mark tables =============================================//
  // Keyed by Type rather than by declaration, so a mark can be bound to a
  // builtin (`bind Display to i64`) exactly as it is to a struct. Types are
  // interned, so the key is a stable pointer.
  /// name -> method, per type (own methods, extends, binds).
  std::map<Type *, std::map<std::string, FunctionDecl *>> Methods;
  /// (type, operator) -> implementation
  /// Every overload of an operator for a type. A type may have several — one
  /// per right-hand type — so `String + String` and `String + Character` can
  /// both exist and mean different things.
  std::map<std::pair<Type *, std::string>, std::vector<FunctionDecl *>>
      Operators;
  /// Marks each type is bound to.
  std::map<Type *, std::vector<MarkDecl *>> Conformances;
  /// (type, mark) -> the methods that binding supplies, whether written out in
  /// the `bind` block or copied in from the mark's defaults. `Methods` above is
  /// what a plain `value.name()` finds; this is what `value::Mark.name()`
  /// finds, and it stays reachable even when an inherent method has the same
  /// name and wins the implicit lookup.
  std::map<std::pair<Type *, MarkDecl *>,
           std::map<std::string, FunctionDecl *>> MarkImpls;
  /// The same, but every version rather than the one that answers the mark's
  /// requirement. A mark may be bound to one type more than once, each
  /// binding taking something different, and `value::Mark.name(...)` picks
  /// between them the way a plain call does.
  std::map<std::pair<Type *, MarkDecl *>,
           std::map<std::string, std::vector<FunctionDecl *>>> MarkImplSets;
  /// Methods a type declares itself, through its own body or an `extend`. A
  /// `bind` never displaces one of these.
  std::set<std::pair<Type *, std::string>> InherentMethods;
  /// Extra operator spellings introduced with `@alias("...")`, mapping the
  /// alias to the canonical operator name it stands for.
  std::map<std::string, std::string> OperatorAliases;
  /// What each binding chose for a mark's associated types, keyed by the type
  /// it binds to and the mark: `AssocTypes[{Counter, Iterator}]["Item"]`.
  std::map<std::pair<Type *, MarkDecl *>, std::map<std::string, Type *>>
      AssocTypes;
  /// A bound on a bind's associated type, held until every module's bindings
  /// are registered. The type that answers `type Iter: Iterator` is often
  /// bound further down the same file, and declaration order should not be
  /// what decides whether the program compiles.
  struct DeferredBound {
    Type *Chosen = nullptr;
    TypeRepr *Bound = nullptr;
    SourceRange At;
    SourceRange Declared;
    std::string Owner;
  };
  std::vector<DeferredBound> DeferredBounds;
  /// How deep `instantiateNominal` currently is, so a type parameterised by
  /// itself reports rather than exhausting the stack.
  unsigned NominalDepth = 0;
  /// True once bodies are being checked. Before that, an instantiation's
  /// bodies are queued rather than checked against a half-built program.
  bool InBodyPass = false;
  std::vector<NominalDecl *> PendingInstantiations;
  /// Methods whose signature wraps `Self` in a generic argument, so resolving
  /// it eagerly would ask for a type wrapping a type wrapping a type. Both
  /// signature and body wait until a call asks for them.
  std::set<FunctionDecl *> DeferredMethods;
  std::vector<FunctionDecl *> PendingMethods;
  /// Bodies already checked, so no declaration is checked twice.
  std::set<FunctionDecl *> BodiesChecked;
  /// Cache of generic instantiations: template + argument list -> clone.
  std::map<std::string, FunctionDecl *> FunctionInstances;
  std::map<std::string, NominalDecl *> NominalInstances;
  /// Binds already prepared, so the work happens once whether the shapes pass
  /// reached the bind first or an instantiation asked for it.
  std::set<BindDecl *> PreparedBinds;

  //=== Passes ===========================================================//
  void collectModule(Module *m);
  void resolveShapes(Module *m);
  /// Resolves a `bind` far enough to be applied: its target, its mark, what
  /// it chose for the mark's associated types, and the mark's defaults copied
  /// in beside the methods it wrote. Done once, by whoever needs it first.
  void prepareBind(Module *m, BindDecl *b);
  void resolveSignatures(Module *m);
  void checkBodies(Module *m);
  void resolveImports(Module *m);

  void collectDecl(Decl *d, Scope *scope);
  /// The names `@alias("...")` adds to a declaration, with the decorator's
  /// range for diagnostics.
  std::vector<std::pair<std::string, SourceRange>> aliasesOf(Decl *d);
  void registerMethods(NominalDecl *nd);
  /// Evaluates the variants' declared values: the tags, and a float-valued
  /// enum's raw values.
  void assignVariantValues(EnumDecl *e);
  /// Gives a `weak` field its Option type and checks that it refers to a class.
  void applyWeakField(FieldDecl *f);
  /// Finds `std::option::Option` and `std::result::Result` once every module
  /// has been collected, and marks them so the type system can recognise them.
  void findLangItems();
  /// `Option<element>`, instantiating on demand. Reports if the standard
  /// library is missing.
  Type *optionOf(Type *element, SourceRange range);

  //=== Types ============================================================//
  Type *resolveType(TypeRepr *repr);
  Type *resolveTypeOrError(TypeRepr *repr, Type *fallback);
  /// Resolves `fn`'s written result type — `()` when it has none. This is
  /// the one place `some Mark` is legal, so it is where the annotation is
  /// told which function it belongs to. `selfType` is what `Self` means for
  /// a method, and is kept so the body can be checked on demand later.
  Type *resolveReturnType(FunctionDecl *fn, Type *selfType);

  //=== `some Mark` ======================================================//
  //
  // A function declared `-> some Mark` returns a concrete type its body
  // chooses and its callers never see. The type exists from the signature
  // pass on, so callers can be checked against it; what it is behind the
  // veil is fixed by the first value the body returns. A caller checked
  // before the body has been asks for it, and the body is checked then.
  //
  /// What the checker needs to check an opaque type's owner on demand.
  struct OpaqueContext {
    FunctionDecl *Fn = nullptr;
    Type *SelfType = nullptr;
    std::map<std::string, Type *> Generics;
    SourceRange Declared;
    /// True while the owner's body is being checked, so a body that reaches
    /// its own result type is reported rather than followed round again.
    bool Resolving = false;
  };
  std::map<Type *, OpaqueContext> OpaqueContexts;
  /// The result annotation currently being resolved, and whose it is. Only
  /// a `some` at exactly this node is a function's result type.
  TypeRepr *ResolvingReturnRepr = nullptr;
  FunctionDecl *ResolvingReturnFn = nullptr;
  /// Checks the owner's body if `t` is a `some` whose type is still unknown,
  /// so that anything asked of it afterwards has an answer.
  void resolveOpaqueNow(Type *t, SourceRange at);
  /// The type behind `t` for a `some`, resolving it first if need be, and
  /// `t` itself otherwise.
  Type *seeThroughOpaque(Type *t, SourceRange at);
  /// Reports `what` as unavailable on a `some` and returns true, or returns
  /// false when `t` is not one.
  bool rejectOpaqueUse(Type *t, SourceRange at, const std::string &what);
  /// `Queue<String>::new(...)`: the arguments belong to the type, so the
  /// type is instantiated and the method looked up on the instantiation.
  /// Null when the path does not name one, leaving the ordinary lookup to it.
  Symbol *lookupStaticOnInstantiation(DeclRefExpr *ref);
  /// `.Red` / `::seconds(5)`: the member named on whatever type the context
  /// is expecting. Null when there is no expected type, or it has no such
  /// member.
  /// Type expressions built while deciding whether `[X:Y]` is a map. They
  /// are resolved, so something points at them; this owns them.
  std::vector<TypeReprPtr> SpeculativeTypeReprs;
  /// `struct Derived : Base` — puts the parent's fields (or variants) at the
  /// front of the child's, once, before anything reads them.
  void spliceInheritance(NominalDecl *nd);
  /// The type an array length names, when it names one: `[String:i64]` is a
  /// map, `[3:i64]` an array.
  Type *typeWrittenAsSize(Expr *size);
  /// `Map<K, V>`, the type `[K:V]` is sugar for.
  Type *mapOf(Type *key, Type *value, SourceRange range);
  /// The variant of `expected` called `name`, if it has one.
  Symbol *variantOfExpected(const std::string &name, Type *expected);
  /// True when a set of type-parameter bindings makes a call work — every
  /// parameter answered, every argument convertible, the result convertible
  /// to where it is going.
  bool fitsCall(FunctionDecl *f, const std::vector<Type *> &formals,
                const std::vector<Type *> &argTypes,
                const std::map<std::string, Type *> &bindings, Type *expected);
  Symbol *resolveInferredPath(DeclRefExpr *ref, Type *expected);
  /// The message for a `.name` that could not be resolved.
  Type *reportInferredPathFailure(DeclRefExpr *ref, Type *expected);
  NominalDecl *lookupNominal(const std::vector<std::string> &path,
                             SourceRange range, bool quiet = false);
  Type *typeOfNominal(NominalDecl *nd, const std::vector<TypeReprPtr> &args,
                      SourceRange range);

  //=== Declarations =====================================================//
  void checkFunction(FunctionDecl *fn, Type *selfType, ClassDecl *selfClass);
  /// Refuses the parameters an `async fn` cannot take: borrows that would
  /// outlive the call. See the definition for the rule.
  void checkAsyncSignature(FunctionDecl *fn);
  /// A closure argument to a `@sendable` function: written at the call, and
  /// capturing only `Send` values.
  void checkSendableClosure(Expr *arg, const std::string &calleeName);
  void checkNominalBodies(NominalDecl *nd);
  /// Type-checks `field: T = <default>` initialisers, which are evaluated at
  /// each construction site rather than once.
  void checkFieldDefaults(NominalDecl *nd);
  void checkForeignSignature(FunctionDecl *fn);
  void checkReflectionCall(CallExpr *c, FunctionDecl *fn);
  Type *asCFunctionIfWanted(FunctionDecl *fn, Type *expected, SourceRange at);
  std::string whyNotThreadSafe(Type *t);
  MarkDecl *SendDecl = nullptr;
  MarkDecl *SyncDecl = nullptr;
  bool checkWhereClauses(const std::vector<WhereClause> &clauses,
                         SourceRange at, const std::string &owner);
  /// Whether a method of an instantiated generic type is there for this
  /// instantiation: its own `where` clauses, asked of the type's arguments.
  /// `Vector<T>::at` is `where T: Clone`, so `Vector<Box<i64>>` has no `at`.
  /// Quiet unless `report`, when the unmet bound is reported at `at`.
  bool methodWhereHolds(FunctionDecl *fn, SourceRange at, bool report);
  std::map<FunctionDecl *, bool> MethodWhereAnswers;
  void checkBind(BindDecl *b);
  void checkExtend(ExtendDecl *e);
  /// A mark requirement's type as the implementing type reads it: every
  /// `Self` replaced by `target`, at any depth.
  Type *readSelfAs(Type *t, Type *markTy, Type *target, SourceRange at);
  void checkGlobal(GlobalVarDecl *g);
  void layoutClass(ClassDecl *c);

  //=== Statements =======================================================//
  void checkStmt(Stmt *s);
  void checkVarStmt(VarStmtNode *v);

  //=== Expressions ======================================================//
  /// Type-checks `e`. `expected` steers literal typing and may be null.
  Type *checkExpr(Expr *e, Type *expected = nullptr);
  Type *checkBlock(BlockExpr *b, Type *expected);
  Type *checkCall(CallExpr *c, Type *expected);
  Type *checkMember(MemberExpr *m, Type *expected, bool forCall);
  Type *checkBinary(BinaryExpr *b, Type *expected);
  Type *checkUnary(UnaryExpr *u, Type *expected);
  Type *checkAssign(AssignExpr *a);
  Type *checkIndex(IndexExpr *i, Type *expected);
  Type *checkIf(IfExpr *i, Type *expected, bool discardBranches);
  Type *checkMatch(MatchExpr *m, Type *expected, bool discardBranches);
  Type *checkFor(ForExpr *f);
  Type *checkClosure(ClosureExpr *c, Type *expected);
  Type *checkStructLit(StructLitExpr *s, Type *expected);
  Type *checkArrayLit(ArrayLitExpr *a, Type *expected);
  Type *checkDeclRef(DeclRefExpr *r, Type *expected);
  Type *checkCast(CastExpr *c);
  /// `if any is T` parses as a pattern, because that is what `is` means
  /// everywhere else. An `Any` has no pattern worth matching, so the name is
  /// read as a type and the condition becomes a dynamic type test. Returns
  /// true when it rewrote `cond`.
  bool rewriteAnyTypeTest(ExprPtr &cond, PatternPtr &pat, Type *condType);
  Type *checkInto(IntoExpr *e);
  Type *checkMove(MoveExpr *m);

  /// True while checking an expression whose value nobody wants — a statement
  /// on its own, or the tail of one. A branching expression in that position
  /// does not have to make its arms agree, because there is no one type for
  /// them to agree *on*. Consumed by `checkExpr`, which clears it before
  /// descending, so it never leaks into a subexpression that is used.
  bool ValueDiscarded = false;

  //=== Unique ownership =================================================//
  /// Reports an attempt to copy a `uniq` reference. `what` names the position
  /// the value was flowing into, e.g. "this binding". Returns true when it
  /// reported, so callers can stop.
  bool rejectUniqCopy(Expr *e, Type *t, const char *what);
  /// True when `e` is a fresh class construction, the one expression that may
  /// become a `uniq` without a move because nothing else refers to it yet.
  bool isFreshConstruction(const Expr *e) const;
  /// Locals that have been moved out of, and the move that did it.
  std::map<VarDecl *, Expr *> MovedFrom;
  /// True while checking the body of a generic that was instantiated. A `T`
  /// that happens to be `uniq` there was not written as one, so the
  /// single-owner rule is not the generic author's to satisfy; `uniq` is
  /// instead refused as an explicit type argument, which leaves `Option` —
  /// the compiler's own — as the only generic that can hold one.
  bool inGenericBody() const;
  /// Records that `e` gave its value away, so the name it came from stops
  /// referring to anything. Does nothing for an expression that names no
  /// local, which is already the case for a temporary.
  void markMoved(Expr *e);
  Type *checkTry(TryExpr *t);
  /// The `convert` that takes an error of `from` to one of `to`, when a
  /// `bind As<to> to from` exists. Null otherwise; reports nothing.
  FunctionDecl *lookupErrorConversion(Type *from, Type *to, SourceRange at);

  //=== Patterns =========================================================//
  void checkPattern(Pattern *p, Type *scrutinee, bool declaresBindings,
                    bool isMutable);
  bool patternIsIrrefutable(const Pattern *p) const;
  /// Resolves a `for` over something that is not a range, array or slice:
  /// an `Iterator` driven directly, or a `Sequence` asked for one.
  Type *resolveIteration(ForExpr *f, Type *seq);

  //=== Helpers ==========================================================//
  /// Resolves `a::b::c`, walking module scopes. Reports nothing when `quiet`.
  Symbol *lookupPath(const std::vector<std::string> &path, SourceRange range,
                     bool quiet);
  /// The enum variants a bare `name` could mean here. Anything a module
  /// declares or imports hides the prelude's, so the result is either every
  /// candidate from the program or — when there are none — Option's and
  /// Result's. More than one entry means the mention is ambiguous.
  std::vector<Symbol> bareVariants(const std::string &name);
  /// Reports `name` as ambiguous, listing the enums that claim it.
  void reportAmbiguousVariant(const std::string &name, SourceRange range,
                              const std::vector<Symbol> &candidates);
  Scope *scopeForModule(Module *m);
  std::string mangleFunction(const FunctionDecl *fn,
                             const std::vector<Type *> &typeArgs);
  //=== extern "C++" =======================================================//
  /// The Itanium symbol for an `extern "C++"` function, reporting anything in
  /// its signature that has no C++ spelling.
  std::string mangleCxx(const FunctionDecl *fn);
  /// Binds a generic type's parameters to the arguments an instantiation was
  /// made with, under every name they answer to — the type's own, and any an
  /// `extend` block introduced for them.
  static void bindTemplateGenerics(const NominalDecl *tmpl,
                                   const std::vector<Type *> &args,
                                   std::map<std::string, Type *> &out);
  /// Folds an `extend` on a generic type into that type's declaration, so its
  /// methods are instantiated with it. Returns false when the target is not a
  /// generic template, leaving the ordinary path to handle it.
  bool foldGenericExtend(ExtendDecl *e);
  /// Resolves `class D : B` and checks what an `extern "C++"` type declares.
  void resolveCxxType(NominalDecl *nd);
  /// The base chain of a C++ class, nearest first, or empty.
  static std::vector<NominalDecl *> cxxBasesOf(NominalDecl *nd);
  /// True when a pointer to `from` may be handed to a C++ parameter of type
  /// `to`: the same pointee, or a derived class where a base is wanted, and
  /// a raw pointer where C++ wrote a reference.
  bool cxxPointerConvertible(Type *from, Type *to);
  /// Reports a C++ `class` used by value, which Rune never holds: only a
  /// pointer or a borrow to one.
  void rejectCxxClassByValue(Type *t, SourceRange where, const char *role);
  /// A bare mark where a value's type was wanted. `dyn Mark` or a bound is
  /// what was meant, and saying so here keeps the mistake from surfacing far
  /// away, inside whatever generic was asked to hold it.
  void rejectMarkByValue(Type *t, SourceRange where, const char *role);
  /// Reorders labelled arguments, fills defaults and checks each against its
  /// parameter type. `paramTypes` excludes `self`.
  bool matchCallArguments(CallExpr *c, const std::vector<Param> &params,
                          const std::vector<Type *> &paramTypes, bool variadic,
                          const std::string &calleeName, SourceRange range,
                          const Decl *calleeDecl);
  /// Records `fn` as one of the implementations `mark` supplies for `target`.
  void addMarkImpl(Type *target, MarkDecl *mark, const std::string &name,
                   FunctionDecl *fn);
  /// Every implementation `mark` supplies for `receiver.name`.
  std::vector<FunctionDecl *> markImplSet(Type *receiver, MarkDecl *mark,
                                          const std::string &name);
  /// The implementation that answers `req` when a mark is bound to one type
  /// by more than one `bind`. Falls back to `fallback` when only one exists.
  FunctionDecl *conformingImpl(Type *target, MarkDecl *mark, FunctionDecl *req,
                               FunctionDecl *fallback);
  /// Verifies a `bind Mark to T` provides every requirement.
  void checkMarkConformance(BindDecl *b);
  /// Unifies two branch types, reporting a mismatch against `context`.
  Type *unifyBranches(Type *a, Type *b, Expr *second, const char *context);
  /// Resolves a generic template's signature with its parameters standing in
  /// as Generic types, so call sites can unify against it.
  void ensureTemplateSignature(FunctionDecl *fn);
  /// Binds the parameters of the type a method belongs to. A generic method on
  /// an instantiated type needs both: the type's `T` and its own `U`.
  void bindOwnerGenerics(const FunctionDecl *fn,
                         std::map<std::string, Type *> &out);
  /// The type a `self` parameter takes for a method on `base`. Aggregates and
  /// nominal types are borrowed; scalars and already-pointer-like types are
  /// passed by value, so `bind Display to i64` gets an `i64` rather than an
  /// `&i64` that no arithmetic accepts.
  Type *selfTypeFor(const Param &p, Type *base);
  /// Resolves a generic enum template's variant payload types with its own
  /// parameters standing in, so a call site can unify against them.
  void ensureVariantPayloadTypes(EnumDecl *e);
  /// Picks the instantiation a variant reference names. `argTypes` are the
  /// constructor arguments (empty for a unit variant) and `expected` is the
  /// contextual type; either may supply the missing parameters.
  EnumDecl *instantiateVariantOwner(EnumDecl *tmpl, unsigned variantIndex,
                                    const std::vector<TypeReprPtr> &explicitArgs,
                                    const std::vector<Type *> &argTypes,
                                    Type *expected, SourceRange range);
  /// Warns when a match cannot be shown to cover every case.
  void checkExhaustive(MatchExpr *m, Type *scrutinee);
  /// Numeric/branch-style promotion of two operand types.
  Type *promote(Type *a, Type *b);
  /// Folds an integer constant expression. Handles literals, the usual
  /// arithmetic, casts, and references to immutable globals whose initialiser
  /// is itself constant. Returns false when the expression is not constant.
  bool evalConstInt(const Expr *e, int64_t &out);
  /// Reports a mismatch unless one side is already an error type.
  void requireConvertible(Expr *e, Type *from, Type *to, const char *context);
  /// A value that does not fit where it is going, but whose type says how to
  /// get there. `bind Celsius into Fahrenheit` makes the conversion available
  /// wherever the destination is written down — an argument, an annotated
  /// binding, a field of a struct literal, a declared result — and this is
  /// where the call is put in. True when it rewrote `slot`.
  bool insertImplicitConversion(ExprPtr &slot, Type *from, Type *to);
  bool isLValue(const Expr *e) const;
  /// Emits "cannot assign to ..." with a related note at the binding site.
  bool requireMutable(Expr *e, const char *action);
  /// Whether what a `match`, `if ... is` or `while ... is` looks at may be
  /// written: a `&var` borrow, a `var` binding, `&var self`, or a temporary
  /// the match owns. Its borrowing bindings may then be lent out mutably.
  bool scrutineeWritable(const Expr *e);
  /// Warns about two variants of `e` with one value (W0402).
  void warnDuplicateVariantValues(EnumDecl *e);
  /// Set around checking the pattern of a `match`, `if ... is` or
  /// `while ... is`, whose bindings borrow unless they say `take`.
  bool PatternBorrows = false;
  bool PatternScrutineeWritable = false;
  bool PatternInGuardedArm = false;
  struct PatternContext {
    Sema &S;
    bool Borrows, Writable, Guarded;
    PatternContext(Sema &s, bool writable, bool guarded)
        : S(s), Borrows(s.PatternBorrows), Writable(s.PatternScrutineeWritable),
          Guarded(s.PatternInGuardedArm) {
      S.PatternBorrows = true;
      S.PatternScrutineeWritable = writable;
      S.PatternInGuardedArm = guarded;
    }
    ~PatternContext() {
      S.PatternBorrows = Borrows;
      S.PatternScrutineeWritable = Writable;
      S.PatternInGuardedArm = Guarded;
    }
  };
  VarDecl *declareLocal(const std::string &name, Type *ty, bool isMutable,
                        SourceRange range, bool isParam = false);
  /// Records a use of `name` in the innermost closure, if it comes from an
  /// enclosing function.
  void noteCapture(VarDecl *v, SourceRange range);

  FunctionDecl *lookupMethod(Type *receiver, const std::string &name,
                             NominalDecl **ownerOut = nullptr);
  /// `look` or `touch` on `receiver`, when it has one with the shape a
  /// stand-in needs: no arguments, and a borrow of something else as the
  /// result. Null when the type is not a stand-in for anything.
  FunctionDecl *pointeeAccessor(Type *receiver, bool wantMutable);
  /// `h.field` where `h` is a `Handle`, a `Box`, an `Rc` — anything that
  /// lends what it holds. Rewrites `m`'s base into the borrow and returns the
  /// member's type, or null when the base is not a stand-in.
  Type *reachThroughPointee(MemberExpr *m, Type *expected, bool forCall);
  /// True when `e` is on the path from an assignment's left-hand side down to
  /// its root binding — the places a write actually passes through, as
  /// against an index or an argument evaluated along the way.
  bool onWriteSpine(const Expr *e) const;
  /// The left-hand side currently being checked, for `onWriteSpine`.
  const Expr *WriteSpine = nullptr;
  /// How many stand-ins deep the current member access has reached, so a
  /// type that lends itself cannot loop.
  unsigned PointeeDepth = 0;
  /// The implementation of `name` that `mark` supplies for `receiver`,
  /// searching the mark's super-marks too. Null when the type is not bound to
  /// it, or the mark has no such member.
  FunctionDecl *lookupMarkMethod(Type *receiver, MarkDecl *mark,
                                 const std::string &name);
  /// Checks that `impl` has the signature `req` asks for, with `Self` read as
  /// `target`. Reports on a mismatch.
  void checkRequirementSignature(FunctionDecl *req, FunctionDecl *impl,
                                 MarkDecl *mark, Type *target);
  /// The sole overload, when there is one. For a binary operator prefer the
  /// three-argument form, which picks by the right-hand type.
  FunctionDecl *lookupOperator(Type *receiver, const std::string &op);
  /// Every implementation of `op` on `receiver`, in declaration order. An
  /// operator may be written once per operand type, so there is rarely only
  /// one: `text + text` and `text + 'a'` are two.
  std::vector<FunctionDecl *> operatorOverloads(Type *receiver,
                                                const std::string &op);
  /// The overload of `op` on `receiver` that accepts `rhs`, or null.
  FunctionDecl *lookupOperator(Type *receiver, const std::string &op,
                               Type *rhs);
  /// True when the language already gives `op` a meaning for this pair, which
  /// is what an overload may never replace.
  bool builtinOperatorApplies(const std::string &op, Type *lhs, Type *rhs);
  /// True when `==` means something for two `t`s, reading through
  /// `Option`s to the payload. `eq` receives the binding the innermost
  /// payload compares with, or stays null when the comparison is builtin.
  bool equalityDefined(Type *t, FunctionDecl *&eq);
  void checkConventions();
  bool typeConformsTo(Type *t, MarkDecl *mark);
  //=== Automatic marks ===================================================//
  /// Validates `@auto` on a mark and records it.
  void checkAutoMark(MarkDecl *mk);
  /// The automatic marks `@never(...)` refuses for this type.
  const std::vector<MarkDecl *> &refusedMarks(NominalDecl *nd);
  /// Whether `t` has the automatic mark `mark`, structurally.
  bool typeHasAutoMark(Type *t, MarkDecl *mark, std::set<Type *> &seen);
  /// A conditional `bind` of an automatic mark to `t`'s template whose
  /// bounds hold, asked with the same `seen` so a cycle assumes the best.
  bool conditionalAutoBindHolds(Type *t, MarkDecl *mark,
                                std::set<Type *> &seen);
  /// Every bind written against a generic template, and the module it was
  /// written in, as instantiation found them.
  std::map<NominalDecl *, std::vector<std::pair<Module *, BindDecl *>>>
      TemplateBinds;
  /// Every module's binds are registered: a conditional bind turned down
  /// from here on is turned down for good.
  bool ShapesDone = false;
  /// `drainPendingInstantiations` is running; a nested call leaves the
  /// queue to it.
  bool DrainingInstantiations = false;
  /// Which binds have been applied to which types, so none is applied twice.
  std::set<std::pair<Type *, BindDecl *>> RegisteredBinds;
  struct DeferredConditionalBind {
    NominalDecl *Inst;
    Module *M;
    BindDecl *B;
    std::map<std::string, Type *> Args;
  };
  std::vector<DeferredConditionalBind> DeferredConditionalBinds;
  void resolveSuppliedSignatures(NominalDecl *inst);
  Type *indexResult(IndexExpr *i, FunctionDecl *impl);
  unsigned ForItemCounter = 0;
  /// Which part of `t` is the reason it does not have `mark`.
  std::string whyNotAutoMark(Type *t, MarkDecl *mark);
  //=== Copying ===========================================================//
  /// The part of `t` whose `deinit` a memberwise copy would duplicate.
  NominalDecl *cloneDuplicatesObligation(Type *t, std::set<Type *> &seen);
  /// Reports `value.$clone()` on a type the compiler must not copy.
  void checkClonable(Type *t, SourceRange at);
  std::map<NominalDecl *, std::vector<MarkDecl *>> RefusedMarks;

  //=== Specialisation ===================================================//
  // More than one `bind` can apply to one type: a general
  // `bind<T> Show to Wrapper<T>` and a `bind Show to Wrapper<i64>` beside it.
  // The more specific one wins, rather than whichever happened to be applied
  // last, and two equally specific ones are reported rather than guessed at.
  //
  /// How specific `b` is as an implementation for `target`. Larger wins.
  int bindSpecificity(const BindDecl *b) const;
  /// Gives `name` on `target` to `fn`, unless something more specific — a
  /// method the type declares itself, or a narrower bind — already has it.
  /// Reports when two equally specific bindings both claim it.
  void claimMethodSlot(Type *target, const std::string &name, FunctionDecl *fn,
                       const BindDecl *from, int score,
                       std::map<std::string, FunctionDecl *> &table);
  /// Which binding holds each method slot, and how specific it was.
  std::map<std::pair<Type *, std::string>,
           std::pair<int, const BindDecl *>> MethodClaims;

  //=== Overloaded bind methods ==========================================//
  // A method a `bind` supplies may exist more than once for one type, so long
  // as the versions differ in what they take. `bind operator::"[]" to Value`
  // written twice — once taking a `String`, once an `int` — is two ways to
  // index one type, and the call site picks by what it hands over. This is
  // the only place in the language where one name has several bodies: a
  // plain `fn` may not be declared twice.
  //
  /// Every implementation of one (type, name) slot, in the order they were
  /// claimed. `Methods` holds the first, which is what answers when there is
  /// nothing to choose between.
  std::map<std::pair<Type *, std::string>, std::vector<FunctionDecl *>>
      MethodOverloads;
  /// Every overload registered against `target`, whatever it is called.
  ///
  /// The table is keyed by `(type, name)` and ordered, so one type's slots sit
  /// together in it and are found by seeking to the first rather than by
  /// looking at all of them. That matters: this is asked once per generic
  /// instantiation, and the table grows with every instantiation, so a walk
  /// over the whole of it costs the square of how much generic code a program
  /// uses.
  void appendOverloadsFor(Type *target, std::vector<FunctionDecl *> &out) const {
    for (auto it = MethodOverloads.lower_bound({target, std::string()});
         it != MethodOverloads.end() && it->first.first == target; ++it)
      out.insert(out.end(), it->second.begin(), it->second.end());
  }
  /// Records `fn` as an implementation of `target.name`. Replaces an earlier
  /// candidate from the same binding with the same parameters, so registering
  /// a binding twice does not make it look ambiguous.
  void addOverload(Type *target, const std::string &name, FunctionDecl *fn,
                   const BindDecl *from);
  /// Drops candidates a narrower binding of the same mark has displaced.
  void dropWiderOverloads(Type *target, const std::string &name,
                          const BindDecl *from, int score);
  /// Every candidate for `receiver.name(...)`, including the superclass chain.
  /// Empty when the name is not overloaded, so callers keep their fast path.
  std::vector<FunctionDecl *> lookupOverloads(Type *receiver,
                                              const std::string &name);
  /// How well one candidate accepts `args`; -1 when it cannot at all.
  /// `labels`, when given, runs alongside `args` and carries each written
  /// argument label — empty for a positional one.
  int overloadScore(FunctionDecl *fn, const std::vector<Type *> &args,
                    const std::vector<std::string> *labels = nullptr);
  /// The candidate whose parameters best accept `args`. An exact type beats a
  /// conversion, and a candidate that needs neither wins outright. Null when
  /// nothing fits, or when two fit equally well — `ambiguous` says which.
  FunctionDecl *pickOverload(const std::vector<FunctionDecl *> &candidates,
                             const std::vector<Type *> &args, bool &ambiguous,
                             const std::vector<std::string> *labels = nullptr);
  /// Reports "no overload of 'name' takes ..." with each candidate listed.
  void reportNoOverload(const std::vector<FunctionDecl *> &candidates,
                        const std::vector<Type *> &args,
                        const std::string &name, SourceRange at,
                        bool ambiguous);
  /// True when two implementations take the same things, which is what makes
  /// them rivals rather than overloads.
  bool sameParameterList(FunctionDecl *a, FunctionDecl *b);
  /// Two bindings that claimed one slot at the same specificity. Whether that
  /// is a clash or an overload is a question about their parameter types, and
  /// those are not resolved until the signature pass, so the answer waits.
  struct SlotClash {
    Type *Target = nullptr;
    std::string Name;
    const BindDecl *Held = nullptr;
    const BindDecl *Claimed = nullptr;
    FunctionDecl *HeldFn = nullptr;
    FunctionDecl *ClaimedFn = nullptr;
  };
  std::vector<SlotClash> SlotClashes;
  /// Decides every deferred clash, once signatures are known.
  void reportSlotClashes();
  /// True when both bindings supply the same slot for the same reason — the
  /// same mark, or the same operator — which is what puts them in
  /// competition. Two marks that happen to name a method alike are not.
  static bool sameBindFamily(const BindDecl *a, const BindDecl *b);
  /// Matches one of a bind's target arguments — which may itself be built out
  /// of the bind's parameters — against what the instantiation really has.
  bool unifyGenericArg(Type *pattern, Type *concrete,
                       std::map<std::string, Type *> &out);

  //=== Generic binds ====================================================//
  /// True when `b`'s bounds and `where` clauses hold for the arguments its
  /// parameters were matched to. A conditional bind that does not apply is
  /// silent: this decides, it never reports.
  bool bindApplies(Module *bindModule, BindDecl *b,
                   const std::map<std::string, Type *> &bindArgs);
  /// Registers `b`'s methods, conformance and associated types against
  /// `target`, with `bindArgs` standing in for the bind's own parameters.
  /// `parent` is the declaration the methods belong to, or null when the
  /// target has none — which is every structural type.
  void registerBindFor(BindDecl *b, Type *target, Decl *parent,
                       const std::map<std::string, Type *> &bindArgs);

  //=== Generic binds on structural types ================================//
  // A `bind<T> Display to [T]` names a shape rather than a declaration, so
  // there is no instantiation to hang it off. It is applied the first time
  // something asks what a structural type can do.
  //
  /// Applies every generic bind whose target is a shape — `[T]`, `[N:T]`,
  /// `(A, B)`, `&T`, `@function(A) -> B` — to `t`. Runs once per type.
  void ensureStructuralBinds(Type *t);
  /// Matches `repr`, written with `params` standing for a bind's own type
  /// parameters, against the concrete `t`, recording what each parameter has
  /// to be. False when the shapes do not line up.
  bool unifyTargetRepr(Module *bindModule, TypeRepr *repr, Type *t,
                       const std::set<std::string> &params,
                       std::map<std::string, Type *> &out);
  /// Structural types whose binds have been applied, so the walk over every
  /// bind happens once per type rather than once per question about it.
  std::set<Type *> StructuralBindsApplied;
  /// One method a structural bind supplied, and what it needs in scope: the
  /// target it was registered for, and what the bind's parameters stand for.
  /// There is no instantiation pass to come back for these, so they are
  /// checked from here.
  struct StructuralBody {
    FunctionDecl *Fn = nullptr;
    Type *Target = nullptr;
    std::map<std::string, Type *> Args;
  };
  std::vector<StructuralBody> PendingStructuralBodies;
  void checkStructuralBody(const StructuralBody &body);
  void drainStructuralBodies();

  //=== Destructors on value types ======================================//
  // A struct or an enum runs a `deinit` for the same reason a class does:
  // something it owns is not a reference the compiler can count — a file
  // descriptor, a lock, a block of memory. Where a class's runs when the last
  // reference goes, a value's runs when the value itself is destroyed.
  //
  /// Finds each type's `deinit`, wherever it was written, and checks that its
  /// signature is one the compiler can call on the way out of a scope.
  void resolveDeinitialisers();
  void checkDeinitSignature(NominalDecl *nd, FunctionDecl *fn);
  /// Checks `@resource` fields: each has to be mentioned in the type's
  /// `deinit`, because a field marked that way holds something the compiler
  /// cannot release on its own.
  void checkResourceFields(NominalDecl *nd);
  /// Types whose `@resource` fields have already been reported on, so the
  /// second pass over instantiations does not say it all again.
  std::set<NominalDecl *> ResourcesChecked;
  /// True when `t` is a struct or enum that runs a `deinit`, directly or
  /// through something it contains.
  bool typeOwnsResources(Type *t, std::set<Type *> &seen);
  bool typeOwnsResources(Type *t) {
    std::set<Type *> seen;
    return typeOwnsResources(t, seen);
  }
  /// Records that `e` handed an owning value away, so the local it names no
  /// longer holds one. Returns true when it was a move.
  bool markOwnedMove(Expr *e, Type *from, Type *to);
  /// The move-marking half of the above, following a value back through the
  /// arms of a `match`, the branches of an `if` and the tail of a block to
  /// the bindings it could actually have come from.
  bool markMovesInResultPosition(Expr *e);

  //=== Strong-cycle checking ===========================================//
  // A class that can reach itself through strong fields can be made into a
  // cycle, and reference counting never frees one. At `--safety full` that is
  // refused outright; below it, it is a warning.
  void checkStrongCycles();
  /// Resolves `@name(...)` on a function to a user-defined decorator: a
  /// function whose last parameter is a function type. Reports when a
  /// decorator is unknown, mis-shaped, or given the wrong arguments.
  void checkRuntimeHook(FunctionDecl *fn, const Attribute &a);
  std::map<std::string, FunctionDecl *> RuntimeHooks;
  void checkDecorators(FunctionDecl *fn);
  /// Records `@Doc("...")` on any declaration into its `Doc` field.
  void collectDoc(Decl *d);
  /// True for a decorator the compiler handles itself.
  static bool isBuiltinDecorator(const std::string &name);
  /// Reports a user decorator written on a method, which is not supported.
  void rejectMethodDecorators(FunctionDecl *fn);
  /// Every class `t` owns a strong reference to, directly or through a
  /// struct, tuple, array, enum payload or Option.
  void collectStrongEdges(Type *t, std::vector<ClassDecl *> &out,
                          std::set<Type *> &seen);
  /// Walks from `start`, returning the first path back to it, or empty.
  bool findCycle(ClassDecl *start, ClassDecl *at,
                 std::set<ClassDecl *> &visiting,
                 std::vector<ClassDecl *> &path);
  /// The type `owner` chose for `mark`'s associated type `name`, searching
  /// super-marks. Null when there is none.
  Type *associatedTypeFor(Type *owner, MarkDecl *mark,
                          const std::string &name);
  /// Checks one bound on a generic argument — a mark, or `operator::name`.
  /// Reports and returns false when it does not hold.
  bool checkGenericBound(Type *arg, TypeRepr *bound, SourceRange at,
                         SourceRange declaredAt, const std::string &owner);
  BuiltinMethod lookupBuiltinMethod(Type *receiver, const std::string &name,
                                    std::vector<Type *> &params, Type *&result);

  /// Instantiates a generic function for `args`, cloning and re-checking it.
  FunctionDecl *instantiate(FunctionDecl *tmpl, const std::vector<Type *> &args,
                            SourceRange range);
  bool signatureWrapsSelf(const FunctionDecl *fn);
  void resolveMethodSignature(FunctionDecl *fn, Type *selfType,
                              const std::vector<Type *> &typeArgs);
  void checkDeferredMethod(FunctionDecl *fn);
  void drainPendingMethods();
  void checkInstantiatedBodies(NominalDecl *inst);
  void drainPendingInstantiations();
  NominalDecl *instantiateNominal(NominalDecl *tmpl,
                                  const std::vector<Type *> &args,
                                  SourceRange range);

  /// Reports use of an unsafe operation unless the current function opted in.
  void reportUnsafe(SourceRange range, const std::string &what,
                    const std::string &suggestion);

  /// The canonical operator name for `bind operator::X`, normalising the
  /// bracket spellings (`LeftSquareBracket` -> `index`).
  std::string canonicalOperatorName(const std::string &raw);

  void noteDeclaredAt(DiagBuilder &b, const Decl *d, const std::string &label,
                      const std::string &hint);
};

} // namespace rune

#endif
