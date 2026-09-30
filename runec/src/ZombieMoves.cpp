//===- ZombieMoves.cpp - Moves, initialisation and drops --------*- C++ -*-===//
//
// Under Zombie an owned value has one owner, and handing it on empties the
// place it was in. This pass follows that emptiness through the graph: which
// places may be uninitialised at each point, and so which uses are uses of
// nothing (E0273), which moves take something out of somewhere it cannot be
// taken from (E0274), and — for the code generator — which drops at scope end
// are certain, impossible, or need a flag.
//
// The unit is a *move path*: a place that is moved as a whole somewhere in
// the body, or contains one. Two bit sets per block entry, may-initialised
// and may-uninitialised, are enough to tell "certainly gone" from "gone on
// some path", which is the difference between an error and a drop flag.
//
//===----------------------------------------------------------------------===//
#include "rune/Zombie.h"
#include "rune/ZombieIR.h"
#include "rune/ZombieInternal.h"

#include <deque>

namespace rune {
namespace zombie {

namespace {

class MoveAnalysis {
public:
  MoveAnalysis(Body &body, DiagnosticEngine &diags, MoveResults &out)
      : B(body), Diags(diags), Out(out) {}
  void run();

private:
  Body &B;
  DiagnosticEngine &Diags;
  MoveResults &Out;

  /// Every place that is a move path, numbered; `PathOf[place]` maps back.
  std::vector<PlaceId> Paths;
  std::unordered_map<PlaceId, PathId> PathOf;
  /// Per path: the paths under it (itself included) and its proper prefixes.
  std::vector<std::vector<PathId>> Subtree;
  std::vector<std::vector<PathId>> Prefixes;
  /// Where each path is moved, for the "moved here" note.
  std::vector<std::vector<std::pair<Location, SourceRange>>> MovedAt;

  std::vector<BitSet> InitIn, UninitIn;

