//===- Ownership.cpp - Borrow and ownership checking ------------*- C++ -*-===//
//
// See Ownership.h for what this pass answers and why. The implementation is
// one walk over a function body, carrying two pieces of state:
//
//   * the borrows currently live, each with the local it points into and the
//     last place its binding is mentioned, which is where it stops mattering
//   * the set of locals something has taken away from this scope
//
// Everything is keyed on a *root local*: `&var v.field[i]` borrows `v`, and
// that is the granularity conflicts are reported at. Finer would need to
// follow indices the compiler cannot evaluate, and would report the same
// conflicts with less certainty.
//
//===----------------------------------------------------------------------===//
#include "rune/Ownership.h"

#include "rune/ASTWalk.h"
#include "rune/Type.h"

#include <functional>
#include <map>
#include <set>
#include <vector>

namespace rune {

namespace {

/// The local a place expression is rooted at, or null when it is not rooted
/// at one — a global, a call's result, a literal.
///
/// `v.field`, `v[i]`, `*p` and `&v` all have the same root as `v` does, which
/// is what makes a conflict between `&var v.a` and `&v.b` visible at all. It
/// also makes the check coarser than it has to be; the note on a conflict
/// says as much, so the reader is not left guessing why two different fields
/// collided.
VarDecl *rootLocal(const Expr *e) {
  while (e) {
    switch (e->Kind) {
    case NodeKind::DeclRef: {
      const auto *r = cast<DeclRefExpr>(e);
      auto *v = r->Resolved ? dyn_cast<VarDecl>(r->Resolved) : nullptr;
      return v && !v->IsGlobal ? v : nullptr;
    }
    case NodeKind::Member: e = cast<MemberExpr>(e)->Base.get(); continue;
    case NodeKind::Index: {
      // A slice is a view: its elements live in whatever buffer it looks
      // at, not in the local holding the view.
      const Expr *base = cast<IndexExpr>(e)->Base.get();
      if (base->Ty && base->Ty->is(TypeKind::Slice))
        return nullptr;
      e = base;
      continue;
    }
    case NodeKind::Borrow: e = cast<BorrowExpr>(e)->Operand.get(); continue;
    case NodeKind::Deref:  e = cast<DerefExpr>(e)->Operand.get(); continue;
    default:
      return nullptr;
    }
  }
  return nullptr;
}

/// How a place is spelled, for a diagnostic: `v`, `v.field`, `v[…]`.
std::string spell(const Expr *e) {
  if (!e)
    return "it";
  switch (e->Kind) {
  case NodeKind::DeclRef: return cast<DeclRefExpr>(e)->joined();
  case NodeKind::Member: {
    const auto *m = cast<MemberExpr>(e);
    return spell(m->Base.get()) + "." + m->Name;
  }
  case NodeKind::Index:  return spell(cast<IndexExpr>(e)->Base.get()) + "[…]";
  case NodeKind::Borrow: return spell(cast<BorrowExpr>(e)->Operand.get());
  case NodeKind::Deref:  return "*" + spell(cast<DerefExpr>(e)->Operand.get());
  default: return "it";
  }
}

/// True for `&T` / `&var T`: a borrow the compiler tracks. A raw pointer is
/// not one — that is what `unsafe` is for, and following it is exactly what
/// this pass does not claim to do.
bool isTrackedBorrow(Type *t) {
  return t && t->is(TypeKind::Pointer) && !t->isRawPointer() &&
         !t->isWeakPointer();
}

/// One borrow that is currently live.
struct LiveBorrow {
  VarDecl *Binding = nullptr;   ///< the local holding the borrow
  VarDecl *Root = nullptr;      ///< the local it points into
  bool Mutable = false;
  SourceRange TakenAt;
  std::string Place;            ///< how the borrowed place was written
  /// Offset of the last mention of `Binding`. A borrow stops mattering after
  /// its final use, not at the end of the block — which is what lets a
  /// function take a `&var`, finish with it, and then read the value again.
  unsigned LastUse = 0;
};

class OwnershipChecker {
public:
  OwnershipChecker(FunctionDecl *fn, DiagnosticEngine &diags,
                   SafetyLevel safety)
      : Fn(fn), Diags(diags), Safety(safety) {}

