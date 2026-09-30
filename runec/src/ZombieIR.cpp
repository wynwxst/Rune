//===- ZombieIR.cpp - Lowering a body for the borrow checker ----*- C++ -*-===//
//
// One walk over a type-checked function body, producing the control-flow
// graph of places and accesses described in ZombieIR.h. Sema has already
// decided everything that needs a name table — which declaration a name is,
// which method a call reaches, how many implicit dereferences a field access
// takes — so this walk consults no table: it reads what Sema wrote on the
// nodes and says what each node does to which place.
//
// Values live in *places*. A place expression (`v`, `v.f`, `r.x`, `*p`,
// `a[i]`) lowers to a `PlaceId` and nothing is emitted for reading it; the
// consumer decides whether it is read, moved or borrowed. Anything else — a
// call, a literal, an arithmetic result — is assigned to a temporary of its
// own, which is then a place like any other. Temporaries die at the end of
// the statement that made them, as the code generator's do.
//
//===----------------------------------------------------------------------===//
#include "rune/Zombie.h"
#include "rune/ZombieIR.h"

#include <algorithm>
#include <cassert>
#include <functional>
#include <set>

namespace rune {
namespace zombie {

//===----------------------------------------------------------------------===//
// PlaceTable
//===----------------------------------------------------------------------===//

PlaceId PlaceTable::intern(const Place &p) {
  std::vector<uint64_t> key;
  key.reserve(p.Proj.size() + 1);
  key.push_back(p.Root);
  for (const Projection &pr : p.Proj)
    key.push_back((static_cast<uint64_t>(pr.K) << 32) | pr.Arg);
  auto it = Index.find(key);
  if (it != Index.end())
    return it->second;
  PlaceId id = static_cast<PlaceId>(Places.size());
  Places.push_back(p);
  Index.emplace(std::move(key), id);
  return id;
}

bool PlaceTable::isPrefixOf(PlaceId a, PlaceId b) const {
  const Place &pa = Places[a], &pb = Places[b];
  if (pa.Root != pb.Root || pa.Proj.size() > pb.Proj.size())
    return false;
  for (size_t i = 0; i < pa.Proj.size(); ++i) {
    const Projection &x = pa.Proj[i], &y = pb.Proj[i];
    if (x.K != y.K)
      return false;
    if (x.K != Projection::Index && x.Arg != y.Arg)
      return false;
  }
  return true;
}

bool PlaceTable::overlaps(PlaceId a, PlaceId b) const {
  return isPrefixOf(a, b) || isPrefixOf(b, a);
}

bool PlaceTable::throughDeref(PlaceId id) const {
  for (const Projection &p : Places[id].Proj)
    if (p.K == Projection::Deref)
      return true;
  return false;
}

PlaceId PlaceTable::parent(PlaceId id) {
  Place p = Places[id];
  if (p.Proj.empty())
    return kNone;
  p.Proj.pop_back();
  return intern(p);
}

PlaceId PlaceTable::project(PlaceId id, Projection pr) {
  Place p = Places[id];
  p.Proj.push_back(pr);
  return intern(p);
}

PlaceId PlaceTable::ownedPrefix(PlaceId id) {
  Place p = Places[id];
  for (size_t i = 0; i < p.Proj.size(); ++i)
    if (p.Proj[i].K == Projection::Deref) {
      p.Proj.resize(i);
      return intern(p);
    }
  return id;
}

//===----------------------------------------------------------------------===//
// Spelling
//===----------------------------------------------------------------------===//

std::string Body::spell(const Place &p) const {
  std::string s = p.Root < Locals.size() ? Locals[p.Root].Name : "?";
  if (s.empty() || Locals[p.Root].K == Local::Temp)
    s = Locals[p.Root].K == Local::Return ? "the result" : "it";
  bool deref = false;
  Place prefix{p.Root, {}};
  Type *t = p.Root < Locals.size() ? Locals[p.Root].Ty : nullptr;
  const EnumVariantDecl *variant = nullptr;
  for (const Projection &pr : p.Proj) {
    switch (pr.K) {
    case Projection::Deref:
      // A dereference reads naturally as the thing behind: `r.x` rather
      // than `(*r).x`. Only a bare `*p` of a real pointer keeps the star;
      // the object behind a class handle is just the handle's name.
      deref = t && t->is(TypeKind::Pointer);
      break;
    case Projection::Field: {
      std::string name = std::to_string(pr.Arg);
      if (variant) {
        if (pr.Arg < variant->Fields.size())
          for (const auto &f : variant->Fields)
            if (f->Index == pr.Arg)
              name = f->Name;
        variant = nullptr;
      } else if (t && t->isNominal() && !t->isOpaque()) {
        NominalDecl *nd = t->nominal();
        std::vector<NominalDecl *> chain{nd};
        if (auto *c = dyn_cast<ClassDecl>(static_cast<Decl *>(nd)))
          for (ClassDecl *sc = c->Super; sc; sc = sc->Super)
            chain.push_back(sc);
        for (NominalDecl *n : chain)
          for (const auto &f : n->Fields)
            if (f->Index == pr.Arg)
              name = f->Name;
      }
      s += "." + name;
      deref = false;
      break;
    }
    case Projection::Index:
      s += "[…]";
      deref = false;
      break;
    case Projection::Variant: {
      auto *e = t && t->isNominal()
                    ? dyn_cast<EnumDecl>(static_cast<Decl *>(t->nominal()))
                    : nullptr;
      if (e && pr.Arg < e->Variants.size())
        variant = e->Variants[pr.Arg].get();
      deref = false;
      break;
    }
    }
    prefix.Proj.push_back(pr);
    Type *next = placeType(*this, const_cast<PlaceTable &>(Places).intern(prefix));
    if (pr.K != Projection::Variant)
      t = next;
  }
  if (deref && p.Proj.size() == 1)
    s = "*" + s;
  return s;
}

std::string Body::spell(PlaceId p) const {
  return p == kNone || p >= Places.size() ? "it" : spell(Places.get(p));
}

//===----------------------------------------------------------------------===//
// Type questions the lowering asks
//===----------------------------------------------------------------------===//

/// True when values of `t` hold a borrow somewhere inside: a `&T`, a slice,
/// a closure (its captures may be borrows), or an aggregate containing one.
/// Such a value has an origin. Reference-counted handles do not count on
/// their own: what they own is theirs.
bool carriesReference(Type *t) {
  std::set<Type *> seen;
  std::function<bool(Type *)> go = [&](Type *x) -> bool {
    if (!x || !seen.insert(x).second)
      return false;
    // `some Iterator` hides the type from callers, not from this question:
    // an iterator over a borrowed vector borrows whatever it is spelled as.
    if (x->isOpaque() && x->canonical() != x)
      return go(x->canonical());
    switch (x->kind()) {
    case TypeKind::Pointer:
      return !x->isRawPointer();
    case TypeKind::Slice:
    case TypeKind::Function:
      return true;
    case TypeKind::Array:
      return go(x->element());
    case TypeKind::Tuple:
      for (Type *e : x->tupleElements())
        if (go(e))
          return true;
      return false;
    case TypeKind::Struct:
    case TypeKind::Enum:
    case TypeKind::Class: {
      NominalDecl *nd = x->nominal();
      if (!nd)
        return false;
      for (const auto &f : nd->Fields)
        if (go(f->Ty))
          return true;
      // A container keeps its elements where no field says so — `Vector<&T>`
      // holds its borrows in a block of raw storage — so what it is an
      // instance *of* counts as much as what it declares.
      for (Type *a : x->typeArguments())
        if (go(a))
          return true;
      if (auto *e = dyn_cast<EnumDecl>(static_cast<Decl *>(nd)))
        for (const auto &v : e->Variants) {
          for (const auto &tt : v->TupleTypes)
            if (go(tt->Resolved))
              return true;
          for (const auto &f : v->Fields)
            if (go(f->Ty))
              return true;
        }
      if (auto *c = dyn_cast<ClassDecl>(static_cast<Decl *>(nd)))
        for (ClassDecl *s = c->Super; s; s = s->Super)
          for (const auto &f : s->Fields)
            if (go(f->Ty))
              return true;
      return false;
    }
    default:
      return false;
    }
  };
  return go(t);
}

/// True when a value of `t` has to be destroyed: it holds a heap allocation
/// or a `deinit`, directly or inside. Under Zombie this is what "owned"
/// means; it is also what today's `isRefCounted` answers, plus resources.
bool needsDrop(Type *t) {
  if (!t)
    return false;
  if (t->isRefCounted())
    return true;
  std::set<Type *> seen;
  std::function<bool(Type *)> go = [&](Type *x) -> bool {
    if (!x || !seen.insert(x).second)
      return false;
    switch (x->kind()) {
    case TypeKind::Struct:
    case TypeKind::Enum: {
      NominalDecl *nd = x->nominal();
      if (!nd)
        return false;
      if (nd->Deinit)
        return true;
      for (const auto &f : nd->Fields)
        if (go(f->Ty))
          return true;
      if (auto *e = dyn_cast<EnumDecl>(static_cast<Decl *>(nd)))
        for (const auto &v : e->Variants) {
          for (const auto &tt : v->TupleTypes)
            if (go(tt->Resolved))
              return true;
          for (const auto &f : v->Fields)
            if (go(f->Ty))
              return true;
        }
      return false;
    }
    case TypeKind::Array:
      return go(x->element());
    case TypeKind::Tuple:
      for (Type *e : x->tupleElements())
        if (go(e))
          return true;
      return false;
    default:
      return false;
    }
  };
  return go(t);
}

/// True for a `&T` / `&var T` the checker follows.
static bool isTrackedRef(Type *t) {
  return t && t->is(TypeKind::Pointer) && !t->isRawPointer() &&
         !t->isWeakPointer();
}

/// True when moving a value of this type out of a place is a move rather
/// than a copy: it is owned, or it is an exclusive borrow (which cannot be
/// duplicated).
static bool movesWhenUsed(Type *t) {
  return needsDrop(t);
}

//===----------------------------------------------------------------------===//
// The lowering
//===----------------------------------------------------------------------===//

namespace {

/// One lexical scope: which locals it declared (dropped in reverse on the
/// way out) and which `defer` bodies it owes.
struct ScopeFrame {
  std::vector<LocalId> Locals;
  std::vector<const Expr *> Deferred;
};

struct LoopFrame {
  std::string Label;
  BlockId Continue = kNone;
  BlockId Break = kNone;
  size_t ScopeDepth = 0;
  PlaceId Result = kNone;   ///< `break value` writes here
};

class Lowerer {
public:
  Lowerer(Body &body, DiagnosticEngine &diags) : B(body), Diags(diags) {}

  void lowerFunction(FunctionDecl *fn);
  void lowerClosureBody(const ClosureExpr *c, const std::vector<Local> &captures);

private:
  Body &B;
  DiagnosticEngine &Diags;
  BlockId Cur = kNone;
  std::vector<ScopeFrame> Scopes;
  std::vector<LoopFrame> Loops;
  /// Temporaries made while lowering the current statement.
  std::vector<LocalId> StatementTemps;
  bool InUnsafe = false;
  LocalId Untracked = kNone;
  std::unordered_map<const GlobalVarDecl *, LocalId> Globals;
  std::vector<const ClosureExpr *> Closures;
  /// Result place of the function, for `return`.
  PlaceId ReturnPlace = kNone;

  //=== Blocks ============================================================//

  BlockId newBlock() {
    B.Blocks.emplace_back();
    return static_cast<BlockId>(B.Blocks.size() - 1);
  }
  Block &cur() { return B.Blocks[Cur]; }
  bool terminated() const {
    return Cur == kNone || B.Blocks[Cur].Term.K != Terminator::Unreachable ||
           Dead;
  }
  /// After a `return`/`break`, statements up to the next join are dead; they
  /// go into a scratch block nothing reaches.
  bool Dead = false;
  Stmt DeadSink;

  void goTo(BlockId target, SourceRange r = {}) {
    if (Dead || Cur == kNone)
      return;
    Block &b = cur();
    if (b.Term.K != Terminator::Unreachable)
      return;
    b.Term.K = Terminator::Goto;
    b.Term.Succ = {target};
    b.Term.Range = r;
    B.Blocks[target].Preds.push_back(Cur);
  }
  void branch(PlaceId tested, BlockId yes, BlockId no, SourceRange r) {
    if (Dead || Cur == kNone)
      return;
    Block &b = cur();
    b.Term.K = Terminator::Branch;
    b.Term.Succ = {yes, no};
    b.Term.Tested = tested;
    b.Term.Range = r;
    B.Blocks[yes].Preds.push_back(Cur);
    B.Blocks[no].Preds.push_back(Cur);
  }
  void switchTo(PlaceId tested, const std::vector<BlockId> &targets,
                SourceRange r) {
    if (Dead || Cur == kNone)
      return;
    Block &b = cur();
    b.Term.K = Terminator::Switch;
    b.Term.Succ = targets;
    b.Term.Tested = tested;
    b.Term.Range = r;
    for (BlockId t : targets)
      B.Blocks[t].Preds.push_back(Cur);
  }
  void setBlock(BlockId b) {
    Cur = b;
    Dead = false;
  }
  /// Starts a block for whatever follows a jump that never falls through.
  void startDeadBlock() {
    Cur = newBlock();
    Dead = true;
  }