  void collectPaths();
  PathId pathFor(PlaceId p);
  void transfer(const Stmt &s, BitSet &init, BitSet &uninit);
  void check(const Stmt &s, Location at, const BitSet &init,
             const BitSet &uninit);
  void reportUseOfMoved(const PlaceAccess &a, PathId moved, Location at,
                        bool certain, bool partial);
  bool reaches(BlockId from, BlockId to);
  /// Reachability starting from `from`'s successors: a path that has to
  /// leave the block first.
  bool reachesAfter(BlockId from, BlockId to);
  void planDrops();
  void checkMoveOutOf(const Stmt &s, const PlaceAccess &a);
};

PathId MoveAnalysis::pathFor(PlaceId p) {
  auto it = PathOf.find(p);
  return it == PathOf.end() ? kNone : it->second;
}

void MoveAnalysis::collectPaths() {
  auto add = [&](PlaceId p) {
    if (p == kNone || PathOf.count(p))
      return;
    // Only what is owned outright is a path: nothing behind a borrow, no
    // element of an array (which one is not known).
    const Place &pl = B.Places.get(p);
    for (const Projection &pr : pl.Proj)
      if (pr.K == Projection::Deref || pr.K == Projection::Index)
        return;
    PathOf[p] = static_cast<PathId>(Paths.size());
    Paths.push_back(p);
  };
  // Every owned local is a path, so its drop can be planned.
  for (LocalId l = 0; l < B.Locals.size(); ++l)
    if (B.Locals[l].Owned || B.Locals[l].K == Local::Param)
      add(B.Places.intern(Place{l, {}}));
  for (const Block &blk : B.Blocks)
    for (const Stmt &s : blk.Stmts)
      for (const PlaceAccess &a : s.Accesses)
        if (a.A == Access::Move || a.A == Access::Write)
          add(a.Place);
  // Prefixes of every path are paths too: moving `s.a` leaves `s` partly
  // moved, which is a state `s` has to be able to be in.
  for (size_t i = 0; i < Paths.size(); ++i) {
    PlaceId p = Paths[i];
    for (PlaceId q = B.Places.parent(p); q != kNone; q = B.Places.parent(q))
      add(q);
  }
  Subtree.assign(Paths.size(), {});
  Prefixes.assign(Paths.size(), {});
  for (PathId i = 0; i < Paths.size(); ++i)
    for (PathId j = 0; j < Paths.size(); ++j) {
      if (B.Places.isPrefixOf(Paths[i], Paths[j]))
        Subtree[i].push_back(j);
      if (i != j && B.Places.isPrefixOf(Paths[j], Paths[i]))
        Prefixes[i].push_back(j);
    }
  MovedAt.assign(Paths.size(), {});
}

void MoveAnalysis::transfer(const Stmt &s, BitSet &init, BitSet &uninit) {
  for (const PlaceAccess &a : s.Accesses) {
    PathId p = pathFor(a.Place);
    switch (a.A) {
    case Access::Move:
      if (p == kNone)
        break;
      for (PathId q : Subtree[p]) {
        init.reset(q);
        uninit.set(q);
      }
      break;
    case Access::Write:
      if (p == kNone)
        break;
      for (PathId q : Subtree[p]) {
        init.set(q);
        uninit.reset(q);
      }
      break;
    case Access::Drop:
    case Access::StorageDead:
      if (p == kNone)
        break;
      for (PathId q : Subtree[p]) {
        init.reset(q);
        uninit.set(q);
      }
      break;
    default:
      break;
    }
  }
  if (s.K == Stmt::StorageLive) {
    PathId p = pathFor(s.Dst);
    if (p != kNone)
      for (PathId q : Subtree[p]) {
        init.reset(q);
        uninit.set(q);
      }
  }
}

bool MoveAnalysis::reaches(BlockId from, BlockId to) {
  std::vector<bool> seen(B.Blocks.size(), false);
  std::deque<BlockId> work{from};
  while (!work.empty()) {
    BlockId b = work.front();
    work.pop_front();
    if (b == to)
      return true;
    if (seen[b])
      continue;
    seen[b] = true;
    for (BlockId succ : B.Blocks[b].Term.Succ)
      work.push_back(succ);
  }
  return false;
}

bool MoveAnalysis::reachesAfter(BlockId from, BlockId to) {
  for (BlockId succ : B.Blocks[from].Term.Succ)
    if (reaches(succ, to))
      return true;
  return false;
}

void MoveAnalysis::reportUseOfMoved(const PlaceAccess &a, PathId moved,
                                    Location at, bool certain, bool partial) {
  std::string place = B.spell(a.Place);
  std::string movedPlace = B.spell(Paths[moved]);
  DiagBuilder d =
      partial ? Diags.error(a.Range,
                            "'{}' cannot be used whole: '{}' was moved out "
                            "of it",
                            place, movedPlace)
      : place == movedPlace
          ? Diags.error(a.Range, "'{}' has been moved out of", place)
          : Diags.error(a.Range, "cannot use '{}': '{}' has been moved out of",
                        place, movedPlace);
  // The move that did it: one that reaches this use. When the use can in
  // turn reach the move, the move is on a loop's next turn.
  const std::pair<Location, SourceRange> *pick = nullptr;
  bool loop = false;
  for (const auto &m : MovedAt[moved]) {
    bool before = m.first.Block == at.Block ? m.first.Index < at.Index
                                            : reaches(m.first.Block, at.Block);
    bool after = reachesAfter(at.Block, m.first.Block) &&
                 (m.first.Block != at.Block || m.first.Index <= at.Index);
    if (!before && !after)
      continue;
    if (!pick || (before && !loop)) {
      pick = &m;
      loop = !before || (after && m.first.Block == at.Block);
      if (before && after)
        loop = false;
    }
  }
  if (!certain)
    d.note("it is moved on some paths that reach here and not on others; "
           "the checker has to assume the worst");
  if (loop)
    d.note("the move happens on every turn of the loop, and the next one "
           "finds nothing there");
  d.note("under `--memory zombie` a class, a `String` or a closure has one "
         "owner: handing it on empties the place it was in");
  if (pick)
    d.related(pick->second, "moved here",
              "clone it with `$clone()` before the move, or borrow it with "
              "`&` instead");
  d.code(partial ? 277 : 273);
}

void MoveAnalysis::check(const Stmt &s, Location at, const BitSet &init,
                         const BitSet &uninit) {
  for (const PlaceAccess &a : s.Accesses) {
    if (a.A == Access::Write || a.A == Access::StorageDead ||
        a.A == Access::Drop || a.A == Access::Activate)
      continue;
    if (a.Place == kNone)
      continue;
    // The path this access is of, or the nearest path above it.
    PathId p = pathFor(a.Place);
    PlaceId probe = a.Place;
    while (p == kNone && probe != kNone) {
      probe = B.Places.parent(probe);
      if (probe != kNone)
        p = pathFor(probe);
    }
    if (p == kNone)
      continue;
    // Something containing this place, or the place itself, may be gone.
    PathId gone = kNone;
    if (uninit.test(p))
      gone = p;
    else
      for (PathId q : Prefixes[p])
        if (uninit.test(q)) {
          gone = q;
          break;
        }
    if (gone != kNone) {
      reportUseOfMoved(a, gone, at, !init.test(gone), false);
      continue;
    }
    // Or a part of it may be, which matters when the whole is wanted.
    if (probe == a.Place && (a.A == Access::Move || a.A == Access::Read ||
                             a.A == Access::Shared || a.A == Access::Mut ||
                             a.A == Access::Reserve)) {
      for (PathId q : Subtree[p]) {
        if (q == p)
          continue;
        if (uninit.test(q)) {
          reportUseOfMoved(a, q, at, !init.test(q), true);
          break;
        }
      }
    }
  }
}

/// A move that takes a value out of somewhere it cannot be taken from.
void MoveAnalysis::checkMoveOutOf(const Stmt &s, const PlaceAccess &a) {
  (void)s;
  const Place &pl = B.Places.get(a.Place);
  const Local &root = B.Locals[pl.Root];
  Type *t = placeType(B, a.Place);
  bool throughRef = false, throughHandle = false, indexed = false;
  {
    Type *cur = root.Ty;
    if (root.RefLike && !pl.Proj.empty() && pl.Proj[0].K == Projection::Deref)
      throughRef = true;
    for (size_t i = 0; i < pl.Proj.size(); ++i) {
      const Projection &pr = pl.Proj[i];
      if (pr.K == Projection::Deref) {
        if (cur && cur->is(TypeKind::Pointer))
          throughRef = true;
        else if (cur && cur->is(TypeKind::Class))
          throughHandle = true;
        else if (!cur)
          throughRef = true;
      }
      if (pr.K == Projection::Index)
        indexed = true;
      PlaceId prefix = B.Places.intern(Place{pl.Root, std::vector<Projection>(
                                                        pl.Proj.begin(),
                                                        pl.Proj.begin() + i + 1)});
      cur = placeType(B, prefix);
    }
  }
  bool whole = pl.Proj.empty();
  if (root.K == Local::Global && !throughRef && !throughHandle) {
    auto d = Diags.error(a.Range, "cannot move '{}' out of a global",
                         B.spell(a.Place));
    d.note("a global is reached from every function, so taking its value "
           "would leave all of them holding nothing");
    d.note("borrow it with `&`, copy it with `$clone()`, or swap something "
           "in with `mem::replace`");
    d.code(274);
    return;
  }
  if (whole && root.RefLike) {
    auto d = Diags.error(a.Range, "cannot move '{}' out of a borrow",
                         B.spell(a.Place));
    d.note("the binding only borrows the value; it is owned somewhere else");
    d.note("borrow it with `&`, or copy it with `$clone()`");
    d.code(274);
    return;
  }
  if (throughRef) {
    auto d = Diags.error(a.Range, "cannot move '{}' out of a borrow",
                         B.spell(a.Place));
    d.note("what a borrow reaches is owned elsewhere, and taking it would "
           "leave that owner holding nothing");
    d.note("borrow it with `&`, copy it with `$clone()`, or take it with "
           "`mem::take` leaving something behind");
    d.code(274);
    return;
  }
  if (throughHandle) {
    auto d = Diags.error(a.Range, "cannot move '{}' out of a class",
                         B.spell(a.Place));
    d.note("an object's fields stay with the object; the field would be "
           "left holding nothing");
    d.note("borrow it with `&`, copy it with `$clone()`, or replace it with "
           "`mem::take`");
    d.code(274);
    return;
  }
  if (indexed) {
    auto d = Diags.error(a.Range, "cannot move '{}' out of an array",
                         B.spell(a.Place));
    d.note("which element it is cannot be known here, so nothing could "
           "remember that this one is gone");
    d.note("borrow it with `&`, copy it with `$clone()`, or take it with "
           "`mem::take`");
    d.code(274);
    return;
  }
  // Out of a value with a destructor: the destructor expects the whole.
  if (!whole) {
    PlaceId owner = B.Places.parent(a.Place);
    while (owner != kNone) {
      Type *ot = placeType(B, owner);
      NominalDecl *nd = ot && ot->isNominal() ? ot->nominal() : nullptr;
      if (nd && nd->Deinit) {
        auto d = Diags.error(a.Range,
                             "cannot move '{}' out of a value with a "
                             "`deinit`",
                             B.spell(a.Place));
        d.note("'{}' hands something back when it is destroyed, and its "
               "`deinit` expects to find every field there",
               ot->toString());
        d.note("borrow it with `&`, copy it with `$clone()`, or take it "
               "with `mem::take`");
        d.code(274);
        return;
      }
      owner = B.Places.parent(owner);
    }
  }
  (void)t;
}

void MoveAnalysis::planDrops() {
  // One plan per owned local: over every drop of it, what state it is in.
  for (BlockId bi = 0; bi < B.Blocks.size(); ++bi) {
    BitSet init = InitIn[bi], uninit = UninitIn[bi];
    for (Stmt &s : B.Blocks[bi].Stmts) {
      if (s.K == Stmt::Drop && s.Dst != kNone) {
        PathId whole = pathFor(s.Dst);
        if (whole != kNone && uninit.test(whole) && !init.test(whole))
          s.DropElided = true;
      }
      if (s.K == Stmt::Drop && s.Dst != kNone) {
        const Place &pl = B.Places.get(s.Dst);
        const Local &l = B.Locals[pl.Root];
        PathId p = pathFor(s.Dst);
        if (p != kNone && l.Var) {
          DropPlan &plan = Out.Drops[l.Var];
          // Paths under the local that are moved somewhere are planned
          // one by one; a local never taken apart is planned whole.
          for (PathId q : Subtree[p]) {
            bool leaf = true;
            for (PathId r : Subtree[q])
              if (r != q) { leaf = false; break; }
            if (!leaf && q != p)
              continue;
            if (!leaf && q == p && Subtree[p].size() > 1)
              continue;
            DropPlan::Kind k;
            if (uninit.test(q))
              k = init.test(q) ? DropPlan::Flagged : DropPlan::Never;
            else
              k = DropPlan::Always;
            const Place &qp = B.Places.get(Paths[q]);
            bool found = false;
            for (DropPlan::Path &existing : plan.Paths)
              if (existing.Proj == qp.Proj) {
                found = true;
                if (existing.K != k)
                  existing.K = DropPlan::Flagged;
              }
            if (!found)
              plan.Paths.push_back({qp.Proj, k});
          }
        }
      }
      transfer(s, init, uninit);
    }
  }
}

void MoveAnalysis::run() {
  // Nothing owned and nothing moved: nothing can be used after a move, and
  // there is no drop to plan.
  if (!B.HasMoves) {
    bool anyOwned = false;
    for (const Local &l : B.Locals)
      if (l.Owned)
        anyOwned = true;
    if (!anyOwned)
      return;
  }
  collectPaths();
  const size_t n = Paths.size();
  const size_t nb = B.Blocks.size();
  InitIn.assign(nb, BitSet(n));
  UninitIn.assign(nb, BitSet(n));
  // At entry: parameters are initialised, everything else is not.
  for (PathId i = 0; i < n; ++i) {
    const Place &pl = B.Places.get(Paths[i]);
    const Local &l = B.Locals[pl.Root];
    if (l.K == Local::Param || l.K == Local::Capture || l.K == Local::Global)
      InitIn[B.Entry].set(i);
    else
      UninitIn[B.Entry].set(i);
  }
  // Where each path is moved.
  for (BlockId bi = 0; bi < nb; ++bi)
    for (uint32_t si = 0; si < B.Blocks[bi].Stmts.size(); ++si)
      for (const PlaceAccess &a : B.Blocks[bi].Stmts[si].Accesses)
        if (a.A == Access::Move) {
          PathId p = pathFor(a.Place);
          if (p != kNone)
            MovedAt[p].push_back({Location{bi, si}, a.Range});
        }

  // Forward may-analysis to a fixpoint.
  std::vector<bool> inWork(nb, true);
  std::deque<BlockId> work;
  for (BlockId bi = 0; bi < nb; ++bi)
    work.push_back(bi);
  while (!work.empty()) {
    BlockId bi = work.front();
    work.pop_front();
    inWork[bi] = false;
    BitSet init = InitIn[bi], uninit = UninitIn[bi];
    for (const Stmt &s : B.Blocks[bi].Stmts)
      transfer(s, init, uninit);
    for (BlockId succ : B.Blocks[bi].Term.Succ) {
      bool changed = InitIn[succ].unite(init);
      changed |= UninitIn[succ].unite(uninit);
      if (changed && !inWork[succ]) {
        inWork[succ] = true;
        work.push_back(succ);
      }
    }
  }

  // Report, in program order.
  for (BlockId bi = 0; bi < nb; ++bi) {
    BitSet init = InitIn[bi], uninit = UninitIn[bi];
    for (uint32_t si = 0; si < B.Blocks[bi].Stmts.size(); ++si) {
      const Stmt &s = B.Blocks[bi].Stmts[si];
      for (const PlaceAccess &a : s.Accesses) {
        if (a.A != Access::Move || a.Place == kNone)
          continue;
        // A binding that only borrows — `c` in `match e { E::A(c) => … }`
        // over a borrowed `e` — has a path of its own, but taking all of it
        // is still taking what somebody else owns.
        const Place &pl = B.Places.get(a.Place);
        const Local &root = B.Locals[pl.Root];
        // Nor can anything be taken out of a global: every function reaches
        // it, and no one function's paths could say it is still there.
        if (pathFor(a.Place) == kNone ||
            (pl.Proj.empty() && root.RefLike && !root.Owned) ||
            root.K == Local::Global)
          checkMoveOutOf(s, a);
      }
      check(s, Location{bi, si}, init, uninit);
      transfer(s, init, uninit);
    }
  }
  planDrops();
  // What the move sites were, for the code generator: a local that is
  // moved out of anywhere may be empty when its scope ends.
  for (const Block &blk : B.Blocks)
    for (const Stmt &s : blk.Stmts)
      for (const PlaceAccess &a : s.Accesses)
        if (a.A == Access::Move) {
          if (a.Source)
            Out.MovedExprs.insert(a.Source);
          if (a.Place != kNone) {
            const Local &l = B.Locals[B.Places.get(a.Place).Root];
            if (l.Var)
              l.Var->ZombieMoved = true;
          }
        }
}

} // namespace

void analyseMoves(Body &body, DiagnosticEngine &diags, MoveResults &out) {
  MoveAnalysis(body, diags, out).run();
}

} // namespace zombie
} // namespace rune