  void run() {
    if (!Fn->Body)
      return;
    // Parameters are the caller's; nothing this body does can free them.
    for (const Param &p : Fn->Params)
      if (p.Binding)
        Parameters.insert(p.Binding);

    collectLastUses(Fn->Body.get());
    walk(Fn->Body.get());
    checkConsumedByValue(Fn->Body->Tail.get());
    checkResult(Fn->Body->Tail.get());

    // Whatever was never taken away stays here. A local the walk never even
    // saw declared is not marked: the pass only claims what it followed.
    for (VarDecl *v : Declared)
      if (!Escaped.count(v))
        v->NoEscape = true;
  }

private:
  FunctionDecl *Fn;
  DiagnosticEngine &Diags;
  SafetyLevel Safety;

  std::set<VarDecl *> Parameters;
  std::set<VarDecl *> Declared;
  std::set<VarDecl *> Escaped;
  /// The last offset at which each local is mentioned anywhere in the body.
  std::map<VarDecl *, unsigned> LastUse;
  std::vector<LiveBorrow> Borrows;

  //=== Reporting ========================================================//

  /// True when a finding is worth saying at this safety level. `--safety
  /// none` asks for no checking at all; the walk still runs, because the
  /// escape analysis it produces is what the code generator reads, and that
  /// must not depend on how much reporting was asked for.
  bool reports() const { return Safety != SafetyLevel::None; }

  DiagBuilder report(SourceRange at, const std::string &text) {
    // Below `--safety full` the program is taken at its word: the finding is
    // still worth saying, but it does not stop the build.
    return Safety == SafetyLevel::Full ? Diags.error(at, "{}", text)
                                       : Diags.warn(at, "{}", text);
  }

  //=== Last-use scan ====================================================//

  void collectLastUses(const Node *n) {
    if (!n)
      return;
    if (const auto *r = dyn_cast<DeclRefExpr>(n))
      if (auto *v = r->Resolved ? dyn_cast<VarDecl>(r->Resolved) : nullptr) {
        unsigned at = r->Range.end().raw();
        auto it = LastUse.find(v);
        if (it == LastUse.end() || it->second < at)
          LastUse[v] = at;
      }
    forEachChild(n, [&](const Node *c) { collectLastUses(c); });
  }

  //=== Escape marking ===================================================//

  /// Records that whatever `e` names has been taken out of this scope. The
  /// walk follows places, so storing `v.field` counts as taking `v`: the
  /// field's new owner can outlive the binding either way.
  void markEscaped(const Expr *e) {
    if (VarDecl *v = rootLocal(e))
      Escaped.insert(v);
    // A borrow handed away takes what it points at with it.
    if (!e)
      return;
    if (const auto *b = dyn_cast<BorrowExpr>(e))
      markEscaped(b->Operand.get());
  }

  /// Everything mentioned anywhere inside `e` escapes. Used where the walk
  /// cannot say what a construct does with what it is given — a closure body,
  /// a raw-pointer cast — and guessing would be worse than assuming the most.
  void markEverythingEscaped(const Node *n) {
    if (!n)
      return;
    if (const auto *r = dyn_cast<DeclRefExpr>(n))
      if (auto *v = r->Resolved ? dyn_cast<VarDecl>(r->Resolved) : nullptr)
        Escaped.insert(v);
    forEachChild(n, [&](const Node *c) { markEverythingEscaped(c); });
  }

  //=== Borrows ==========================================================//

  void retireBorrows(unsigned at) {
    for (size_t i = Borrows.size(); i > 0; --i)
      if (Borrows[i - 1].LastUse < at)
        Borrows.erase(Borrows.begin() + static_cast<long>(i - 1));
  }

  /// Reports when taking `e` as a borrow — or writing through it — clashes
  /// with one that is already live.
  void checkBorrowConflict(const Expr *place, bool wantMutable,
                           SourceRange at, const char *what) {
    VarDecl *root = rootLocal(place);
    if (!root)
      return;
    if (!reports())
      return;
    for (const LiveBorrow &b : Borrows) {
      if (b.Root != root)
        continue;
      if (!b.Mutable && !wantMutable)
        continue;                       // two readers never disagree
      if (b.TakenAt.begin() == at.begin())
        continue;                       // this very borrow
      auto d = report(at, fmt("cannot {} '{}' while it is borrowed", what,
                              spell(place)));
      d.note(b.Mutable
                 ? "the live borrow can write through it, so nothing else may "
                   "reach the value while it lasts"
                 : "a borrow that can write has to be the only one");
      d.related(b.TakenAt, "the borrow is taken here",
                fmt("finish with '{}' before this line, or take this one as "
                    "`&` instead", b.Place));
      d.note("a borrow is followed to the binding it starts from, so two "
             "fields of the same value count as the same place");
      d.code(270);
      return;
    }
  }