  Stmt &emit(Stmt s) {
    s.Unsafe = InUnsafe;
    // Whatever lands in a destination writes it; say so once, centrally,
    // so no producer can forget.
    if (s.Dst != kNone && (s.K == Stmt::Assign || s.K == Stmt::Borrow ||
                           s.K == Stmt::Call)) {
      bool has = false;
      for (const PlaceAccess &a : s.Accesses)
        if (a.A == Access::Write && a.Place == s.Dst)
          has = true;
      if (!has) {
        PlaceAccess w;
        w.Place = s.Dst;
        w.A = Access::Write;
        w.Range = s.Range;
        s.Accesses.push_back(w);
      }
    }
    if (Dead) {
      // Nothing reaches this; keep it out of the graph entirely.
      DeadSink = std::move(s);
      return DeadSink;
    }
    cur().Stmts.push_back(std::move(s));
    return cur().Stmts.back();
  }

  //=== Locals and places ================================================//

  LocalId addLocal(Local l) {
    if (l.Ty && carriesReference(l.Ty) && l.Origin == kNone) {
      Origin o;
      o.K = l.K == Local::Param ? Origin::Placeholder : Origin::Local;
      o.Owner = static_cast<LocalId>(B.Locals.size());
      o.Param = l.Index;
      l.Origin = static_cast<OriginId>(B.Origins.size());
      B.Origins.push_back(o);
    }
    l.Owned = l.Ty && needsDrop(l.Ty) && !l.RefLike;
    B.Locals.push_back(l);
    LocalId id = static_cast<LocalId>(B.Locals.size() - 1);
    if (l.Var)
      B.ByVar[l.Var] = id;
    return id;
  }

  LocalId localFor(VarDecl *v, SourceRange at) {
    auto it = B.ByVar.find(v);
    if (it != B.ByVar.end())
      return it->second;
    Local l;
    l.K = v->IsParam ? Local::Param : Local::User;
    l.Var = v;
    l.Ty = v->Ty;
    l.Name = v->Name;
    l.Range = v->Range;
    return addLocal(l);
  }

  LocalId globalFor(const GlobalVarDecl *g) {
    auto it = Globals.find(g);
    if (it != Globals.end())
      return it->second;
    Local l;
    l.K = Local::Global;
    l.Ty = g->Ty;
    l.Name = g->Name;
    l.Range = g->Range;
    l.Origin = B.GlobalOrigin;
    LocalId id = addLocal(l);
    Globals[g] = id;
    return id;
  }

  /// A temporary that only holds a two-phase receiver loan until the call.
  LocalId newHolder(SourceRange r) {
    LocalId id = newTemp(nullptr, r, "recv");
    Origin o;
    o.K = Origin::Local;
    o.Owner = id;
    B.Locals[id].Origin = static_cast<OriginId>(B.Origins.size());
    B.Locals[id].RefLike = true;
    B.Origins.push_back(o);
    return id;
  }

  LocalId newTemp(Type *t, SourceRange r, const char *what = "tmp") {
    Local l;
    l.K = Local::Temp;
    l.Ty = t;
    l.Name = what;
    l.Range = r;
    LocalId id = addLocal(l);
    StatementTemps.push_back(id);
    if (!Dead) {
      Stmt s;
      s.K = Stmt::StorageLive;
      s.Dst = place(id);
      s.Range = r;
      emit(std::move(s));
    }
    return id;
  }

  PlaceId place(LocalId l) { return B.Places.intern(Place{l, {}}); }
  PlaceId project(PlaceId p, Projection::Kind k, uint32_t arg = 0) {
    return B.Places.project(p, Projection{k, arg});
  }
  PlaceId untracked() {
    if (Untracked == kNone) {
      Local l;
      l.K = Local::Temp;
      l.Name = "<raw>";
      l.Origin = B.UntrackedOrigin;
      Untracked = addLocal(l);
    }
    return place(Untracked);
  }
  bool isUntracked(PlaceId p) const {
    return p == kNone || B.Places.get(p).Root == Untracked;
  }

  Type *typeOf(PlaceId p);
  OriginId originOf(PlaceId p) {
    if (p == kNone)
      return kNone;
    return B.Locals[B.Places.get(p).Root].Origin;
  }

  //=== Accesses ==========================================================//

  void noteUse(PlaceId p, SourceRange r) {
    if (p == kNone)
      return;
    B.Uses[B.Places.get(p).Root].push_back(r);
  }

  /// Reads `p` in the way its type dictates when the value is taken *by
  /// value*: a copy of something trivially copyable, a move of something
  /// owned, a reborrow of an exclusive reference.
  void consume(Stmt &s, PlaceId p, const Expr *e, SourceRange r) {
    if (p == kNone || isUntracked(p))
      return;
    noteUse(p, r);
    Type *t = typeOf(p);
    PlaceAccess a;
    a.Place = p;
    a.Range = r;
    a.Source = e;
    if (t && isTrackedRef(t) && t->isMutablePointer()) {
      // Using a `&var` by value hands the exclusive access on: a reborrow
      // of what it points at, for as long as the receiver keeps it.
      a.A = Access::Read;
      s.Accesses.push_back(a);
      PlaceAccess rb;
      rb.Place = project(p, Projection::Deref);
      rb.A = Access::Mut;
      rb.Range = r;
      rb.Source = e;
      s.Accesses.push_back(rb);
      return;
    }
    a.A = t && movesWhenUsed(t) ? Access::Move : Access::Read;
    if (a.A == Access::Move)
      B.HasMoves = true;
    s.Accesses.push_back(a);
  }

  void read(Stmt &s, PlaceId p, const Expr *e, SourceRange r) {
    if (p == kNone || isUntracked(p))
      return;
    noteUse(p, r);
    PlaceAccess a;
    a.Place = p;
    a.A = Access::Read;
    a.Range = r;
    a.Source = e;
    s.Accesses.push_back(a);
  }

  /// `dst = &p` / `&var p`: issues a loan.
  LoanId borrowInto(PlaceId dst, PlaceId p, bool mut, bool twoPhase,
                    const Expr *e, SourceRange r) {
    if (isUntracked(p)) {
      // Borrowing through a raw pointer: the result points somewhere the
      // checker cannot see, and satisfies every constraint.
      Stmt s;
      s.K = Stmt::Assign;
      s.Dst = dst;
      s.Range = r;
      s.Source = e;
      if (originOf(dst) != kNone)
        s.Subsets.push_back({B.UntrackedOrigin, originOf(dst)});
      emit(std::move(s));
      return kNone;
    }
    B.HasBorrows = true;
    noteUse(p, r);
    Loan l;
    l.Place = p;
    l.Mutable = mut;
    l.TwoPhase = twoPhase;
    l.Range = r;
    l.At = Location{Cur, static_cast<uint32_t>(Dead ? 0 : cur().Stmts.size())};
    LoanId id = static_cast<LoanId>(B.Loans.size());
    B.Loans.push_back(l);
    Stmt s;
    s.K = Stmt::Borrow;
    s.Dst = dst;
    s.Src = p;
    s.Loan = id;
    s.Range = r;
    s.Source = e;
    PlaceAccess a;
    a.Place = p;
    a.A = twoPhase ? Access::Reserve : (mut ? Access::Mut : Access::Shared);
    a.Range = r;
    a.Source = e;
    s.Accesses.push_back(a);
    // A borrow taken *through* another borrow is bounded by it: what the
    // result points at is what the reference it went through pointed at.
    flowThrough(s, p, dst);
    emit(std::move(s));
    return id;
  }

  /// A value that came out of place `src` and lands in `dst` carries the
  /// origins of every reference `src` went through.
  void flowThrough(Stmt &s, PlaceId src, PlaceId dst) {
    OriginId into = originOf(dst);
    if (into == kNone || src == kNone)
      return;
    const Place &p = B.Places.get(src);
    OriginId from = B.Locals[p.Root].Origin;
    if (from != kNone && from != into)
      s.Subsets.push_back({from, into});
  }

  //=== Scopes ============================================================//

  void pushScope() { Scopes.emplace_back(); }
  void declareIn(LocalId l) { Scopes.back().Locals.push_back(l); }

  /// Emits what leaving one scope does: its deferred bodies, then the drops
  /// and deaths of its locals in reverse order.
  void emitScopeExit(ScopeFrame &f) {
    for (size_t i = f.Deferred.size(); i > 0; --i)
      lowerDeferred(f.Deferred[i - 1]);
    for (size_t i = f.Locals.size(); i > 0; --i)
      emitLocalEnd(f.Locals[i - 1]);
  }
  void emitLocalEnd(LocalId l) {
    const Local &loc = B.Locals[l];
    if (loc.Owned) {
      Stmt d;
      d.K = Stmt::Drop;
      d.Dst = place(l);
      d.Range = loc.Range;
      PlaceAccess a;
      a.Place = d.Dst;
      a.A = Access::Drop;
      a.Range = loc.Range;
      d.Accesses.push_back(a);
      emit(std::move(d));
    }
    Stmt s;
    s.K = Stmt::StorageDead;
    s.Dst = place(l);
    s.Range = loc.Range;
    PlaceAccess a;
    a.Place = s.Dst;
    a.A = Access::StorageDead;
    a.Range = loc.Range;
    s.Accesses.push_back(a);
    emit(std::move(s));
  }
  void popScope() {
    emitScopeExit(Scopes.back());
    Scopes.pop_back();
  }
  /// Leaves every scope above `depth` without popping them: what `break`,
  /// `continue` and `return` do on their way out.
  void unwindTo(size_t depth) {
    for (size_t i = Scopes.size(); i > depth; --i)
      emitScopeExit(Scopes[i - 1]);
  }
  void endStatementTemps() {
    for (size_t i = StatementTemps.size(); i > 0; --i)
      emitLocalEnd(StatementTemps[i - 1]);
    StatementTemps.clear();
  }

  //=== Expressions =======================================================//