  //=== The walk =========================================================//

  void walk(const Node *n) {
    if (!n)
      return;
    switch (n->Kind) {
    case NodeKind::Block: {
      const auto *b = cast<BlockExpr>(n);
      for (const auto &st : b->Stmts)
        walk(st.get());
      walk(b->Tail.get());
      return;
    }

    case NodeKind::VarStmt: {
      const auto *v = cast<VarStmtNode>(n);
      walk(v->Init.get());
      noteDeclaration(v->Binding.get(), v->Init.get());
      return;
    }

    case NodeKind::Assign: {
      const auto *a = cast<AssignExpr>(n);
      walk(a->RHS.get());
      // Writing to a place that something else is reading through is the
      // other half of the conflict rule.
      retireBorrows(a->Range.begin().raw());
      checkBorrowConflict(a->LHS.get(), /*wantMutable=*/true, a->OpRange,
                          "assign to");
      // The right-hand side ends up somewhere that may outlive this scope
      // unless the destination is a local of this scope.
      if (!rootLocal(a->LHS.get()) || a->LHS->Kind != NodeKind::DeclRef)
        markEscaped(a->RHS.get());
      checkConsumedByValue(a->RHS.get());
      if (a->DeclaresBinding && a->DeclaredVar) {
        Declared.insert(a->DeclaredVar);
        noteBorrowBinding(a->DeclaredVar, a->RHS.get());
      }
      walk(a->LHS.get());
      return;
    }

    case NodeKind::Borrow: {
      const auto *b = cast<BorrowExpr>(n);
      retireBorrows(b->Range.begin().raw());
      checkBorrowConflict(b->Operand.get(), b->IsMutable, b->Range,
                          b->IsMutable ? "borrow" : "borrow");
      walk(b->Operand.get());
      return;
    }

    case NodeKind::Return: {
      const auto *r = cast<ReturnExpr>(n);
      walk(r->Value.get());
      markEscaped(r->Value.get());
      checkConsumedByValue(r->Value.get());
      checkResult(r->Value.get());
      return;
    }

    case NodeKind::Call: {
      const auto *c = cast<CallExpr>(n);
      walk(c->Callee.get());
      for (const auto &a : c->Args) {
        walk(a.Value.get());
        // A borrow handed to a call is the caller's business only for as long
        // as the call lasts, so it takes nothing away. Anything else does:
        // the callee may keep it.
        if (!isTrackedBorrow(a.Value->Ty)) {
          markEscaped(a.Value.get());
          checkConsumedByValue(a.Value.get());
        }
      }
      return;
    }

    case NodeKind::Closure:
      // A closure outlives the expression that made it, and what it captured
      // goes with it.
      markEverythingEscaped(cast<ClosureExpr>(n)->Body.get());
      return;

    case NodeKind::Cast: {
      const auto *c = cast<CastExpr>(n);
      // Casting to a raw pointer hands the address somewhere this pass cannot
      // follow.
      if (c->Ty && c->Ty->is(TypeKind::Pointer) && c->Ty->isRawPointer())
        markEverythingEscaped(c->Operand.get());
      walk(c->Operand.get());
      return;
    }

    case NodeKind::StructLit: {
      const auto *s = cast<StructLitExpr>(n);
      for (const auto &f : s->Fields) {
        walk(f.Value.get());
        markEscaped(f.Value.get());
        checkConsumedByValue(f.Value.get());
      }
      walk(s->Base.get());
      return;
    }

    case NodeKind::ArrayLit: {
      const auto *a = cast<ArrayLitExpr>(n);
      for (const auto &e : a->Elements) {
        walk(e.get());
        markEscaped(e.get());
        checkConsumedByValue(e.get());
      }
      walk(a->RepeatCount.get());
      return;
    }

    case NodeKind::TupleLit: {
      for (const auto &e : cast<TupleLitExpr>(n)->Elements) {
        walk(e.get());
        markEscaped(e.get());
        checkConsumedByValue(e.get());
      }
      return;
    }


    default:
      forEachChild(n, [&](const Node *c) { walk(c); });
      return;
    }
  }