  PlaceId lower(Expr *e);
  PlaceId lowerPlace(Expr *e);
  /// Lowers a block. With `into`, the tail value is assigned there before
  /// the block's locals are destroyed — what a function body does with its
  /// result, so a returned borrow of a local is seen for what it is.
  PlaceId lowerBlock(BlockExpr *b, bool wantValue, PlaceId into = kNone);
  PlaceId lowerCall(CallExpr *c);
  PlaceId lowerIf(IfExpr *i);
  PlaceId lowerMatch(MatchExpr *m);
  PlaceId lowerLoop(Expr *e);
  PlaceId lowerFor(ForExpr *f);
  PlaceId lowerBinary(BinaryExpr *b);
  PlaceId lowerAssign(AssignExpr *a);
  PlaceId lowerTry(TryExpr *t);
  PlaceId lowerClosure(ClosureExpr *c);
  PlaceId lowerAggregate(Expr *e, const std::vector<Expr *> &parts,
                         const StructLitExpr *lit);
  void lowerReturn(Expr *value, SourceRange r);
  void lowerStmt(Stmt *) = delete;
  void lowerStatement(rune::Stmt *s);
  void lowerDeferred(const Expr *body);
  void bindPattern(Pattern *p, PlaceId src, bool owned, SourceRange r,
                   bool mutableAlias = false);
  /// The place a `match`, `if … is` or `while … is` really looks at.
  ///
  /// A scrutinee of reference type is matched *through* the reference, so
  /// what the payload comes out of is the referent — `cur.*@1.0`, not
  /// `cur@1.0` — and `match &var x` names `x` itself. `owned` comes back
  /// false when there is such a place and true when the subject is a value
  /// of the match's own; `mutableThrough` says whether it was reached
  /// exclusively, which is what lets a binding out of it be written through.
  PlaceId scrutineePlace(Expr *e, bool &owned, bool &mutableThrough);
  /// Materialises a value into a fresh temporary, consuming the place it
  /// came from. Returns the temporary's place.
  PlaceId materialise(PlaceId src, Type *t, const Expr *e, SourceRange r);
  /// Moves or copies `src` into `dst`.
  void assignInto(PlaceId dst, PlaceId src, const Expr *e, SourceRange r);
  PlaceId resultTemp(Type *t, SourceRange r) {
    if (!t || t->isVoid() || t->is(TypeKind::Never))
      return kNone;
    return place(newTemp(t, r));
  }
  PlaceId callResult(Type *t, SourceRange r) {
    return resultTemp(t, r);
  }
  bool declaredInCurrentScope(PlaceId p) {
    if (p == kNone || Scopes.empty())
      return false;
    LocalId root = B.Places.get(p).Root;
    for (LocalId l : Scopes.back().Locals)
      if (l == root)
        return true;
    return false;
  }
};

Type *Lowerer::typeOf(PlaceId p) { return placeType(B, p); }

} // namespace

Type *placeType(const Body &B, PlaceId p) {
  if (p == kNone)
    return nullptr;
  const Place &pl = B.Places.get(p);
  Type *t = B.Locals[pl.Root].Ty;
  // After a `Variant` step the next `Field` picks a payload of the variant
  // rather than a field of a type, so the variant is remembered.
  const EnumVariantDecl *variant = nullptr;
  for (const Projection &pr : pl.Proj) {
    if (!t && !variant)
      return nullptr;
    switch (pr.K) {
    case Projection::Deref:
      variant = nullptr;
      if (t && t->is(TypeKind::Pointer))
        t = t->pointee();
      else if (t && t->is(TypeKind::Class))
        ; // the object a handle owns has the class's own type
      else
        return nullptr;
      break;
    case Projection::Field: {
      if (variant) {
        const EnumVariantDecl *v = variant;
        variant = nullptr;
        if (pr.Arg < v->TupleTypes.size())
          t = v->TupleTypes[pr.Arg]->Resolved;
        else {
          t = nullptr;
          for (const auto &f : v->Fields)
            if (f->Index == pr.Arg)
              t = f->Ty;
        }
        break;
      }
      if (t->is(TypeKind::Tuple)) {
        const auto &els = t->tupleElements();
        t = pr.Arg < els.size() ? els[pr.Arg] : nullptr;
        break;
      }
      NominalDecl *nd = t->isNominal() ? t->nominal() : nullptr;
      if (!nd)
        return nullptr;
      Type *found = nullptr;
      std::vector<NominalDecl *> chain{nd};
      if (auto *c = dyn_cast<ClassDecl>(static_cast<Decl *>(nd)))
        for (ClassDecl *s = c->Super; s; s = s->Super)
          chain.push_back(s);
      for (NominalDecl *n : chain)
        for (const auto &f : n->Fields)
          if (f->Index == pr.Arg)
            found = f->Ty;
      t = found;
      break;
    }
    case Projection::Index:
      variant = nullptr;
      if (t && (t->is(TypeKind::Array) || t->is(TypeKind::Slice)))
        t = t->element();
      else
        return nullptr;
      break;
    case Projection::Variant: {
      auto *e = t && t->isNominal()
                    ? dyn_cast<EnumDecl>(static_cast<Decl *>(t->nominal()))
                    : nullptr;
      if (!e || pr.Arg >= e->Variants.size())
        return nullptr;
      variant = e->Variants[pr.Arg].get();
      t = nullptr; // a payload is reached by `Field` after this
      break;
    }
    }
  }
  return t;
}

namespace {

//===----------------------------------------------------------------------===//
// Entry points
//===----------------------------------------------------------------------===//

void Lowerer::lowerFunction(FunctionDecl *fn) {
  B.Fn = fn;
  Origin g;
  g.K = Origin::Global;
  B.GlobalOrigin = static_cast<OriginId>(B.Origins.size());
  B.Origins.push_back(g);
  Origin u;
  u.K = Origin::Untracked;
  B.UntrackedOrigin = static_cast<OriginId>(B.Origins.size());
  B.Origins.push_back(u);

  // Parameters, in order. `self` on a class is a handle; by reference it is
  // a borrow the type does not spell, so the local says so instead.
  unsigned index = 0;
  for (Param &p : fn->Params) {
    Local l;
    l.K = Local::Param;
    l.Var = p.Binding;
    l.Ty = p.Ty;
    l.Name = p.Name;
    l.Range = p.Range;
    l.Index = index++;
    if (p.IsSelf && p.SelfByRef && p.Ty && !p.Ty->is(TypeKind::Pointer)) {
      l.RefLike = true;
      l.RefMutable = p.SelfMutable;
      Origin o;
      o.K = Origin::Placeholder;
      o.Owner = static_cast<LocalId>(B.Locals.size());
      o.Param = l.Index;
      l.Origin = static_cast<OriginId>(B.Origins.size());
      B.Origins.push_back(o);
    }
    if (fn->Flavour == FunctionFlavour::Initialiser && p.IsSelf) {
      // `init` receives the object it is filling in; nothing else can
      // reach it yet, so it is exclusive whatever the spelling.
      l.RefLike = true;
      l.RefMutable = true;
      if (l.Origin == kNone) {
        Origin o;
        o.K = Origin::Placeholder;
        o.Owner = static_cast<LocalId>(B.Locals.size());
        o.Param = l.Index;
        l.Origin = static_cast<OriginId>(B.Origins.size());
        B.Origins.push_back(o);
      }
    }
    addLocal(l);
  }

  Type *ret = fn->Ty ? fn->Ty->result() : nullptr;
  {
    Local l;
    l.K = Local::Return;
    l.Ty = ret;
    l.Name = "<result>";
    l.Range = fn->ReturnType ? fn->ReturnType->Range : fn->NameRange;
    if (ret && carriesReference(ret)) {
      Origin o;
      o.K = Origin::Result;
      o.Owner = static_cast<LocalId>(B.Locals.size());
      l.Origin = static_cast<OriginId>(B.Origins.size());
      B.Origins.push_back(o);
    }
    B.ReturnLocal = addLocal(l);
    B.Locals[B.ReturnLocal].Owned = false;
    ReturnPlace = place(B.ReturnLocal);
  }

  B.Entry = newBlock();
  setBlock(B.Entry);
  pushScope();
  // Parameters belong to the outermost scope: the callee owns what it was
  // passed by value and destroys it on the way out.
  for (LocalId i = 0; i < static_cast<LocalId>(fn->Params.size()); ++i)
    declareIn(i);

  bool wantsValue = ret && !ret->isVoid();
  lowerBlock(fn->Body.get(), /*wantValue=*/wantsValue,
             wantsValue ? ReturnPlace : kNone);
  if (!Dead) {
    endStatementTemps();
    unwindTo(0);
    cur().Term.K = Terminator::Return;
    cur().Term.Range = fn->Range;
    B.Exits.push_back(Cur);
  }
  Scopes.clear();

  // Closures written in this body are bodies of their own.
  for (const ClosureExpr *c : Closures) {
    auto nested = std::make_unique<Body>();
    nested->Fn = c->Lifted;
    nested->Closure = c;
    Lowerer inner(*nested, Diags);
    std::vector<Local> caps;
    for (const Capture &cap : c->Captures) {
      Local l;
      l.K = Local::Capture;
      l.Var = cap.Var;
      l.Ty = cap.Ty;
      l.Name = cap.Name;
      l.Range = c->Range;
      l.Index = cap.Index;
      caps.push_back(l);
    }
    inner.lowerClosureBody(c, caps);
    B.Closures.push_back(std::move(nested));
  }
}

void Lowerer::lowerClosureBody(const ClosureExpr *c,
                               const std::vector<Local> &captures) {
  Origin g;
  g.K = Origin::Global;
  B.GlobalOrigin = static_cast<OriginId>(B.Origins.size());
  B.Origins.push_back(g);
  Origin u;
  u.K = Origin::Untracked;
  B.UntrackedOrigin = static_cast<OriginId>(B.Origins.size());
  B.Origins.push_back(u);

  unsigned index = 0;
  for (const Param &p : c->Params) {
    Local l;
    l.K = Local::Param;
    l.Var = p.Binding;
    l.Ty = p.Ty;
    l.Name = p.Name;
    l.Range = p.Range;
    l.Index = index++;
    addLocal(l);
  }
  // A captured variable is what the closure holds of the outer scope. It
  // is analysed as a parameter the closure was passed: a borrow when it is
  // only ever read, its value otherwise — which mode the parent then uses.
  for (Local cap : captures) {
    cap.K = Local::Capture;
    cap.Index = index++;
    Origin o;
    o.K = Origin::Placeholder;
    o.Owner = static_cast<LocalId>(B.Locals.size());
    o.Param = cap.Index;
    cap.Origin = static_cast<OriginId>(B.Origins.size());
    B.Origins.push_back(o);
    addLocal(cap);
  }

  Type *ret = c->Ty && c->Ty->is(TypeKind::Function) ? c->Ty->result()
                                                      : nullptr;
  {
    Local l;
    l.K = Local::Return;
    l.Ty = ret;
    l.Name = "<result>";
    l.Range = c->Range;
    if (ret && carriesReference(ret)) {
      Origin o;
      o.K = Origin::Result;
      o.Owner = static_cast<LocalId>(B.Locals.size());
      l.Origin = static_cast<OriginId>(B.Origins.size());
      B.Origins.push_back(o);
    }
    B.ReturnLocal = addLocal(l);
    B.Locals[B.ReturnLocal].Owned = false;
    ReturnPlace = place(B.ReturnLocal);
  }

  B.Entry = newBlock();
  setBlock(B.Entry);
  pushScope();
  for (LocalId i = 0; i < static_cast<LocalId>(c->Params.size()); ++i)
    declareIn(i);
  bool wantsValue = ret && !ret->isVoid();
  lowerBlock(c->Body.get(), wantsValue, wantsValue ? ReturnPlace : kNone);
  if (!Dead) {
    endStatementTemps();
    unwindTo(0);
    cur().Term.K = Terminator::Return;
    cur().Term.Range = c->Range;
    B.Exits.push_back(Cur);
  }
  Scopes.clear();
  for (const ClosureExpr *inner : Closures) {
    auto nested = std::make_unique<Body>();
    nested->Fn = inner->Lifted;
    nested->Closure = inner;
    Lowerer next(*nested, Diags);
    std::vector<Local> caps;
    for (const Capture &cap : inner->Captures) {
      Local l;
      l.K = Local::Capture;
      l.Var = cap.Var;
      l.Ty = cap.Ty;
      l.Name = cap.Name;
      l.Range = inner->Range;
      l.Index = cap.Index;
      caps.push_back(l);
    }
    next.lowerClosureBody(inner, caps);
    B.Closures.push_back(std::move(nested));
  }
}

//===----------------------------------------------------------------------===//
// Statements and blocks
//===----------------------------------------------------------------------===//

void Lowerer::lowerStatement(rune::Stmt *s) {
  if (!s || Dead)
    return;
  switch (s->Kind) {
  case NodeKind::ExprStmt: {
    auto *es = cast<ExprStmt>(s);
    PlaceId v = lower(es->Value.get());
    // A value nobody keeps is destroyed at the end of the statement — unless
    // it was somebody's place, which is left alone.
    if (v != kNone && !isUntracked(v)) {
      const Place &p = B.Places.get(v);
      bool temp = B.Locals[p.Root].K == Local::Temp && p.Proj.empty();
      if (!temp) {
        Stmt fr;
        fr.K = Stmt::FakeRead;
        fr.Range = es->Range;
        read(fr, v, es->Value.get(), es->Range);
        emit(std::move(fr));
      }
    }
    endStatementTemps();
    return;
  }
  case NodeKind::VarStmt: {
    auto *v = cast<VarStmtNode>(s);
    PlaceId init = v->Init ? lower(v->Init.get()) : kNone;
    bindPattern(v->Binding.get(), init, /*owned=*/true, v->Range);
    endStatementTemps();
    return;
  }
  case NodeKind::DeferStmt: {
    auto *d = cast<DeferStmtNode>(s);
    Scopes.back().Deferred.push_back(d->Body.get());
    return;
  }
  case NodeKind::DeclStmtKind:
    // A nested declaration (a local function, a type) has its own body.
    return;
  default:
    return;
  }
}

void Lowerer::lowerDeferred(const Expr *body) {
  // Each exit runs the deferred body once, so it is lowered once per exit
  // with temporaries of its own.
  std::vector<LocalId> saved;
  saved.swap(StatementTemps);
  lower(const_cast<Expr *>(body));
  endStatementTemps();
  StatementTemps.swap(saved);
}

PlaceId Lowerer::lowerBlock(BlockExpr *b, bool wantValue, PlaceId into) {
  if (!b)
    return kNone;
  // Temporaries of the statement this block sits in are not this block's
  // business: its own statements end only their own.
  std::vector<LocalId> outerTemps;
  outerTemps.swap(StatementTemps);
  pushScope();
  for (auto &st : b->Stmts) {
    lowerStatement(st.get());
    if (Dead)
      break;
  }
  PlaceId tail = kNone;
  if (!Dead && b->Tail) {
    tail = lower(b->Tail.get());
    if (into != kNone && !Dead) {
      if (tail != kNone)
        assignInto(into, tail, b->Tail.get(), b->Tail->Range);
      tail = kNone;
      endStatementTemps();
    } else if (wantValue && tail != kNone && !isUntracked(tail) &&
        declaredInCurrentScope(tail)) {
      // The value outlives the block but the place does not: take it out
      // before the block's locals are destroyed.
      tail = materialise(tail, b->Tail->Ty, b->Tail.get(), b->Tail->Range);
    } else if (!wantValue) {
      endStatementTemps();
    }
  }
  if (!Dead)
    popScope();
  else
    Scopes.pop_back();
  // The tail's temporaries live on into the enclosing statement.
  outerTemps.insert(outerTemps.end(), StatementTemps.begin(),
                    StatementTemps.end());
  StatementTemps.swap(outerTemps);
  return tail;
}

PlaceId Lowerer::materialise(PlaceId src, Type *t, const Expr *e,
                             SourceRange r) {
  if (src == kNone || !t || t->isVoid())
    return kNone;
  LocalId tmp = newTemp(t, r);
  PlaceId dst = place(tmp);
  assignInto(dst, src, e, r);
  return dst;
}

void Lowerer::assignInto(PlaceId dst, PlaceId src, const Expr *e,
                         SourceRange r) {
  if (dst == kNone)
    return;
  // A handle put where a shared borrow of it is wanted is lent, not moved:
  // `source: self` in a `&self` method, `let r: &Dog = dog`.
  {
    Type *dt = typeOf(dst);
    Type *st = typeOf(src);
    if (src != kNone && dt && st && dt->is(TypeKind::Pointer) &&
        !dt->isRawPointer() && !dt->isMutablePointer() && !st->is(TypeKind::Pointer) &&
        st->isHeapHandle()) {
      PlaceId object = project(src, Projection::Deref);
      borrowInto(dst, object, false, false, e, r);
      return;
    }
  }
  Stmt s;
  s.K = Stmt::Assign;
  s.Dst = dst;
  s.Src = src;
  s.Range = r;
  s.Source = e;
  if (src != kNone && src != dst) {
    consume(s, src, e, r);
    flowThrough(s, src, dst);
    Type *st = typeOf(src);
    s.MoveOfHandle = st && st->isHeapHandle();
  }
  PlaceAccess w;
  w.Place = dst;
  w.A = Access::Write;
  w.Range = r;
  w.Source = e;
  s.Accesses.push_back(w);
  // Writing a whole local that holds a reference forgets what it held.
  if (B.Places.get(dst).Proj.empty()) {
    OriginId o = originOf(dst);
    if (o != kNone && B.Origins[o].K == Origin::Local)
      s.Clears.push_back(o);
  }
  emit(std::move(s));
}

//===----------------------------------------------------------------------===//
// Places
//===----------------------------------------------------------------------===//

PlaceId Lowerer::lowerPlace(Expr *e) {
  if (!e)
    return kNone;
  switch (e->Kind) {
  case NodeKind::DeclRef: {
    auto *r = cast<DeclRefExpr>(e);
    if (auto *v = r->Resolved ? dyn_cast<VarDecl>(r->Resolved) : nullptr) {
      if (v->IsGlobal)
        return untracked();
      return place(localFor(v, r->Range));
    }
    if (auto *g = r->Resolved ? dyn_cast<GlobalVarDecl>(r->Resolved) : nullptr)
      return place(globalFor(g));
    return kNone; // a function, a variant: not a place
  }
  case NodeKind::SelfRef: {
    auto *s = cast<SelfExpr>(e);
    if (s->Binding)
      return place(localFor(s->Binding, s->Range));
    return kNone;
  }
  case NodeKind::Member: {
    auto *m = cast<MemberExpr>(e);
    if (m->FieldIndex < 0 && !m->IsTupleIndex)
      return kNone; // a method, not a field
    PlaceId base = lowerPlace(m->Base.get());
    if (base == kNone) {
      // A field of a temporary: name the temporary first.
      base = lower(m->Base.get());
      if (base == kNone)
        return kNone;
    }
    if (isUntracked(base))
      return base;
    Type *bt = m->Base->Ty;
    for (unsigned i = 0; i < m->AutoDerefs && bt; ++i) {
      if (bt->is(TypeKind::Pointer)) {
        if (bt->isRawPointer())
          return untracked();
        base = project(base, Projection::Deref);
        bt = bt->pointee();
      }
    }
    // A class is a handle: its fields live in the object it owns.
    if (bt && bt->is(TypeKind::Class))
      base = project(base, Projection::Deref);
    uint32_t idx = m->IsTupleIndex ? m->TupleIndex
                                   : static_cast<uint32_t>(m->FieldIndex);
    return project(base, Projection::Field, idx);
  }
  case NodeKind::Index: {
    auto *i = cast<IndexExpr>(e);
    if (i->OverloadResolved || i->StringChar)
      return kNone; // a call, or a character read out of a String: a value
    if (i->ThroughRawPointer)
      return untracked();
    PlaceId base = lowerPlace(i->Base.get());
    if (base == kNone)
      base = lower(i->Base.get());
    if (base == kNone)
      return kNone;
    if (isUntracked(base))
      return base;
    Type *bt = i->Base->Ty;
    while (bt && bt->is(TypeKind::Pointer)) {
      if (bt->isRawPointer())
        return untracked();
      base = project(base, Projection::Deref);
      bt = bt->pointee();
    }
    // A slice is a view: its elements are behind it, not in it, so an
    // element of one is reached through the view the way `*r` is.
    if (bt && bt->is(TypeKind::Slice))
      base = project(base, Projection::Deref);
    // Slicing (`v[a..b]`) is a borrow of the range, not an element.
    if (isa<RangeExpr>(i->Index.get()))
      return base;
    // The index itself is read as part of whatever uses the place.
    Stmt s;
    s.K = Stmt::Nop;
    PlaceId ip = lower(i->Index.get());
    if (ip != kNone) {
      read(s, ip, i->Index.get(), i->Index->Range);
      if (!s.Accesses.empty()) {
        s.K = Stmt::FakeRead;
        s.Range = i->Index->Range;
        emit(std::move(s));
      }
    }
    return project(base, Projection::Index);
  }
  case NodeKind::Deref: {
    auto *d = cast<DerefExpr>(e);
    if (d->OverloadResolved)
      return kNone; // `deref()` returns a value
    Type *ot = d->Operand->Ty;
    if (ot && ot->is(TypeKind::Pointer) && ot->isRawPointer())
      return untracked();
    PlaceId base = lowerPlace(d->Operand.get());
    if (base == kNone)
      base = lower(d->Operand.get());
    if (base == kNone || isUntracked(base))
      return base;
    return project(base, Projection::Deref);
  }
  case NodeKind::UnsafeBlock: {
    bool saved = InUnsafe;
    InUnsafe = true;
    PlaceId p = lowerBlock(cast<UnsafeBlockExpr>(e)->Body.get(), true);
    InUnsafe = saved;
    return p;
  }
  default:
    return kNone;
  }
}

//===----------------------------------------------------------------------===//
// Expressions
//===----------------------------------------------------------------------===//

PlaceId Lowerer::lower(Expr *e) {
  if (!e || Dead)
    return kNone;
  switch (e->Kind) {
  // --- places -----------------------------------------------------------
  case NodeKind::DeclRef:
  case NodeKind::SelfRef:
    return lowerPlace(e);
  case NodeKind::Member: {
    auto *m = cast<MemberExpr>(e);
    if (m->FieldIndex >= 0 || m->IsTupleIndex)
      return lowerPlace(e);
    // A method used as a value: the closure keeps the receiver.
    PlaceId base = lower(m->Base.get());
    PlaceId dst = resultTemp(e->Ty, e->Range);
    Stmt s;
    s.K = Stmt::Assign;
    s.Dst = dst;
    s.Range = e->Range;
    s.Source = e;
    if (base != kNone) {
      consume(s, base, m->Base.get(), m->Base->Range);
      flowThrough(s, base, dst);
    }
    emit(std::move(s));
    return dst;
  }
  case NodeKind::Index: {
    auto *i = cast<IndexExpr>(e);
    if (i->StringChar) {
      // `text[i]`: a read of the string and of the index, producing a
      // character that borrows nothing.
      PlaceId base = lowerPlace(i->Base.get());
      if (base == kNone)
        base = lower(i->Base.get());
      PlaceId idx = lower(i->Index.get());
      PlaceId dst = resultTemp(e->Ty, e->Range);
      Stmt s;
      s.K = Stmt::FakeRead;
      s.Range = e->Range;
      s.Source = e;
      if (base != kNone)
        read(s, base, i->Base.get(), i->Base->Range);
      if (idx != kNone)
        read(s, idx, i->Index.get(), i->Index->Range);
      if (!s.Accesses.empty())
        emit(std::move(s));
      return dst;
    }
    if (auto *impl = i->OverloadResolved) {
      // `index(&self, i)`: a call with the base as receiver.
      PlaceId base = lowerPlace(i->Base.get());
      if (base == kNone)
        base = lower(i->Base.get());
      PlaceId idx = lower(i->Index.get());
      PlaceId dst = callResult(e->Ty, e->Range);
      Stmt s;
      s.K = Stmt::Call;
      s.Dst = dst;
      s.Callee = impl;
      s.Range = e->Range;
      s.Source = e;
      const Param *selfP = impl->Params.empty() ? nullptr : &impl->Params[0];
      if (selfP && selfP->IsSelf && base != kNone) {
        if (selfP->SelfByRef) {
          PlaceAccess a;
          a.Place = base;
          a.A = selfP->SelfMutable ? Access::Mut : Access::Shared;
          a.Range = i->Base->Range;
          a.Source = i->Base.get();
          s.Accesses.push_back(a);
          noteUse(base, i->Base->Range);
        } else {
          consume(s, base, i->Base.get(), i->Base->Range);
        }
      }
      s.Args.push_back(base);
      if (idx != kNone) {
        consume(s, idx, i->Index.get(), i->Index->Range);
        s.Args.push_back(idx);
      }
      emit(std::move(s));
      return dst;
    }
    if (isa<RangeExpr>(i->Index.get())) {
      // `v[a..b]`: a slice borrowing the same storage.
      auto *r = cast<RangeExpr>(i->Index.get());
      PlaceId lo = lower(r->Lo.get());
      PlaceId hi = lower(r->Hi.get());
      PlaceId base = lowerPlace(i->Base.get());
      if (base == kNone)
        base = lower(i->Base.get());
      PlaceId dst = resultTemp(e->Ty, e->Range);
      {
        Stmt s;
        s.K = Stmt::FakeRead;
        s.Range = e->Range;
        read(s, lo, r->Lo.get(), r->Range);
        read(s, hi, r->Hi.get(), r->Range);
        if (!s.Accesses.empty())
          emit(std::move(s));
      }
      Type *bt = i->Base->Ty;
      bool mut = bt && bt->is(TypeKind::Pointer) && bt->isMutablePointer();
      if (base != kNone && dst != kNone)
        borrowInto(dst, base, mut, false, e, e->Range);
      return dst;
    }
    return lowerPlace(e);
  }
  case NodeKind::Deref: {
    auto *d = cast<DerefExpr>(e);
    if (auto *impl = d->OverloadResolved) {
      PlaceId base = lowerPlace(d->Operand.get());
      if (base == kNone)
        base = lower(d->Operand.get());
      PlaceId dst = callResult(e->Ty, e->Range);
      Stmt s;
      s.K = Stmt::Call;
      s.Dst = dst;
      s.Callee = impl;
      s.Range = e->Range;
      s.Source = e;
      if (base != kNone) {
        const Param *selfP = impl->Params.empty() ? nullptr : &impl->Params[0];
        if (selfP && selfP->IsSelf && selfP->SelfByRef) {
          PlaceAccess a;
          a.Place = base;
          a.A = selfP->SelfMutable ? Access::Mut : Access::Shared;
          a.Range = d->Operand->Range;
          a.Source = d->Operand.get();
          s.Accesses.push_back(a);
          noteUse(base, d->Operand->Range);
        } else {
          consume(s, base, d->Operand.get(), d->Operand->Range);
        }
      }
      s.Args.push_back(base);
      emit(std::move(s));
      return dst;
    }
    return lowerPlace(e);
  }

  // --- constants --------------------------------------------------------
  case NodeKind::IntLit:
  case NodeKind::FloatLit:
  case NodeKind::CharLit:
  case NodeKind::BoolLit:
  case NodeKind::NilLit:
    return kNone;
  case NodeKind::StringLit: {
    // A literal is an owned `String`; it needs a place so a move of it can
    // be followed, though its drop is nothing. The object itself is
    // immortal, which a borrow of the temporary gets to rely on.
    PlaceId dst = resultTemp(e->Ty, e->Range);
    if (dst != kNone)
      B.Locals[B.Places.get(dst).Root].Immortal = true;
    Stmt s;
    s.K = Stmt::Assign;
    s.Dst = dst;
    s.Range = e->Range;
    s.Source = e;
    emit(std::move(s));
    return dst;
  }

  // --- aggregates -------------------------------------------------------
  case NodeKind::ArrayLit: {
    auto *a = cast<ArrayLitExpr>(e);
    std::vector<Expr *> parts;
    for (auto &el : a->Elements)
      parts.push_back(el.get());
    if (a->RepeatCount)
      parts.push_back(a->RepeatCount.get());
    return lowerAggregate(e, parts, nullptr);
  }
  case NodeKind::TupleLit: {
    auto *t = cast<TupleLitExpr>(e);
    std::vector<Expr *> parts;
    for (auto &el : t->Elements)
      parts.push_back(el.get());
    return lowerAggregate(e, parts, nullptr);
  }
  case NodeKind::StructLit: {
    auto *sl = cast<StructLitExpr>(e);
    std::vector<Expr *> parts;
    for (auto &f : sl->Fields)
      parts.push_back(f.Value.get());
    if (sl->Base)
      parts.push_back(sl->Base.get());
    return lowerAggregate(e, parts, sl);
  }

  // --- operators --------------------------------------------------------
  case NodeKind::Unary: {
    auto *u = cast<UnaryExpr>(e);
    if (auto *impl = u->OverloadResolved) {
      PlaceId op = lowerPlace(u->Operand.get());
      if (op == kNone)
        op = lower(u->Operand.get());
      PlaceId dst = callResult(e->Ty, e->Range);
      Stmt s;
      s.K = Stmt::Call;
      s.Dst = dst;
      s.Callee = impl;
      s.Range = e->Range;
      s.Source = e;
      const Param *selfP = impl->Params.empty() ? nullptr : &impl->Params[0];
      if (op != kNone && selfP && selfP->IsSelf && selfP->SelfByRef) {
        PlaceAccess a;
        a.Place = op;
        a.A = selfP->SelfMutable ? Access::Mut : Access::Shared;
        a.Range = u->Operand->Range;
        a.Source = u->Operand.get();
        s.Accesses.push_back(a);
        noteUse(op, u->Operand->Range);
      } else if (op != kNone) {
        consume(s, op, u->Operand.get(), u->Operand->Range);
      }
      s.Args.push_back(op);
      emit(std::move(s));
      return dst;
    }
    PlaceId op = lower(u->Operand.get());
    PlaceId dst = resultTemp(e->Ty, e->Range);
    Stmt s;
    s.K = Stmt::Assign;
    s.Dst = dst;
    s.Range = e->Range;
    s.Source = e;
    read(s, op, u->Operand.get(), u->Operand->Range);
    emit(std::move(s));
    return dst;
  }
  case NodeKind::Binary:
    return lowerBinary(cast<BinaryExpr>(e));
  case NodeKind::Assign:
    return lowerAssign(cast<AssignExpr>(e));
  case NodeKind::Cast: {
    auto *c = cast<CastExpr>(e);
    PlaceId op = lowerPlace(c->Operand.get());
    if (op == kNone)
      op = lower(c->Operand.get());
    PlaceId dst = resultTemp(e->Ty, e->Range);
    Stmt s;
    s.K = Stmt::Assign;
    s.Dst = dst;
    s.Range = e->Range;
    s.Source = e;
    bool toRaw = e->Ty && e->Ty->is(TypeKind::Pointer) && e->Ty->isRawPointer();
    if (op != kNone) {
      if (toRaw) {
        // The address goes somewhere the checker cannot follow. The place
        // is read; nothing is borrowed, because nothing could be released.
        read(s, op, c->Operand.get(), c->Operand->Range);
      } else if (c->Operand->Ty && c->Operand->Ty->is(TypeKind::Pointer) &&
                 c->Operand->Ty->isRawPointer()) {
        // A raw pointer becoming a borrow: untracked origin.
        read(s, op, c->Operand.get(), c->Operand->Range);
        if (originOf(dst) != kNone)
          s.Subsets.push_back({B.UntrackedOrigin, originOf(dst)});
      } else {
        consume(s, op, c->Operand.get(), c->Operand->Range);
        flowThrough(s, op, dst);
      }
    }
    emit(std::move(s));
    return dst;
  }
  case NodeKind::Into: {
    auto *in = cast<IntoExpr>(e);
    PlaceId op = lowerPlace(in->Operand.get());
    if (op == kNone)
      op = lower(in->Operand.get());
    PlaceId dst = callResult(e->Ty, e->Range);
    Stmt s;
    s.K = Stmt::Call;
    s.Dst = dst;
    s.Callee = in->Conversion;
    s.Range = e->Range;
    s.Source = e;
    if (op != kNone) {
      const Param *selfP = in->Conversion && !in->Conversion->Params.empty()
                               ? &in->Conversion->Params[0]
                               : nullptr;
      if (selfP && selfP->IsSelf && selfP->SelfByRef) {
        PlaceAccess a;
        a.Place = op;
        a.A = selfP->SelfMutable ? Access::Mut : Access::Shared;
        a.Range = in->Operand->Range;
        a.Source = in->Operand.get();
        s.Accesses.push_back(a);
        noteUse(op, in->Operand->Range);
      } else {
        consume(s, op, in->Operand.get(), in->Operand->Range);
      }
    }
    s.Args.push_back(op);
    emit(std::move(s));
    return dst;
  }
  case NodeKind::TypeTest: {
    auto *t = cast<TypeTestExpr>(e);
    PlaceId op = lowerPlace(t->Operand.get());
    if (op == kNone)
      op = lower(t->Operand.get());
    PlaceId dst = resultTemp(e->Ty, e->Range);
    Stmt s;
    s.K = Stmt::Assign;
    s.Dst = dst;
    s.Range = e->Range;
    s.Source = e;
    read(s, op, t->Operand.get(), t->Operand->Range);
    emit(std::move(s));
    return dst;
  }
  case NodeKind::Range: {
    auto *r = cast<RangeExpr>(e);
    PlaceId lo = lower(r->Lo.get());
    PlaceId hi = lower(r->Hi.get());
    PlaceId dst = resultTemp(e->Ty, e->Range);
    Stmt s;
    s.K = Stmt::Assign;
    s.Dst = dst;
    s.Range = e->Range;
    s.Source = e;
    read(s, lo, r->Lo.get(), r->Range);
    read(s, hi, r->Hi.get(), r->Range);
    emit(std::move(s));
    return dst;
  }
  case NodeKind::Borrow: {
    auto *b = cast<BorrowExpr>(e);
    PlaceId op = lowerPlace(b->Operand.get());
    if (op == kNone)
      op = lower(b->Operand.get());   // `&f()`: a borrow of a temporary
    PlaceId dst = resultTemp(e->Ty, e->Range);
    if (op == kNone || dst == kNone)
      return dst;
    Type *ot = b->Operand->Ty;
    if (ot && isTrackedRef(ot)) {
      // `&r` where `r` is already a borrow: a reborrow of what it points at.
      op = project(op, Projection::Deref);
    } else if (ot && ot->isHeapHandle() && !b->IsMutable) {
      // `&text` where `text` is a `String` or a class: the shared borrow
      // is of the object the handle owns, which stays where it is when the
      // handle moves — what lets a struct borrow from its own field
      // (§3.3), and what `&self` on a class already is. A `&var` borrow is
      // of the slot, so that `*out += x` can put a new handle in it.
      op = project(op, Projection::Deref);
    }
    borrowInto(dst, op, b->IsMutable, false, e, e->Range);
    return dst;
  }
  case NodeKind::Move: {
    auto *m = cast<MoveExpr>(e);
    PlaceId op = lowerPlace(m->Operand.get());
    if (op == kNone)
      op = lower(m->Operand.get());
    if (op == kNone)
      return kNone;
    PlaceId dst = resultTemp(e->Ty, e->Range);
    if (dst == kNone)
      return kNone;
    Stmt s;
    s.K = Stmt::Assign;
    s.Dst = dst;
    s.Src = op;
    s.Range = e->Range;
    s.Source = e;
    noteUse(op, e->Range);
    PlaceAccess a;
    a.Place = op;
    a.A = Access::Move;
    a.Range = e->Range;
    a.Source = m->Operand.get();
    s.Accesses.push_back(a);
    B.HasMoves = true;
    flowThrough(s, op, dst);
    s.MoveOfHandle = typeOf(op) && typeOf(op)->isHeapHandle();
    PlaceAccess w;
    w.Place = dst;
    w.A = Access::Write;
    w.Range = e->Range;
    s.Accesses.push_back(w);
    emit(std::move(s));
    return dst;
  }

  // --- calls and closures ----------------------------------------------
  case NodeKind::Call:
    return lowerCall(cast<CallExpr>(e));
  case NodeKind::Closure:
    return lowerClosure(cast<ClosureExpr>(e));

  // --- control flow -----------------------------------------------------
  case NodeKind::Block:
    return lowerBlock(cast<BlockExpr>(e), true);
  case NodeKind::UnsafeBlock: {
    bool saved = InUnsafe;
    InUnsafe = true;
    PlaceId p = lowerBlock(cast<UnsafeBlockExpr>(e)->Body.get(), true);
    InUnsafe = saved;
    return p;
  }
  case NodeKind::If:
    return lowerIf(cast<IfExpr>(e));
  case NodeKind::Match:
    return lowerMatch(cast<MatchExpr>(e));
  case NodeKind::While:
  case NodeKind::Loop:
    return lowerLoop(e);
  case NodeKind::For:
    return lowerFor(cast<ForExpr>(e));
  case NodeKind::Return: {
    auto *r = cast<ReturnExpr>(e);
    lowerReturn(r->Value.get(), r->Range);
    return kNone;
  }
  case NodeKind::Break: {
    auto *br = cast<BreakExpr>(e);
    LoopFrame *frame = nullptr;
    for (size_t i = Loops.size(); i > 0; --i)
      if (br->Label.empty() || Loops[i - 1].Label == br->Label) {
        frame = &Loops[i - 1];
        break;
      }
    if (!frame)
      return kNone;
    if (br->Value) {
      PlaceId v = lower(br->Value.get());
      if (frame->Result != kNone)
        assignInto(frame->Result, v, br->Value.get(), br->Value->Range);
    }
    endStatementTemps();
    unwindTo(frame->ScopeDepth);
    goTo(frame->Break, br->Range);
    startDeadBlock();
    return kNone;
  }
  case NodeKind::Continue: {
    auto *c = cast<ContinueExpr>(e);
    LoopFrame *frame = nullptr;
    for (size_t i = Loops.size(); i > 0; --i)
      if (c->Label.empty() || Loops[i - 1].Label == c->Label) {
        frame = &Loops[i - 1];
        break;
      }
    if (!frame)
      return kNone;
    endStatementTemps();
    unwindTo(frame->ScopeDepth);
    goTo(frame->Continue, c->Range);
    startDeadBlock();
    return kNone;
  }
  case NodeKind::Try:
    return lowerTry(cast<TryExpr>(e));
  case NodeKind::SuperRef:
  case NodeKind::Error:
  default:
    return kNone;
  }
}

PlaceId Lowerer::lowerAggregate(Expr *e, const std::vector<Expr *> &parts,
                                const StructLitExpr *lit) {
  PlaceId dst = resultTemp(e->Ty, e->Range);
  // Every part is evaluated first, in order, then written into its own
  // place in the whole — so a part that is a handle carries the loans into
  // its object along with it, and a struct may borrow from its own field.
  std::vector<PlaceId> places;
  for (Expr *p : parts) {
    PlaceId v = lowerPlace(p);
    if (v == kNone)
      v = lower(p);
    places.push_back(v);
  }
  auto *arr = dyn_cast<ArrayLitExpr>(e);
  // A field that borrows from a sibling is written after the sibling, so
  // the sibling's object is already in place to be borrowed from.
  std::vector<size_t> order;
  std::vector<const FieldDecl *> fieldOf(parts.size(), nullptr);
  if (lit && e->Ty && e->Ty->isNominal()) {
    for (size_t i = 0; i < parts.size() && i < lit->Fields.size(); ++i) {
      unsigned idx = lit->Fields[i].FieldIndex;
      NominalDecl *nd = e->Ty->nominal();
      const std::vector<std::unique_ptr<FieldDecl>> *fields = &nd->Fields;
      if (lit->VariantIndex >= 0)
        if (auto *en = dyn_cast<EnumDecl>(static_cast<Decl *>(nd)))
          if (static_cast<size_t>(lit->VariantIndex) < en->Variants.size())
            fields = &en->Variants[lit->VariantIndex]->Fields;
      for (const auto &f : *fields)
        if (f->Index == idx)
          fieldOf[i] = f.get();
    }
  }
  for (size_t i = 0; i < parts.size(); ++i)
    if (!fieldOf[i] || !fieldOf[i]->InternalRef)
      order.push_back(i);
  for (size_t i = 0; i < parts.size(); ++i)
    if (fieldOf[i] && fieldOf[i]->InternalRef)
      order.push_back(i);
  for (size_t i : order) {
    if (places[i] == kNone || dst == kNone) {
      // A constant part: nothing to read.
      if (places[i] != kNone) {
        Stmt s;
        s.K = Stmt::FakeRead;
        s.Range = parts[i]->Range;
        consume(s, places[i], parts[i], parts[i]->Range);
        emit(std::move(s));
      }
      continue;
    }
    PlaceId sub;
    if (lit) {
      if (i < lit->Fields.size())
        sub = project(dst, Projection::Field, lit->Fields[i].FieldIndex);
      else
        sub = dst; // `..base`: the rest of the fields
    } else if (arr) {
      sub = project(dst, Projection::Index);
    } else {
      sub = project(dst, Projection::Field, static_cast<uint32_t>(i));
    }
    if (sub == dst) {
      // `..base` copies the fields not written: read as a whole.
      Stmt s;
      s.K = Stmt::FakeRead;
      s.Range = parts[i]->Range;
      consume(s, places[i], parts[i], parts[i]->Range);
      flowThrough(s, places[i], dst);
      emit(std::move(s));
      continue;
    }
    assignInto(sub, places[i], parts[i], parts[i]->Range);
    // Say which sibling an internal reference has to borrow from.
    if (fieldOf[i] && fieldOf[i]->InternalRef && !Dead &&
        !cur().Stmts.empty()) {
      Stmt &last = cur().Stmts.back();
      const OriginClause *clause = nullptr;
      std::function<void(const TypeRepr *)> find = [&](const TypeRepr *t) {
        if (!t || clause)
          return;
        if (t->Origin)
          clause = t->Origin.get();
        if (auto *pt = dyn_cast<PointerTypeRepr>(t))
          find(pt->Pointee.get());
        else if (auto *st = dyn_cast<SliceTypeRepr>(t))
          find(st->Element.get());
        else if (auto *ot = dyn_cast<OptionalTypeRepr>(t))
          find(ot->Element.get());
        else if (auto *nt = dyn_cast<NamedTypeRepr>(t))
          for (auto &a : nt->GenericArgs)
            find(a.get());
        else if (auto *tt = dyn_cast<TupleTypeRepr>(t))
          for (auto &el : tt->Elements)
            find(el.get());
      };
      find(fieldOf[i]->TypeAnnotation.get());
      if (clause && !clause->Places.empty() &&
          !clause->Places[0].FieldPath.empty()) {
        PlaceId target = dst;
        for (unsigned f : clause->Places[0].FieldPath)
          target = project(target, Projection::Field, f);
        last.InternalTarget = target;
        last.InternalField = fieldOf[i];
      }
    }
  }
  if (dst != kNone) {
    Stmt s;
    s.K = Stmt::Assign;
    s.Dst = dst;
    s.Range = e->Range;
    s.Source = e;
    s.Literal = lit;
    s.Aggregate = true;
    emit(std::move(s));
  }
  return dst;
}

PlaceId Lowerer::lowerBinary(BinaryExpr *b) {
  if (b->Op == BinaryOp::LogicalAnd || b->Op == BinaryOp::LogicalOr) {
    // Short-circuit: the right side runs on one edge only.
    PlaceId dst = resultTemp(b->Ty, b->Range);
    PlaceId l = lower(b->LHS.get());
    assignInto(dst, l, b->LHS.get(), b->LHS->Range);
    BlockId rhsB = newBlock(), join = newBlock();
    if (b->Op == BinaryOp::LogicalAnd)
      branch(dst, rhsB, join, b->OpRange);
    else
      branch(dst, join, rhsB, b->OpRange);
    setBlock(rhsB);
    PlaceId r = lower(b->RHS.get());
    assignInto(dst, r, b->RHS.get(), b->RHS->Range);
    goTo(join, b->Range);
    setBlock(join);
    return dst;
  }
  if (b->Op == BinaryOp::Coalesce) {
    // `a ?? b`: take `a`'s payload, or evaluate `b`.
    PlaceId dst = resultTemp(b->Ty, b->Range);
    PlaceId l = lowerPlace(b->LHS.get());
    bool ownedL = l == kNone;
    if (l == kNone)
      l = lower(b->LHS.get());
    BlockId some = newBlock(), none = newBlock(), join = newBlock();
    if (l != kNone) {
      Stmt fr;
      fr.K = Stmt::FakeRead;
      fr.Range = b->LHS->Range;
      read(fr, l, b->LHS.get(), b->LHS->Range);
      emit(std::move(fr));
    }
    switchTo(l, {some, none}, b->OpRange);
    setBlock(some);
    if (l != kNone && dst != kNone) {
      Type *lt = b->LHS->Ty;
      int si = lt ? variantIndexNamed(lt, "Some") : -1;
      PlaceId payload = si >= 0 ? project(project(l, Projection::Variant,
                                                  static_cast<uint32_t>(si)),
                                          Projection::Field, 0)
                                : l;
      bool owned = ownedL || !B.Places.throughDeref(l);
      if (owned)
        assignInto(dst, payload, b->LHS.get(), b->LHS->Range);
      else {
        // Out of borrowed content: the result aliases it.
        Stmt s;
        s.K = Stmt::Assign;
        s.Dst = dst;
        s.Src = payload;
        s.Range = b->Range;
        read(s, payload, b->LHS.get(), b->LHS->Range);
        flowThrough(s, payload, dst);
        emit(std::move(s));
      }
    }
    goTo(join, b->Range);
    setBlock(none);
    PlaceId r = lower(b->RHS.get());
    assignInto(dst, r, b->RHS.get(), b->RHS->Range);
    goTo(join, b->Range);
    setBlock(join);
    return dst;
  }
  if (auto *impl = b->OverloadResolved) {
    PlaceId l = lowerPlace(b->LHS.get());
    if (l == kNone)
      l = lower(b->LHS.get());
    PlaceId r = lowerPlace(b->RHS.get());
    if (r == kNone)
      r = lower(b->RHS.get());
    PlaceId dst = callResult(b->Ty, b->Range);
    Stmt s;
    s.K = Stmt::Call;
    s.Dst = dst;
    s.Callee = impl;
    s.Range = b->Range;
    s.Source = b;
    const Param *selfP = nullptr, *rhsP = nullptr;
    for (const Param &p : impl->Params) {
      if (p.IsSelf)
        selfP = &p;
      else if (!rhsP)
        rhsP = &p;
    }
    if (l != kNone) {
      if (selfP && selfP->IsSelf && selfP->SelfByRef) {
        PlaceAccess a;
        a.Place = l;
        a.A = selfP->SelfMutable ? Access::Mut : Access::Shared;
        a.Range = b->LHS->Range;
        a.Source = b->LHS.get();
        s.Accesses.push_back(a);
        noteUse(l, b->LHS->Range);
      } else {
        consume(s, l, b->LHS.get(), b->LHS->Range);
      }
    }
    s.Args.push_back(l);
    if (r != kNone) {
      if (rhsP && rhsP->Ty && isTrackedRef(rhsP->Ty) &&
          !(b->RHS->Ty && b->RHS->Ty->is(TypeKind::Pointer))) {
        // The overload takes `&T` and the operand is a place: borrowed for
        // the call.
        PlaceAccess a;
        a.Place = r;
        a.A = rhsP->Ty->isMutablePointer() ? Access::Mut : Access::Shared;
        a.Range = b->RHS->Range;
        a.Source = b->RHS.get();
        s.Accesses.push_back(a);
        noteUse(r, b->RHS->Range);
      } else {
        consume(s, r, b->RHS.get(), b->RHS->Range);
      }
    }
    s.Args.push_back(r);
    emit(std::move(s));
    return dst;
  }
  // The builtin operators read their operands: arithmetic, comparison, and
  // `String +`, which makes a new string from two it only reads.
  PlaceId l = lowerPlace(b->LHS.get());
  if (l == kNone)
    l = lower(b->LHS.get());
  PlaceId r = lowerPlace(b->RHS.get());
  if (r == kNone)
    r = lower(b->RHS.get());
  PlaceId dst = resultTemp(b->Ty, b->Range);
  Stmt s;
  s.K = Stmt::Assign;
  s.Dst = dst;
  s.Range = b->Range;
  s.Source = b;
  read(s, l, b->LHS.get(), b->LHS->Range);
  read(s, r, b->RHS.get(), b->RHS->Range);
  if (dst != kNone) {
    PlaceAccess w;
    w.Place = dst;
    w.A = Access::Write;
    w.Range = b->Range;
    s.Accesses.push_back(w);
  }
  emit(std::move(s));
  return dst;
}

PlaceId Lowerer::lowerAssign(AssignExpr *a) {
  // A compound assignment through an overload is a call on the target.
  if (a->OperatorImpl || a->DerefSetImpl || a->IndexSetImpl) {
    FunctionDecl *impl = a->OperatorImpl ? a->OperatorImpl
                         : a->DerefSetImpl ? a->DerefSetImpl
                                           : a->IndexSetImpl;
    Expr *target = a->LHS.get();
    Expr *index = nullptr;
    if (a->IndexSetImpl && isa<IndexExpr>(target)) {
      index = cast<IndexExpr>(target)->Index.get();
      target = cast<IndexExpr>(target)->Base.get();
    } else if (a->DerefSetImpl && isa<DerefExpr>(target)) {
      target = cast<DerefExpr>(target)->Operand.get();
    }
    PlaceId recv = lowerPlace(target);
    if (recv == kNone)
      recv = lower(target);
    PlaceId idx = index ? lower(index) : kNone;
    PlaceId rhs = lowerPlace(a->RHS.get());
    if (rhs == kNone)
      rhs = lower(a->RHS.get());
    // The receiver is exclusive for the call, reserved first so the
    // arguments may still read it (`v[i] = v.len()`).
    Stmt s;
    s.K = Stmt::Call;
    s.Callee = impl;
    s.Range = a->Range;
    s.Source = a;
    const Param *selfP = impl->Params.empty() ? nullptr : &impl->Params[0];
    LoanId reserved = kNone;
    if (recv != kNone && selfP && selfP->IsSelf && selfP->SelfByRef) {
      // Reserve, then the call activates.
      LocalId holder = newHolder(a->Range);
      reserved = borrowInto(place(holder), recv, selfP->SelfMutable, true, a,
                            target->Range);
      read(s, place(holder), target, target->Range);
    } else if (recv != kNone) {
      consume(s, recv, target, target->Range);
    }
    s.Args.push_back(recv);
    if (idx != kNone) {
      consume(s, idx, index, index->Range);
      s.Args.push_back(idx);
    }
    if (rhs != kNone) {
      consume(s, rhs, a->RHS.get(), a->RHS->Range);
      s.Args.push_back(rhs);
    }
    if (reserved != kNone) {
      PlaceAccess act;
      act.Place = recv;
      act.A = Access::Activate;
      act.Range = target->Range;
      act.Source = target;
      s.Accesses.push_back(act);
      s.Loan = reserved;
    }
    emit(std::move(s));
    return kNone;
  }

  PlaceId rhs = lowerPlace(a->RHS.get());
  if (rhs == kNone)
    rhs = lower(a->RHS.get());

  if (a->DeclaresBinding && a->DeclaredVar) {
    LocalId l = localFor(a->DeclaredVar, a->Range);
    Stmt live;
    live.K = Stmt::StorageLive;
    live.Dst = place(l);
    live.Range = a->Range;
    emit(std::move(live));
    declareIn(l);
    assignInto(place(l), rhs, a->RHS.get(), a->Range);
    return kNone;
  }

  PlaceId lhs = lowerPlace(a->LHS.get());
  if (lhs == kNone || isUntracked(lhs)) {
    // Through a raw pointer, or somewhere without a place: the right side
    // is still consumed.
    Stmt s;
    s.K = Stmt::Assign;
    s.Range = a->Range;
    s.Source = a;
    if (rhs != kNone)
      consume(s, rhs, a->RHS.get(), a->RHS->Range);
    emit(std::move(s));
    return kNone;
  }
  if (a->Op != AssignOp::Assign) {
    // `x += v` reads and writes `x`.
    Stmt s;
    s.K = Stmt::Assign;
    s.Dst = lhs;
    s.Range = a->Range;
    s.Source = a;
    read(s, lhs, a->LHS.get(), a->LHS->Range);
    read(s, rhs, a->RHS.get(), a->RHS->Range);
    PlaceAccess w;
    w.Place = lhs;
    w.A = Access::Write;
    w.Range = a->OpRange;
    w.Source = a;
    s.Accesses.push_back(w);
    emit(std::move(s));
    return kNone;
  }
  assignInto(lhs, rhs, a->RHS.get(), a->Range);
  return kNone;
}

PlaceId Lowerer::lowerCall(CallExpr *c) {
  FunctionDecl *target = c->Target;
  PlaceId dst = callResult(c->Ty, c->Range);
  Stmt s;
  s.K = Stmt::Call;
  s.Dst = dst;
  s.Callee = target;
  s.Site = c;
  s.Range = c->Range;
  s.Source = c;

  // The receiver of a method call, or the value being called.
  PlaceId recv = kNone;
  Expr *recvExpr = nullptr;
  bool methodCall = false;
  if (auto *m = dyn_cast<MemberExpr>(c->Callee.get())) {
    if (c->IsMethodCall || m->ResolvedMethod || m->Builtin != BuiltinMethod::None ||
        c->Builtin != BuiltinMethod::None) {
      recvExpr = m->Base.get();
      recv = lowerPlace(recvExpr);
      if (recv == kNone)
        recv = lower(recvExpr);
      methodCall = true;
    }
  }
  if (!methodCall && !target && !c->ConstructsClass && !c->ConstructsEnum) {
    // A closure or function value being called.
    PlaceId callee = lowerPlace(c->Callee.get());
    if (callee == kNone)
      callee = lower(c->Callee.get());
    s.CalleeValue = callee;
    if (callee != kNone)
      read(s, callee, c->Callee.get(), c->Callee->Range);
  }

  LoanId reserved = kNone;
  if (methodCall && recv != kNone) {
    const Param *selfP = target && !target->Params.empty() &&
                                 target->Params[0].IsSelf
                             ? &target->Params[0]
                             : nullptr;
    Type *rt = recvExpr->Ty;
    if (!selfP) {
      // A builtin (`$str()`, `length()`): reads its receiver.
      read(s, recv, recvExpr, recvExpr->Range);
    } else if (selfP->SelfByRef) {
      // The receiver is borrowed for the call. Through an existing borrow,
      // the borrow is of what it points at.
      PlaceId what = recv;
      if (rt && isTrackedRef(rt))
        what = project(recv, Projection::Deref);
      if (selfP->SelfMutable) {
        LocalId holder = newHolder(c->Range);
        reserved = borrowInto(place(holder), what, true, true, c,
                              recvExpr->Range);
        read(s, place(holder), recvExpr, recvExpr->Range);
      } else {
        PlaceAccess a;
        a.Place = what;
        a.A = Access::Shared;
        a.Range = recvExpr->Range;
        a.Source = recvExpr;
        s.Accesses.push_back(a);
        noteUse(what, recvExpr->Range);
        if (rt && isTrackedRef(rt) && originOf(recv) != kNone && dst != kNone &&
            originOf(dst) != kNone)
          ; // the summary decides what the result borrows from
      }
    } else {
      consume(s, recv, recvExpr, recvExpr->Range);
    }
    s.Args.push_back(recv);
  } else if (target && !target->Params.empty() && target->Params[0].IsSelf) {
    // `Type::method(x, …)`: the first argument is the receiver.
    s.Args.push_back(kNone);
  }

  // Arguments in parameter order, the order the code generator evaluates
  // them in.
  std::vector<const Param *> formals;
  if (target)
    for (const Param &p : target->Params)
      if (!p.IsSelf)
        formals.push_back(&p);
  size_t count = std::max(formals.size(), c->ArgOrder.size());
  if (c->ArgOrder.empty())
    count = c->Args.size();
  std::vector<bool> used(c->Args.size(), false);
  for (size_t i = 0; i < count; ++i) {
    Expr *value = nullptr;
    if (!c->ArgOrder.empty()) {
      if (i < c->ArgOrder.size() && c->ArgOrder[i] != static_cast<unsigned>(-1) &&
          c->ArgOrder[i] < c->Args.size()) {
        value = c->Args[c->ArgOrder[i]].Value.get();
        used[c->ArgOrder[i]] = true;
      }
    } else if (i < c->Args.size()) {
      value = c->Args[i].Value.get();
      used[i] = true;
    }
    if (!value) {
      s.Args.push_back(kNone);   // a default: made fresh at the call
      continue;
    }
    const Param *formal = i < formals.size() ? formals[i] : nullptr;
    PlaceId v = lowerPlace(value);
    if (v == kNone)
      v = lower(value);
    Type *want = formal ? formal->Ty : nullptr;
    if (v != kNone && want && isTrackedRef(want) && value->Ty &&
        !value->Ty->is(TypeKind::Pointer)) {
      // A place handed to a `&T` parameter: borrowed for the call (the
      // implicit borrow of arguments, §3.4 of the plan).
      PlaceAccess a;
      a.Place = v;
      a.A = want->isMutablePointer() ? Access::Mut : Access::Shared;
      a.Range = value->Range;
      a.Source = value;
      s.Accesses.push_back(a);
      noteUse(v, value->Range);
    } else if (v != kNone) {
      consume(s, v, value, value->Range);
    }
    s.Args.push_back(v);
  }
  // Variadic extras.
  for (size_t i = 0; i < c->Args.size(); ++i) {
    if (used[i])
      continue;
    Expr *value = c->Args[i].Value.get();
    PlaceId v = lowerPlace(value);
    if (v == kNone)
      v = lower(value);
    if (v != kNone)
      consume(s, v, value, value->Range);
    s.Args.push_back(v);
  }

  if (reserved != kNone) {
    PlaceAccess act;
    act.Place = B.Loans[reserved].Place;
    act.A = Access::Activate;
    act.Range = recvExpr->Range;
    act.Source = recvExpr;
    s.Accesses.push_back(act);
    s.Loan = reserved;
  }
  if (dst != kNone) {
    PlaceAccess w;
    w.Place = dst;
    w.A = Access::Write;
    w.Range = c->Range;
    s.Accesses.push_back(w);
  }
  emit(std::move(s));
  // A call that never returns — `panic`, `exit` — ends the path here.
  if (c->Ty && c->Ty->is(TypeKind::Never)) {
    if (!Dead && Cur != kNone) {
      cur().Term.K = Terminator::Unreachable;
      cur().Term.Range = c->Range;
    }
    startDeadBlock();
  }
  return dst;
}

PlaceId Lowerer::lowerClosure(ClosureExpr *c) {
  Closures.push_back(c);
  PlaceId dst = resultTemp(c->Ty, c->Range);
  Stmt s;
  s.K = Stmt::Assign;
  s.Dst = dst;
  s.Range = c->Range;
  s.Source = c;
  // Captures are taken by value: the closure keeps its own copy, moving what
  // is owned. A borrowed capture — a `&T` local, `self` in a method — makes
  // the closure a borrowing value. (Capture by reference is decided by the
  // checker after the closure body is read; see Zombie.cpp.)
  for (const Capture &cap : c->Captures) {
    if (!cap.Var)
      continue;
    LocalId l = localFor(cap.Var, c->Range);
    PlaceId p = place(l);
    // A binding that only borrows its value — a `&self` object receiver, a
    // payload of a borrowed scrutinee — is carried as the borrow it is, not
    // moved; its origin flows into the closure below.
    if (l != kNone && B.Locals[l].RefLike && !B.Locals[l].Owned)
      read(s, p, c, c->Range);
    else
      consume(s, p, c, c->Range);
    flowThrough(s, p, dst);
  }
  if (dst != kNone) {
    PlaceAccess w;
    w.Place = dst;
    w.A = Access::Write;
    w.Range = c->Range;
    s.Accesses.push_back(w);
  }
  emit(std::move(s));
  return dst;
}

//===----------------------------------------------------------------------===//
// Control flow
//===----------------------------------------------------------------------===//

PlaceId Lowerer::lowerIf(IfExpr *i) {
  PlaceId dst = resultTemp(i->Ty, i->Range);
  BlockId thenB = newBlock(), elseB = newBlock(), join = newBlock();
  if (i->BindingPat) {
    // `if x is Pat`: the scrutinee is a place when it can be.
    bool owned = true, scrutMutable = false;
    PlaceId scrut = scrutineePlace(i->Cond.get(), owned, scrutMutable);
    if (scrut != kNone) {
      Stmt fr;
      fr.K = Stmt::FakeRead;
      fr.Range = i->Cond->Range;
      read(fr, scrut, i->Cond.get(), i->Cond->Range);
      emit(std::move(fr));
    }
    bool scrutBorrowed = i->Cond->Ty && i->Cond->Ty->is(TypeKind::Pointer);
    switchTo(scrut, {thenB, elseB}, i->Cond->Range);
    setBlock(thenB);
    pushScope();
    bindPattern(i->BindingPat.get(), scrut,
                (owned || !B.Places.throughDeref(scrut)) && !scrutBorrowed,
                i->BindingPat->Range, scrutMutable);
    PlaceId v = lowerBlock(i->Then.get(), dst != kNone);
    if (!Dead && dst != kNone)
      assignInto(dst, v, i->Then->Tail.get(), i->Then->Range);
    if (!Dead)
      popScope();
    else
      Scopes.pop_back();
    goTo(join, i->Range);
  } else {
    PlaceId cond = lower(i->Cond.get());
    branch(cond, thenB, elseB, i->Cond->Range);
    setBlock(thenB);
    PlaceId v = lowerBlock(i->Then.get(), dst != kNone);
    if (!Dead && dst != kNone)
      assignInto(dst, v, i->Then->Tail.get(), i->Then->Range);
    goTo(join, i->Range);
  }
  setBlock(elseB);
  if (i->Else) {
    PlaceId v = lower(i->Else.get());
    if (!Dead && dst != kNone)
      assignInto(dst, v, i->Else.get(), i->Else->Range);
  }
  goTo(join, i->Range);
  setBlock(join);
  return dst;
}

PlaceId Lowerer::lowerMatch(MatchExpr *m) {
  PlaceId dst = resultTemp(m->Ty, m->Range);
  bool owned = true, scrutMutable = false;
  PlaceId scrut = scrutineePlace(m->Scrutinee.get(), owned, scrutMutable);
  if (scrut != kNone) {
    Stmt fr;
    fr.K = Stmt::FakeRead;
    fr.Range = m->Scrutinee->Range;
    read(fr, scrut, m->Scrutinee.get(), m->Scrutinee->Range);
    emit(std::move(fr));
  }
  // A scrutinee of reference type is matched through the reference, so its
  // payload is borrowed content — an alias, never a move — even though the
  // reference local itself carries no `Deref` projection.
  bool scrutBorrowed = m->Scrutinee->Ty && m->Scrutinee->Ty->is(TypeKind::Pointer);
  bool ownedScrut = (owned || (scrut != kNone && !B.Places.throughDeref(scrut))) &&
                    !scrutBorrowed;
  BlockId join = newBlock();
  // One block per arm; Sema has already made sure one of them matches.
  std::vector<BlockId> arms;
  for (size_t i = 0; i < m->Arms.size(); ++i)
    arms.push_back(newBlock());
  switchTo(scrut, arms, m->Scrutinee->Range);
  for (size_t i = 0; i < m->Arms.size(); ++i) {
    MatchArm &arm = m->Arms[i];
    setBlock(arms[i]);
    pushScope();
    bindPattern(arm.Pat.get(), scrut, ownedScrut && !arm.Guard, arm.Range,
                scrutMutable && !arm.Guard);
    if (arm.Guard) {
      // A guard sees the bindings but cannot consume them: the arm may not
      // be taken. Bindings are taken by reference until the guard passes.
      PlaceId g = lower(arm.Guard.get());
      BlockId taken = newBlock();
      BlockId next = i + 1 < m->Arms.size() ? arms[i + 1] : join;
      branch(g, taken, next, arm.Guard->Range);
      setBlock(taken);
    }
    PlaceId v = lower(arm.Body.get());
    if (!Dead && dst != kNone)
      assignInto(dst, v, arm.Body.get(), arm.Body->Range);
    if (!Dead)
      popScope();
    else
      Scopes.pop_back();
    goTo(join, arm.Range);
  }
  setBlock(join);
  return dst;
}

PlaceId Lowerer::lowerLoop(Expr *e) {
  BlockId header = newBlock(), body = newBlock(), exit = newBlock();
  LoopFrame frame;
  frame.Continue = header;
  frame.Break = exit;
  frame.ScopeDepth = Scopes.size();
  frame.Result = resultTemp(e->Ty, e->Range);
  if (auto *w = dyn_cast<WhileExpr>(e)) {
    frame.Label = w->Label;
    goTo(header, e->Range);
    setBlock(header);
    Loops.push_back(frame);
    if (w->BindingPat) {
      bool owned = true, scrutMutable = false;
      PlaceId scrut = scrutineePlace(w->Cond.get(), owned, scrutMutable);
      if (scrut != kNone) {
        Stmt fr;
        fr.K = Stmt::FakeRead;
        fr.Range = w->Cond->Range;
        read(fr, scrut, w->Cond.get(), w->Cond->Range);
        emit(std::move(fr));
      }
      bool scrutBorrowed = w->Cond->Ty && w->Cond->Ty->is(TypeKind::Pointer);
      switchTo(scrut, {body, exit}, w->Cond->Range);
      setBlock(body);
      pushScope();
      bindPattern(w->BindingPat.get(), scrut,
                  (owned || !B.Places.throughDeref(scrut)) && !scrutBorrowed,
                  w->BindingPat->Range, scrutMutable);
      lowerBlock(w->Body.get(), false);
      if (!Dead)
        popScope();
      else
        Scopes.pop_back();
    } else {
      PlaceId cond = lower(w->Cond.get());
      endStatementTemps();
      branch(cond, body, exit, w->Cond->Range);
      setBlock(body);
      lowerBlock(w->Body.get(), false);
    }
    goTo(header, e->Range);
    Loops.pop_back();
  } else {
    auto *l = cast<LoopExpr>(e);
    frame.Label = l->Label;
    goTo(header, e->Range);
    setBlock(header);
    Loops.push_back(frame);
    goTo(body, e->Range);
    setBlock(body);
    lowerBlock(l->Body.get(), false);
    goTo(header, e->Range);
    Loops.pop_back();
  }
  setBlock(exit);
  return frame.Result;
}

PlaceId Lowerer::lowerFor(ForExpr *f) {
  // The sequence is evaluated once and kept for the loop's duration.
  PlaceId seq = lowerPlace(f->Sequence.get());
  bool seqIsPlace = seq != kNone;
  if (seq == kNone)
    seq = lower(f->Sequence.get());
  Type *seqTy = f->Sequence->Ty;

  pushScope(); // the loop's own scope: the sequence slot, the iterator
  LocalId iter = kNone;
  PlaceId iterPlace = kNone;
  if (f->NextMethod) {
    if (f->IterateMethod) {
      // `iterate(&self)`: borrows the sequence for as long as the cursor
      // lives. A sequence that is a temporary is kept in a slot of its own.
      if (!seqIsPlace && seq != kNone) {
        LocalId slot = newTemp(seqTy, f->Sequence->Range, "seq");
        StatementTemps.pop_back();
        declareIn(slot);
        assignInto(place(slot), seq, f->Sequence.get(), f->Sequence->Range);
        seq = place(slot);
      }
      iter = newTemp(f->IterType, f->Sequence->Range, "iter");
      StatementTemps.pop_back();
      declareIn(iter);
      iterPlace = place(iter);
      Stmt s;
      s.K = Stmt::Call;
      s.Dst = iterPlace;
      s.Callee = f->IterateMethod;
      s.Range = f->Sequence->Range;
      s.Source = f;
      if (seq != kNone) {
        const Param &selfP = f->IterateMethod->Params[0];
        PlaceId what = seq;
        if (seqTy && isTrackedRef(seqTy))
          what = project(seq, Projection::Deref);
        if (selfP.SelfByRef) {
          PlaceAccess a;
          a.Place = what;
          a.A = selfP.SelfMutable ? Access::Mut : Access::Shared;
          a.Range = f->Sequence->Range;
          a.Source = f->Sequence.get();
          s.Accesses.push_back(a);
          noteUse(what, f->Sequence->Range);
        } else {
          consume(s, seq, f->Sequence.get(), f->Sequence->Range);
        }
      }
      s.Args.push_back(seq);
      emit(std::move(s));
    } else {
      // The sequence is its own cursor: driven where it is when it is a
      // place, or from a slot when it was made here.
      if (seqIsPlace) {
        iterPlace = seq;
        if (seqTy && isTrackedRef(seqTy))
          iterPlace = project(seq, Projection::Deref);
      } else {
        iter = newTemp(seqTy, f->Sequence->Range, "iter");
        StatementTemps.pop_back();
        declareIn(iter);
        iterPlace = place(iter);
        assignInto(iterPlace, seq, f->Sequence.get(), f->Sequence->Range);
      }
    }
  } else if (!seqIsPlace && seq != kNone) {
    LocalId slot = newTemp(seqTy, f->Sequence->Range, "seq");
    StatementTemps.pop_back();
    declareIn(slot);
    assignInto(place(slot), seq, f->Sequence.get(), f->Sequence->Range);
    seq = place(slot);
  }
  endStatementTemps();

  BlockId header = newBlock(), body = newBlock(), exit = newBlock();
  LoopFrame frame;
  frame.Label = f->Label;
  frame.Continue = header;
  frame.Break = exit;
  frame.ScopeDepth = Scopes.size();
  goTo(header, f->Range);
  setBlock(header);
  Loops.push_back(frame);

  pushScope(); // one turn
  if (f->NextMethod) {
    // `next(&var iter)` each turn; the element is owned by the turn.
    LocalId step = newTemp(f->NextResult, f->Range, "step");
    StatementTemps.pop_back();
    declareIn(step);
    Stmt s;
    s.K = Stmt::Call;
    s.Dst = place(step);
    s.Callee = f->NextMethod;
    s.Range = f->Sequence->Range;
    s.Source = f;
    if (iterPlace != kNone) {
      const Param &selfP = f->NextMethod->Params[0];
      PlaceAccess a;
      a.Place = iterPlace;
      a.A = selfP.SelfByRef ? (selfP.SelfMutable ? Access::Mut : Access::Shared)
                            : Access::Read;
      a.Range = f->Sequence->Range;
      a.Source = f->Sequence.get();
      s.Accesses.push_back(a);
      noteUse(iterPlace, f->Sequence->Range);
    }
    s.Args.push_back(iterPlace);
    emit(std::move(s));
    switchTo(place(step), {body, exit}, f->Range);
    setBlock(body);
    Type *ot = f->NextResult;
    int si = ot ? variantIndexNamed(ot, "Some") : -1;
    PlaceId payload = si >= 0 ? project(project(place(step), Projection::Variant,
                                                static_cast<uint32_t>(si)),
                                        Projection::Field, 0)
                              : place(step);
    bindPattern(f->Binding.get(), payload, /*owned=*/true, f->Binding->Range);
  } else {
    // An array, slice or range walked by index. The element is read in
    // place: an alias of `seq[…]` for the turn.
    Stmt fr;
    fr.K = Stmt::FakeRead;
    fr.Range = f->Sequence->Range;
    if (seq != kNone)
      read(fr, seq, f->Sequence.get(), f->Sequence->Range);
    emit(std::move(fr));
    branch(kNone, body, exit, f->Range);
    setBlock(body);
    PlaceId elem = kNone;
    if (seq != kNone && !isUntracked(seq)) {
      PlaceId base = seq;
      Type *bt = seqTy;
      while (bt && bt->is(TypeKind::Pointer) && !bt->isRawPointer()) {
        base = project(base, Projection::Deref);
        bt = bt->pointee();
      }
      if (bt && (bt->is(TypeKind::Array) || bt->is(TypeKind::Slice)))
        elem = project(base, Projection::Index);
    }
    bindPattern(f->Binding.get(), elem, /*owned=*/false, f->Binding->Range);
  }
  lowerBlock(f->Body.get(), false);
  if (!Dead)
    popScope();
  else
    Scopes.pop_back();
  goTo(header, f->Range);
  Loops.pop_back();
  setBlock(exit);
  popScope(); // the loop's scope: iterator and sequence slot
  return kNone;
}

PlaceId Lowerer::lowerTry(TryExpr *t) {
  PlaceId op = lowerPlace(t->Operand.get());
  bool owned = op == kNone;
  if (op == kNone)
    op = lower(t->Operand.get());
  PlaceId dst = resultTemp(t->Ty, t->Range);
  if (op == kNone)
    return dst;
  {
    Stmt fr;
    fr.K = Stmt::FakeRead;
    fr.Range = t->Range;
    read(fr, op, t->Operand.get(), t->Operand->Range);
    emit(std::move(fr));
  }
  BlockId ok = newBlock(), err = newBlock();
  switchTo(op, {ok, err}, t->Range);

  // The empty case leaves the function with the error.
  setBlock(err);
  Type *ot = t->Operand->Ty;
  int errIdx = ot ? variantIndexNamed(ot, "Err") : -1;
  if (errIdx < 0 && ot)
    errIdx = variantIndexNamed(ot, "None");
  PlaceId errPayload = op;
  if (errIdx >= 0 && ot && variantIndexNamed(ot, "Err") >= 0)
    errPayload = project(project(op, Projection::Variant,
                                 static_cast<uint32_t>(errIdx)),
                         Projection::Field, 0);
  {
    Stmt s;
    s.K = t->ErrorConversion ? Stmt::Call : Stmt::Assign;
    s.Dst = ReturnPlace;
    s.Callee = t->ErrorConversion;
    s.Range = t->Range;
    s.Source = t;
    if (ot && variantIndexNamed(ot, "Err") >= 0) {
      if (owned || !B.Places.throughDeref(op))
        consume(s, errPayload, t->Operand.get(), t->Range);
      else
        read(s, errPayload, t->Operand.get(), t->Range);
      flowThrough(s, errPayload, ReturnPlace);
    }
    if (t->ErrorConversion)
      s.Args.push_back(errPayload);
    if (ReturnPlace != kNone) {
      PlaceAccess w;
      w.Place = ReturnPlace;
      w.A = Access::Write;
      w.Range = t->Range;
      s.Accesses.push_back(w);
    }
    emit(std::move(s));
  }
  endStatementTemps();
  unwindTo(0);
  if (!Dead) {
    cur().Term.K = Terminator::Return;
    cur().Term.Range = t->Range;
    B.Exits.push_back(Cur);
  }

  setBlock(ok);
  int okIdx = ot ? variantIndexNamed(ot, "Ok") : -1;
  if (okIdx < 0 && ot)
    okIdx = variantIndexNamed(ot, "Some");
  PlaceId payload = okIdx >= 0 ? project(project(op, Projection::Variant,
                                                  static_cast<uint32_t>(okIdx)),
                                          Projection::Field, 0)
                                : op;
  if (dst != kNone) {
    if (owned || !B.Places.throughDeref(op))
      assignInto(dst, payload, t->Operand.get(), t->Range);
    else {
      Stmt s;
      s.K = Stmt::Assign;
      s.Dst = dst;
      s.Src = payload;
      s.Range = t->Range;
      read(s, payload, t->Operand.get(), t->Range);
      flowThrough(s, payload, dst);
      emit(std::move(s));
    }
  }
  return dst;
}

void Lowerer::lowerReturn(Expr *value, SourceRange r) {
  if (value) {
    PlaceId v = lowerPlace(value);
    if (v == kNone)
      v = lower(value);
    if (Dead)
      return;
    if (v != kNone && ReturnPlace != kNone)
      assignInto(ReturnPlace, v, value, value->Range);
  }
  endStatementTemps();
  unwindTo(0);
  if (!Dead) {
    cur().Term.K = Terminator::Return;
    cur().Term.Range = r;
    B.Exits.push_back(Cur);
  }
  startDeadBlock();
}

//===----------------------------------------------------------------------===//
// Patterns
//===----------------------------------------------------------------------===//

PlaceId Lowerer::scrutineePlace(Expr *e, bool &owned, bool &mutableThrough) {
  mutableThrough = false;
  owned = true;
  if (!e)
    return kNone;
  // `match &var list.head { ... }` looks at `list.head`; the borrow says how
  // exclusively, and there is no separate reference to reach through.
  bool borrowed = false, borrowMutable = false;
  Expr *inner = e;
  while (auto *b = dyn_cast<BorrowExpr>(inner)) {
    borrowed = true;
    borrowMutable = b->IsMutable;
    inner = b->Operand.get();
  }
  PlaceId scrut = lowerPlace(inner);
  if (scrut == kNone) {
    // Not a place: a value of the match's own, lowered as written.
    return lower(e);
  }
  owned = false;
  if (isUntracked(scrut))
    return scrut;
  if (borrowed) {
    mutableThrough = borrowMutable;
    return scrut;
  }
  // A reference the scrutinee already is: matched through it.
  Type *ty = inner->Ty;
  bool any = false, allMutable = true;
  while (ty && ty->is(TypeKind::Pointer)) {
    if (ty->isRawPointer() || ty->isWeakPointer())
      return untracked();
    allMutable = allMutable && ty->isMutablePointer();
    any = true;
    scrut = project(scrut, Projection::Deref);
    ty = ty->pointee();
  }
  mutableThrough = any && allMutable;
  return scrut;
}

void Lowerer::bindPattern(Pattern *p, PlaceId src, bool owned, SourceRange r,
                          bool mutableAlias) {
  if (!p)
    return;
  switch (p->Kind) {
  case NodeKind::BindingPat: {
    auto *bp = cast<BindingPattern>(p);
    if (bp->isVariantTest() || !bp->Binding)
      return;
    LocalId l = localFor(bp->Binding, bp->Range);
    Local &loc = B.Locals[l];
    Stmt live;
    live.K = Stmt::StorageLive;
    live.Dst = place(l);
    live.Range = bp->Range;
    emit(std::move(live));
    declareIn(l);
    if (src == kNone || isUntracked(src)) {
      // A constant, or something the checker does not follow: the binding
      // simply exists.
      Stmt s;
      s.K = Stmt::Assign;
      s.Dst = place(l);
      s.Range = bp->Range;
      PlaceAccess w;
      w.Place = s.Dst;
      w.A = Access::Write;
      w.Range = bp->Range;
      s.Accesses.push_back(w);
      emit(std::move(s));
      if (bp->Sub)
        bindPattern(bp->Sub.get(), src, owned, r, mutableAlias);
      return;
    }
    Type *st = typeOf(src);
    bool trivially = st && !movesWhenUsed(st);
    if (owned || trivially || bp->ByRef) {
      if (bp->ByRef) {
        borrowInto(place(l), src, bp->IsMutable, false, nullptr, bp->Range);
      } else {
        assignInto(place(l), src, nullptr, bp->Range);
      }
    } else {
      // Out of borrowed content: an alias. The binding holds the value
      // where it is, as a borrow of the scrutinee place — exclusive when the
      // scrutinee was reached through a `&var`, which is what lets a walk
      // write as it goes. Codegen reads the flag: an alias is never dropped
      // and never emptied.
      loc.RefLike = true;
      loc.RefMutable = mutableAlias;
      loc.Owned = false;
      bp->Binding->ZombieAlias = true;
      if (loc.Origin == kNone) {
        Origin o;
        o.K = Origin::Local;
        o.Owner = l;
        loc.Origin = static_cast<OriginId>(B.Origins.size());
        B.Origins.push_back(o);
      }
      borrowInto(place(l), src, mutableAlias, false, nullptr, bp->Range);
    }
    if (bp->Sub)
      bindPattern(bp->Sub.get(), src, owned, r, mutableAlias);
    return;
  }
  case NodeKind::WildcardPat:
  case NodeKind::LiteralPat:
  case NodeKind::PathPat:
  case NodeKind::RangePat:
    return;
  case NodeKind::RefPat: {
    auto *rp = cast<RefPattern>(p);
    // `&pat` matches through the reference: what follows is borrowed.
    PlaceId inner = src;
    Type *st = typeOf(src);
    if (st && isTrackedRef(st))
      inner = project(src, Projection::Deref);
    bindPattern(rp->Sub.get(), inner, /*owned=*/false, r, mutableAlias);
    return;
  }
  case NodeKind::TuplePat: {
    auto *tp = cast<TuplePattern>(p);
    for (size_t i = 0; i < tp->Elements.size(); ++i) {
      PlaceId el = src == kNone ? kNone
                                : project(src, Projection::Field,
                                          static_cast<uint32_t>(i));
      bindPattern(tp->Elements[i].get(), el, owned, r, mutableAlias);
    }
    return;
  }
  case NodeKind::StructPat: {
    auto *sp = cast<StructPattern>(p);
    PlaceId base = src;
    if (src != kNone && sp->VariantIndex >= 0)
      base = project(src, Projection::Variant,
                     static_cast<uint32_t>(sp->VariantIndex));
    for (auto &f : sp->Fields) {
      PlaceId fp = base == kNone ? kNone
                                 : project(base, Projection::Field, f.FieldIndex);
      bindPattern(f.Value.get(), fp, owned, r, mutableAlias);
    }
    return;
  }
  case NodeKind::EnumPat: {
    auto *ep = cast<EnumPattern>(p);
    PlaceId base = src;
    if (src != kNone && ep->VariantIndex >= 0)
      base = project(src, Projection::Variant,
                     static_cast<uint32_t>(ep->VariantIndex));
    for (size_t i = 0; i < ep->Elements.size(); ++i) {
      PlaceId fp = base == kNone ? kNone
                                 : project(base, Projection::Field,
                                           static_cast<uint32_t>(i));
      bindPattern(ep->Elements[i].get(), fp, owned, r, mutableAlias);
    }
    return;
  }
  case NodeKind::OrPat: {
    // Each alternative binds the same names; the first says how.
    auto *op = cast<OrPattern>(p);
    if (!op->Alternatives.empty())
      bindPattern(op->Alternatives[0].get(), src, owned, r, mutableAlias);
    return;
  }
  case NodeKind::SlicePat: {
    auto *sp = cast<SlicePattern>(p);
    PlaceId el = src == kNone ? kNone : project(src, Projection::Index);
    for (auto &e : sp->Prefix)
      bindPattern(e.get(), el, owned, r, mutableAlias);
    for (auto &e : sp->Suffix)
      bindPattern(e.get(), el, owned, r, mutableAlias);
    if (sp->Rest) {
      // `..rest` is a slice of the middle: a borrow of the elements.
      if (auto *bp = dyn_cast<BindingPattern>(sp->Rest.get()))
        if (bp->Binding && src != kNone && !isUntracked(src)) {
          LocalId l = localFor(bp->Binding, bp->Range);
          Stmt live;
          live.K = Stmt::StorageLive;
          live.Dst = place(l);
          live.Range = bp->Range;
          emit(std::move(live));
          declareIn(l);
          borrowInto(place(l), el, false, false, nullptr, bp->Range);
        }
    }
    return;
  }
  default:
    return;
  }
}

} // namespace

//===----------------------------------------------------------------------===//
// Entry
//===----------------------------------------------------------------------===//

std::unique_ptr<Body> lowerBody(FunctionDecl *fn, DiagnosticEngine &diags) {
  auto body = std::make_unique<Body>();
  Lowerer l(*body, diags);
  l.lowerFunction(fn);
  return body;
}

} // namespace zombie
} // namespace rune