  /// A binding that was just declared: record it, and note it when what it
  /// holds is a borrow.
  void noteDeclaration(const Pattern *pat, const Expr *init) {
    const auto *bp = dyn_cast<BindingPattern>(pat);
    if (!bp || !bp->Binding)
      return;
    Declared.insert(bp->Binding);
    noteBorrowBinding(bp->Binding, init);
  }

  void noteBorrowBinding(VarDecl *binding, const Expr *init) {
    if (!binding || !init || !isTrackedBorrow(binding->Ty))
      return;
    VarDecl *root = rootLocal(init);
    if (!root)
      return;
    LiveBorrow b;
    b.Binding = binding;
    b.Root = root;
    b.Mutable = binding->Ty->isMutablePointer();
    b.TakenAt = init->Range;
    b.Place = spell(init);
    auto it = LastUse.find(binding);
    b.LastUse = it == LastUse.end() ? init->Range.end().raw() : it->second;
    Borrows.push_back(b);
  }

  /// A value that owns something a reference count cannot see must not be
  /// copied out of a place that goes on holding it: two owners means the
  /// resource is handed back twice.
  ///
  /// Only a *place inside something else* is reported. A plain local handed
  /// away is a move, which the type checker already tracks and which leaves
  /// the binding empty rather than making a second owner.
  void checkConsumedByValue(const Expr *e) {
    if (!reports() || !e || !ownsResources(e->Ty))
      return;
    if (e->Kind != NodeKind::Member && e->Kind != NodeKind::Index)
      return;
    auto d = report(e->Range,
                    fmt("'{}' owns a resource, so taking it out of '{}' by "
                        "value would make a second owner",
                        e->Ty->toString(), spell(e)));
    d.note("borrow it with `&`, or take only the part of it you need");
    d.note("a value with a `deinit` hands something back when it is "
           "destroyed, and doing that twice is what this prevents");
    d.code(271);
  }

  static bool ownsResources(Type *t) {
    std::set<Type *> seen;
    return ownsResources(t, seen);
  }
  static bool ownsResources(Type *t, std::set<Type *> &seen) {
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
      for (const auto &f : nd->Fields)
        if (ownsResources(f->Ty, seen))
          return true;
      return false;
    }
    case TypeKind::Array: return ownsResources(t->element(), seen);
    case TypeKind::Tuple:
      for (Type *e : t->tupleElements())
        if (ownsResources(e, seen))
          return true;
      return false;
    default:
      return false;
    }
  }

  //=== Escaping borrows =================================================//

  /// Reports a result that borrows something this call is about to destroy.
  void checkResult(const Expr *value) {
    if (!value || !Fn->Ty)
      return;
    Type *ret = Fn->Ty->result();
    if (!isTrackedBorrow(ret))
      return;
    VarDecl *root = borrowedRoot(value);
    if (!root || Parameters.count(root) || !reports())
      return;                       // a parameter belongs to the caller
    auto d = report(value->Range,
                    fmt("this returns a borrow of '{}', which does not "
                        "outlive the call", root->Name));
    d.note("the binding is destroyed when this function returns, so the "
           "caller would be handed an address to nothing");
    d.related(root->Range, "declared here",
              "return the value itself, or take what is borrowed as a "
              "parameter so the caller owns it");
    d.code(272);
  }

  /// The local a returned borrow ultimately points into, following borrow
  /// bindings one step: `let p = &v; p` points into `v`.
  VarDecl *borrowedRoot(const Expr *e) {
    if (!e)
      return nullptr;
    if (const auto *b = dyn_cast<BorrowExpr>(e))
      return rootLocal(b->Operand.get());
    if (const auto *r = dyn_cast<DeclRefExpr>(e)) {
      auto *v = r->Resolved ? dyn_cast<VarDecl>(r->Resolved) : nullptr;
      if (!v || !isTrackedBorrow(v->Ty))
        return nullptr;
      for (const LiveBorrow &b : Borrows)
        if (b.Binding == v)
          return b.Root;
      return nullptr;
    }
    return nullptr;
  }
};

} // namespace

void checkOwnership(FunctionDecl *fn, DiagnosticEngine &diags,
                    SafetyLevel safety) {
  if (!fn || !fn->Body)
    return;
  OwnershipChecker(fn, diags, safety).run();
}

} // namespace rune
