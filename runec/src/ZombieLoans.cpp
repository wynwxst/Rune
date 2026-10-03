//===- ZombieLoans.cpp - Liveness, loans and their conflicts ----*- C++ -*-===//
//
// The heart of the checker, after Polonius: a reference's origin is the set
// of loans it may hold, and a loan is live at a point when some origin that
// is itself live there may hold it. Origins are per local, so "live" is the
// ordinary liveness of the local — computed backwards, killed by a definition
// — and the contents of an origin are computed forwards, location by
// location, so that a reference reassigned on one path and not another holds
// different loans on each. That is what makes a conditional return of a
// reference (Polonius's problem case #3) come out right.
//
// An access conflicts with a live loan when the two places overlap and the
// kinds disagree: a shared loan objects to writes, moves, exclusive borrows
// and destruction; an exclusive loan objects to everything. Assignments and
// the end of storage are *shallow* — they touch the place itself, not what
// a pointer inside it reaches — and kill the loans they would otherwise
// invalidate; every other access is *deep*. A move of a heap handle whose
// destination is known re-roots the loans into the object it owns, because
// the object did not move (§3.3 of the plan: internal references).
//
//===----------------------------------------------------------------------===//
#include "rune/Zombie.h"
#include "rune/ZombieIR.h"
#include "rune/ZombieInternal.h"

#include <deque>

namespace rune {
namespace zombie {

namespace {

struct State {
  /// Per origin: the loans it may hold.
  std::vector<BitSet> Contains;
  /// Two-phase loans that have been activated.
  BitSet Activated;
  bool unite(const State &o) {
    bool changed = false;
    for (size_t i = 0; i < Contains.size(); ++i)
      changed |= Contains[i].unite(o.Contains[i]);
    changed |= Activated.unite(o.Activated);
    return changed;
  }
  bool operator==(const State &o) const {
    return Contains == o.Contains && Activated == o.Activated;
  }
};

class LoanAnalysis {
public:
  LoanAnalysis(Body &body, DiagnosticEngine &diags, LoanResults &out)
      : B(body), Diags(diags), Out(out) {}
  void run();

private:
  Body &B;
  DiagnosticEngine &Diags;
  LoanResults &Out;

  //=== Liveness ==========================================================//
  /// Locals whose origin is their own (kind `Local`), densely numbered.
  std::vector<LocalId> Tracked;
  std::vector<uint32_t> TrackedIndex;   // per local, or kNone
  std::vector<BitSet> LiveOut;          // per block
  /// Per block, per statement index (0..n): the live set on entry to it;
  /// index n is the terminator.
  std::vector<std::vector<BitSet>> LiveBefore;
  void computeLiveness();
  bool originLive(OriginId o, BlockId b, uint32_t at) const {
    const Origin &og = B.Origins[o];
    if (og.K != Origin::Local)
      return true;
    uint32_t t = TrackedIndex[og.Owner];
    return t != kNone && LiveBefore[b][at].test(t);
  }

  //=== Loans =============================================================//
  std::vector<LoanId> PlaceholderLoan;  // per origin, or kNone
  std::unordered_map<uint64_t, std::vector<std::pair<LoanId, LoanId>>> Reroot;
  void seedSyntheticLoans();
  void precomputeReroots();
  LoanId rerootedLoan(LoanId l, PlaceId src, PlaceId dst);
  std::vector<State> In;
  State entryState();
  void transfer(const Stmt &s, Location at, State &st);
  void filterDead(State &st, BlockId b, uint32_t at);
  void propagate();

  //=== Checking ==========================================================//
  bool deepAfter(PlaceId prefix, PlaceId place) const;
  /// True when the loan reaches through an *owning* handle stored under
  /// `prefix`: a write there destroys what the loan points at.
  bool deepThroughHandle(PlaceId prefix, const Loan &l) const;
  bool loanCovers(const Loan &l, PlaceId p, bool &deep) const;
  bool conflicts(const PlaceAccess &a, const Loan &l, LoanId id,
                 const State &st, bool &deep) const;
  void checkStatement(const Stmt &s, Location at, const State &st);
  void checkMutability(const PlaceAccess &a, const Stmt &s);
  void report(const PlaceAccess &a, LoanId l, const Stmt &s, Location at,
              const State &st, bool deep);
  std::set<const PlaceAccess *> Reported;
  /// A later use of a local that holds `l` at `at`, for "still used here".
  SourceRange laterUse(LoanId l, Location at, const State &st) const;
  void collectResult();
};

//===----------------------------------------------------------------------===//
// Liveness
//===----------------------------------------------------------------------===//

void LoanAnalysis::computeLiveness() {
  TrackedIndex.assign(B.Locals.size(), kNone);
  for (LocalId l = 0; l < B.Locals.size(); ++l) {
    OriginId o = B.Locals[l].Origin;
    if (o != kNone && B.Origins[o].K == Origin::Local &&
        B.Origins[o].Owner == l) {
      TrackedIndex[l] = static_cast<uint32_t>(Tracked.size());
      Tracked.push_back(l);
    }
  }
  const size_t n = Tracked.size();
  const size_t nb = B.Blocks.size();
  LiveOut.assign(nb, BitSet(n));
  LiveBefore.assign(nb, {});

  auto rootOf = [&](PlaceId p) -> uint32_t {
    if (p == kNone)
      return kNone;
    return TrackedIndex[B.Places.get(p).Root];
  };
  auto isWholeLocal = [&](PlaceId p) {
    return p != kNone && B.Places.get(p).Proj.empty();
  };

  // Backward transfer of one statement over a live set.
  auto stmtTransfer = [&](const Stmt &s, BitSet &live) {
    // Dropping what was already moved away touches nothing it borrowed.
    if (s.K == Stmt::Drop && s.DropElided)
      return;
    // Definitions first (they come after the uses in execution order, so
    // are undone first walking backwards): a whole-local write, storage
    // coming or going.
    if (s.K == Stmt::StorageLive || s.K == Stmt::StorageDead) {
      uint32_t t = rootOf(s.Dst);
      if (t != kNone)
        live.reset(t);
    }
    // The closing write of an aggregate literal defines nothing: its parts
    // were written one by one just before, and what they borrowed is what
    // the whole holds. Treating it as a definition made the local dead in
    // between, and the loans its fields carried were dropped there — so a
    // struct or tuple holding a borrow was never checked against it.
    bool wholeWrite = false;
    for (const PlaceAccess &a : s.Accesses)
      if (a.A == Access::Write && isWholeLocal(a.Place) && !s.Aggregate)
        wholeWrite = true;
    if (s.Aggregate) {
      uint32_t t = rootOf(s.Dst);
      if (t != kNone)
        live.set(t);
    }
    if (wholeWrite) {
      uint32_t t = rootOf(s.Dst);
      if (t != kNone)
        live.reset(t);
    }
    for (const PlaceAccess &a : s.Accesses) {
      if (a.A == Access::Write && isWholeLocal(a.Place))
        continue;
      if (a.A == Access::StorageDead)
        continue;
      uint32_t t = rootOf(a.Place);
      if (t != kNone)
        live.set(t);
    }
    for (PlaceId arg : s.Args) {
      uint32_t t = rootOf(arg);
      if (t != kNone)
        live.set(t);
    }
    if (s.CalleeValue != kNone) {
      uint32_t t = rootOf(s.CalleeValue);
      if (t != kNone)
        live.set(t);
    }
    // Reading a reference through a subset edge keeps its source alive.
    for (const Subset &sub : s.Subsets) {
      const Origin &og = B.Origins[sub.From];
      if (og.K == Origin::Local) {
        uint32_t t = TrackedIndex[og.Owner];
        if (t != kNone)
          live.set(t);
      }
    }
  };

  bool changed = true;
  while (changed) {
    changed = false;
    for (size_t bi = nb; bi > 0; --bi) {
      BlockId b = static_cast<BlockId>(bi - 1);
      const Block &blk = B.Blocks[b];
      BitSet live(n);
      for (BlockId succ : blk.Term.Succ) {
        // Live-in of the successor is its first statement's live set.
        if (!LiveBefore[succ].empty())
          live.unite(LiveBefore[succ][0]);
      }
      if (blk.Term.Tested != kNone) {
        uint32_t t = rootOf(blk.Term.Tested);
        if (t != kNone)
          live.set(t);
      }
      std::vector<BitSet> before(blk.Stmts.size() + 1, BitSet(n));
      before[blk.Stmts.size()] = live;
      for (size_t si = blk.Stmts.size(); si > 0; --si) {
        stmtTransfer(blk.Stmts[si - 1], live);
        before[si - 1] = live;
      }
      if (LiveBefore[b].empty() || LiveBefore[b] != before) {
        LiveBefore[b] = std::move(before);
        changed = true;
      }
    }
  }
}

//===----------------------------------------------------------------------===//
// Loans
//===----------------------------------------------------------------------===//

void LoanAnalysis::seedSyntheticLoans() {
  PlaceholderLoan.assign(B.Origins.size(), kNone);
  for (OriginId o = 0; o < B.Origins.size(); ++o) {
    const Origin &og = B.Origins[o];
    if (og.K == Origin::Placeholder || og.K == Origin::Global ||
        og.K == Origin::Untracked) {
      Loan l;
      l.Placeholder = og.K == Origin::Placeholder;
      l.Param = og.Param;
      l.Global = og.K == Origin::Global;
      l.Place = kNone;
      if (og.K == Origin::Untracked)
        l.Placeholder = false;
      PlaceholderLoan[o] = static_cast<LoanId>(B.Loans.size());
      B.Loans.push_back(l);
    }
  }
}

bool LoanAnalysis::deepAfter(PlaceId prefix, PlaceId place) const {
  const Place &a = B.Places.get(prefix), &b = B.Places.get(place);
  for (size_t i = a.Proj.size(); i < b.Proj.size(); ++i)
    if (b.Proj[i].K == Projection::Deref)
      return true;
  return false;
}

LoanId LoanAnalysis::rerootedLoan(LoanId l, PlaceId src, PlaceId dst) {
  const Loan &old = B.Loans[l];
  const Place &sp = B.Places.get(src), &lp = B.Places.get(old.Place),
              &dp = B.Places.get(dst);
  Place np;
  np.Root = dp.Root;
  np.Proj = dp.Proj;
  np.Proj.insert(np.Proj.end(), lp.Proj.begin() + sp.Proj.size(),
                 lp.Proj.end());
  PlaceId nplace = B.Places.intern(np);
  for (LoanId i = 0; i < B.Loans.size(); ++i)
    if (B.Loans[i].Place == nplace && B.Loans[i].Mutable == old.Mutable &&
        B.Loans[i].At == old.At &&
        B.Loans[i].Range.begin().raw() == old.Range.begin().raw())
      return i;
  Loan n = old;
  n.Place = nplace;
  n.Alt.clear();
  B.Loans.push_back(n);
  return static_cast<LoanId>(B.Loans.size() - 1);
}

void LoanAnalysis::precomputeReroots() {
  // A move of a handle carries the loans under its `Deref` with it. Each
  // such statement gets a table of (old loan, new loan); new loans may be
  // carried again by a later move, so this runs to a fixpoint.
  bool changed = true;
  while (changed) {
    changed = false;
    for (BlockId bi = 0; bi < B.Blocks.size(); ++bi)
      for (uint32_t si = 0; si < B.Blocks[bi].Stmts.size(); ++si) {
        const Stmt &s = B.Blocks[bi].Stmts[si];
        if (s.K != Stmt::Assign || s.Src == kNone || s.Dst == kNone)
          continue;
        bool moves = false;
        for (const PlaceAccess &a : s.Accesses)
          if (a.A == Access::Move && a.Place == s.Src)
            moves = true;
        if (!moves)
          continue;
        uint64_t key = (static_cast<uint64_t>(bi) << 32) | si;
        auto &table = Reroot[key];
        for (LoanId l = 0; l < B.Loans.size(); ++l) {
          const Loan &ln = B.Loans[l];
          if (ln.Place == kNone || ln.Placeholder || ln.Global)
            continue;
          if (!B.Places.isPrefixOf(s.Src, ln.Place) || ln.Place == s.Src)
            continue;
          if (!deepAfter(s.Src, ln.Place))
            continue;
          // Two handles that swap places (`a.next = b; b.prev = a`) would
          // carry loans back and forth forever; a place that deep is not
          // one anybody wrote, so the loan simply stays where it was.
          const Place &dp = B.Places.get(s.Dst), &lp = B.Places.get(ln.Place),
                      &sp = B.Places.get(s.Src);
          if (dp.Proj.size() + lp.Proj.size() - sp.Proj.size() > 12)
            continue;
          bool have = false;
          for (const auto &pr : table)
            if (pr.first == l)
              have = true;
          if (have)
            continue;
          size_t before = B.Loans.size();
          LoanId n = rerootedLoan(l, s.Src, s.Dst);
          table.push_back({l, n});
          if (B.Loans.size() != before)
            changed = true;
        }
      }
  }
}

State LoanAnalysis::entryState() {
  State st;
  st.Contains.assign(B.Origins.size(), BitSet(B.Loans.size()));
  st.Activated = BitSet(B.Loans.size());
  for (OriginId o = 0; o < B.Origins.size(); ++o)
    if (PlaceholderLoan[o] != kNone)
      st.Contains[o].set(PlaceholderLoan[o]);
  return st;
}

void LoanAnalysis::transfer(const Stmt &s, Location at, State &st) {
  auto originOfPlace = [&](PlaceId p) -> OriginId {
    if (p == kNone)
      return kNone;
    return B.Locals[B.Places.get(p).Root].Origin;
  };
  const size_t nl = B.Loans.size();

  // 1. Assignments and storage ends kill what they overwrite, deep. The
  //    closing write of an aggregate literal overwrites nothing.
  for (const PlaceAccess &a : s.Accesses) {
    if (a.A != Access::Write && a.A != Access::StorageDead)
      continue;
    if (a.Place == kNone || (s.Aggregate && a.Place == s.Dst))
      continue;
    for (LoanId l = 0; l < nl; ++l) {
      const Loan &ln = B.Loans[l];
      if (ln.Place == kNone)
        continue;
      bool covered = B.Places.isPrefixOf(a.Place, ln.Place);
      for (PlaceId alt : ln.Alt)
        covered |= B.Places.isPrefixOf(a.Place, alt);
      if (!covered)
        continue;
      // The result's origin keeps what it holds: a loan of a local that
      // is still there when the function returns is what E0272 is about.
      for (OriginId o = 0; o < st.Contains.size(); ++o)
        if (B.Origins[o].K != Origin::Result)
          st.Contains[o].reset(l);
    }
  }
  // 2. A whole-local write forgets the local's old loans.
  for (OriginId o : s.Clears)
    st.Contains[o].clear();
  if (s.Dst != kNone && B.Places.get(s.Dst).Proj.empty() &&
      (s.K == Stmt::Borrow || s.K == Stmt::Call)) {
    OriginId o = originOfPlace(s.Dst);
    if (o != kNone && B.Origins[o].K == Origin::Local)
      st.Contains[o].clear();
  }
  // 3. Moving a handle carries the loans into its object along.
  {
    uint64_t key = (static_cast<uint64_t>(at.Block) << 32) | at.Index;
    auto it = Reroot.find(key);
    if (it != Reroot.end())
      for (const auto &pr : it->second)
        for (BitSet &c : st.Contains)
          if (c.test(pr.first)) {
            c.reset(pr.first);
            c.set(pr.second);
          }
  }
  // 4. New loans land in the destination's origin — or, in a local tracked
  //    by field, in the origins `refineFieldOrigins` chose.
  std::vector<OriginId> intos = s.Into;
  if (intos.empty()) {
    OriginId into = originOfPlace(s.Dst);
    if (into != kNone)
      intos.push_back(into);
  }
  if (s.K == Stmt::Borrow && s.Loan != kNone) {
    for (OriginId into : intos)
      st.Contains[into].set(s.Loan);
    // Taken again — on a loop's next turn — a two-phase loan starts out
    // reserved again.
    st.Activated.reset(s.Loan);
  }
  for (OriginId into : intos)
    for (LoanId l : s.Issues)
      st.Contains[into].set(l);
  // 5. Subset edges: the destination may hold whatever the source may.
  for (int round = 0; round < 2; ++round)
    for (const Subset &sub : s.Subsets)
      st.Contains[sub.Into].unite(st.Contains[sub.From]);
  // 6. Activation.
  for (const PlaceAccess &a : s.Accesses)
    if (a.A == Access::Activate && s.Loan != kNone)
      st.Activated.set(s.Loan);
}

void LoanAnalysis::filterDead(State &st, BlockId b, uint32_t at) {
  for (OriginId o = 0; o < B.Origins.size(); ++o)
    if (!originLive(o, b, at))
      st.Contains[o].clear();
}

void LoanAnalysis::propagate() {
  const size_t nb = B.Blocks.size();
  In.assign(nb, State{});
  for (BlockId b = 0; b < nb; ++b) {
    In[b].Contains.assign(B.Origins.size(), BitSet(B.Loans.size()));
    In[b].Activated = BitSet(B.Loans.size());
  }
  In[B.Entry] = entryState();
  std::deque<BlockId> work{B.Entry};
  std::vector<bool> inWork(nb, false);
  inWork[B.Entry] = true;
  while (!work.empty()) {
    BlockId b = work.front();
    work.pop_front();
    inWork[b] = false;
    State st = In[b];
    const Block &blk = B.Blocks[b];
    for (uint32_t si = 0; si < blk.Stmts.size(); ++si) {
      transfer(blk.Stmts[si], Location{b, si}, st);
      filterDead(st, b, si + 1);
    }
    for (BlockId succ : blk.Term.Succ) {
      State next = st;
      filterDead(next, succ, 0);
      // Placeholders and the like are always present.
      for (OriginId o = 0; o < B.Origins.size(); ++o)
        if (PlaceholderLoan[o] != kNone)
          next.Contains[o].set(PlaceholderLoan[o]);
      if (In[succ].unite(next) && !inWork[succ]) {
        inWork[succ] = true;
        work.push_back(succ);
      }
    }
  }
}

//===----------------------------------------------------------------------===//
// Conflicts
//===----------------------------------------------------------------------===//

bool LoanAnalysis::loanCovers(const Loan &l, PlaceId p, bool &deep) const {
  auto one = [&](PlaceId lp) {
    if (lp == kNone || p == kNone)
      return false;
    if (B.Places.isPrefixOf(p, lp)) {
      deep = deepAfter(p, lp);
      return true;
    }
    if (B.Places.isPrefixOf(lp, p)) {
      deep = false;
      return true;
    }
    return false;
  };
  if (!l.Alt.empty()) {
    for (PlaceId alt : l.Alt)
      if (one(alt))
        return true;
    return false;
  }
  return one(l.Place);
}

bool LoanAnalysis::conflicts(const PlaceAccess &a, const Loan &l, LoanId id,
                             const State &st, bool &deep) const {
  if (l.Placeholder || l.Global || l.Place == kNone)
    return false;
  if (!loanCovers(l, a.Place, deep))
    return false;
  bool exclusive = l.Mutable && (!l.TwoPhase || st.Activated.test(id));
  switch (a.A) {
  case Access::Read:
  case Access::Shared:
  case Access::Reserve:
    return exclusive;
  case Access::Activate:
    // Becoming exclusive: nothing else may reach the place.
    return true;
  case Access::Mut:
    return true;
  case Access::Move:
    // Deep through a handle with a known destination: carried along, not
    // a conflict. Everything else: a move takes the value away.
    return !(deep && false);
  case Access::Drop:
    return true;
  case Access::Write:
  case Access::StorageDead:
    // Shallow: what a *pointer* inside the place reaches is not touched.
    // What an owning handle inside it reaches is destroyed with it.
    return !deep || deepThroughHandle(a.Place, l);
  }
  return false;
}

bool LoanAnalysis::deepThroughHandle(PlaceId prefix, const Loan &l) const {
  auto one = [&](PlaceId lp) {
    if (lp == kNone || !B.Places.isPrefixOf(prefix, lp))
      return false;
    const Place &a = B.Places.get(prefix), &b = B.Places.get(lp);
    Place walk{b.Root, std::vector<Projection>(b.Proj.begin(),
                                               b.Proj.begin() + a.Proj.size())};
    for (size_t i = a.Proj.size(); i < b.Proj.size(); ++i) {
      if (b.Proj[i].K == Projection::Deref) {
        // A borrow-like local (`&self` on a class, an alias binding) holds
        // its object for someone else: its dereference owns nothing.
        if (walk.Proj.empty() && B.Locals[walk.Root].RefLike)
          return false;
        Type *t = placeType(B, const_cast<PlaceTable &>(B.Places).intern(walk));
        return t && t->isHeapHandle();
      }
      walk.Proj.push_back(b.Proj[i]);
    }
    return false;
  };
  if (!l.Alt.empty()) {
    for (PlaceId alt : l.Alt)
      if (one(alt))
        return true;
    return false;
  }
  return one(l.Place);
}

SourceRange LoanAnalysis::laterUse(LoanId l, Location at,
                                   const State &st) const {
  // A local whose origin holds the loan here and which is used later.
  for (OriginId o = 0; o < B.Origins.size(); ++o) {
    if (!st.Contains[o].test(l))
      continue;
    const Origin &og = B.Origins[o];
    if (og.K != Origin::Local)
      continue;
    LocalId owner = og.Owner;
    auto it = B.Uses.find(owner);
    if (it == B.Uses.end())
      continue;
    // The last mention textually after the access is the honest "here".
    SourceRange best;
    for (const SourceRange &r : it->second)
      if (r.begin().raw() > B.Loans[l].Range.begin().raw() &&
          (!best.isValid() || r.begin().raw() > best.begin().raw()))
        best = r;
    if (best.isValid())
      return best;
  }
  return SourceRange();
}

void LoanAnalysis::report(const PlaceAccess &a, LoanId l, const Stmt &s,
                          Location at, const State &st, bool deep) {
  if (!Reported.insert(&a).second)
    return;
  const Loan &ln = B.Loans[l];
  std::string place = B.spell(a.Place);
  std::string borrowed = B.spell(ln.Place);
  bool exclusive = ln.Mutable && (!ln.TwoPhase || st.Activated.test(l));
  const char *kind = exclusive ? "`&var`" : "`&`";
  SourceRange later = laterUse(l, at, st);
  (void)deep;
  (void)s;

  switch (a.A) {
  case Access::Shared:
  case Access::Reserve:
  case Access::Mut:
  case Access::Activate: {
    bool wantMut = a.A != Access::Shared;
    DiagBuilder d =
        wantMut ? Diags.error(a.Range,
                              "cannot borrow '{}' as `&var` while '{}' is "
                              "borrowed",
                              place, borrowed)
                : Diags.error(a.Range,
                              "cannot borrow '{}' while '{}' is borrowed as "
                              "`&var`",
                              place, borrowed);
    if (a.A == Access::Activate && !ln.TwoPhase)
      ;
    if (ln.TwoPhase && a.A == Access::Activate)
      d.note("the receiver was reserved for the call and is still borrowed "
             "when the call needs it exclusively");
    d.note("a borrow that can write has to be the only one");
    d.related(ln.Range, "the other borrow is taken here",
              later.isValid()
                  ? "and is still used later; finish with it first, or take "
                    "both as `&`"
                  : "finish with it first, or take both as `&`");
    if (later.isValid())
      d.related(later, "the borrow is still used here");
    d.note("if both borrows genuinely have to overlap, `std::mem::Checked<T>` "
           "moves this check to run time");
    d.code(270);
    return;
  }
  case Access::Write: {
    auto d = Diags.error(a.Range, "cannot assign to '{}' while it is borrowed",
                         place);
    d.note("the borrow would see the new value, or point at nothing");
    d.related(ln.Range, "borrowed here",
              "finish with the borrow before this line");
    if (later.isValid())
      d.related(later, "the borrow is still used here");
    d.code(278);
    return;
  }
  case Access::Read: {
    auto d = Diags.error(a.Range,
                         "cannot use '{}' while it is borrowed as `&var`",
                         place);
    d.note("an exclusive borrow is the only way to reach the value while it "
           "lasts");
    d.related(ln.Range, "borrowed here",
              "finish with the borrow before this line");
    if (later.isValid())
      d.related(later, "the borrow is still used here");
    d.code(279);
    return;
  }
  case Access::Move: {
    auto d = Diags.error(a.Range, "cannot move '{}' while it is borrowed",
                         place);
    d.note("the borrow would be left pointing at a place that no longer "
           "holds anything");
    d.related(ln.Range, "the borrow is taken here",
              "finish with the borrow first, or borrow instead of moving");
    if (later.isValid())
      d.related(later, "the borrow is still used here");
    d.code(275);
    return;
  }
  case Access::Drop:
  case Access::StorageDead: {
    auto d = Diags.error(ln.Range, "'{}' does not live long enough",
                         B.spell(ln.Place));
    d.note("'{}' is destroyed at the end of its scope while the borrow is "
           "still used",
           place);
    if (later.isValid())
      d.related(later, "the borrow is still used here",
                "declare the value in the outer scope, or hand it on with "
                "`move`");
    else
      d.note("declare the value in the outer scope, or hand it on with "
             "`move`");
    d.code(280);
    return;
  }
  }
  (void)kind;
}

void LoanAnalysis::checkMutability(const PlaceAccess &a, const Stmt &s) {
  (void)s;
  if (a.A != Access::Write && a.A != Access::Mut && a.A != Access::Reserve)
    return;
  if (a.Place == kNone)
    return;
  const Place &pl = B.Places.get(a.Place);
  const Local &root = B.Locals[pl.Root];
  // `&var` of a global cannot be proven exclusive across functions.
  if (root.K == Local::Global && (a.A == Access::Mut || a.A == Access::Reserve) &&
      !s.Unsafe) {
    auto d = Diags.error(a.Range, "a global cannot be borrowed as `&var`");
    d.note("nothing can prove that no other function reaches it meanwhile");
    d.note("keep it in a `std::mem::Checked<T>`, or write the access in an "
           "`unsafe { }` block");
    d.code(289);
    return;
  }
  // Through a shared borrow: `&self`, `&T`, an alias binding.
  Type *t = root.Ty;
  bool viaShared = false;
  if (root.RefLike && !root.RefMutable && !pl.Proj.empty() &&
      pl.Proj[0].K == Projection::Deref)
    viaShared = true;
  Place prefix{pl.Root, {}};
  for (const Projection &pr : pl.Proj) {
    if (pr.K == Projection::Deref && t && t->is(TypeKind::Pointer) &&
        !t->isRawPointer() && !t->isMutablePointer())
      viaShared = true;
    prefix.Proj.push_back(pr);
    t = placeType(B, B.Places.intern(prefix));
    if (!t)
      break;
  }
  if (!viaShared)
    return;
  if (a.A == Access::Write) {
    auto d = Diags.error(a.Range, "cannot assign to '{}' through `&`",
                         B.spell(a.Place));
    d.note("a shared borrow may read what it reaches, never change it");
    d.note("take `&var self` (or a `&var` parameter); if the field must "
           "change behind `&`, keep it in a `std::mem::Checked<T>`");
    d.code(290);
  } else {
    auto d = Diags.error(a.Range,
                         "cannot borrow '{}' as `&var` through `&`",
                         B.spell(a.Place));
    d.note("a shared borrow may read what it reaches, never change it — "
           "and a `&var self` method changes its receiver");
    d.note("take `&var self` (or a `&var` parameter); if the value must "
           "change behind `&`, keep it in a `std::mem::Checked<T>`");
    d.code(290);
  }
}

void LoanAnalysis::checkStatement(const Stmt &s, Location at, const State &st) {
  // (Mutability was checked up front, before any dataflow.)
  // An internal reference closes its own loop: everything it holds points
  // into the sibling field's object, and nothing else.
  if (s.InternalTarget != kNone && s.Src != kNone) {
    OriginId o = B.Locals[B.Places.get(s.Src).Root].Origin;
    if (o != kNone) {
      bool complained = false;
      st.Contains[o].forEach([&](size_t li) {
        if (complained)
          return;
        const Loan &l = B.Loans[li];
        if (l.Placeholder || l.Global || l.Place == kNone) {
          complained = true;
        } else if (!B.Places.isPrefixOf(s.InternalTarget, l.Place)) {
          complained = true;
        }
        if (!complained)
          return;
        std::string target = B.spell(s.InternalTarget);
        size_t dot = target.rfind('.');
        std::string field = dot == std::string::npos ? target
                                                     : target.substr(dot + 1);
        auto d = Diags.error(s.Range,
                             "'{}' must borrow from the '{}' of this same "
                             "value",
                             s.InternalField ? s.InternalField->Name : "it",
                             field);
        if (l.Place != kNone && !l.Placeholder && !l.Global)
          d.related(l.Range, "it borrows from here instead",
                    fmt("build '{}' first and borrow '{}' from it", field,
                        s.InternalField ? s.InternalField->Name : "it"));
        else
          d.note("build '{}' first and borrow '{}' from it", field,
                 s.InternalField ? s.InternalField->Name : "it");
        d.code(297);
      });
    }
  }
  // A caller-side `from` on a parameter (`item: &Item from list`): the
  // argument's origin must stay within what the named argument(s) lend, so a
  // thread handed the scope's environment cannot also be handed a body local.
  for (const Stmt::FromRequirement &req : s.FromReqs) {
    PlaceId argP = req.Arg < s.Args.size() ? s.Args[req.Arg] : kNone;
    if (argP == kNone)
      continue;
    OriginId argO = B.Locals[B.Places.get(argP).Root].Origin;
    if (argO == kNone)
      continue;
    // What the named argument(s) lend: the roots they themselves borrow from
    // (so `item` may borrow from wherever `list` does), plus their own place
    // roots (so it may borrow from `list` directly). `origin(item)` has to
    // stay inside that.
    std::set<LocalId> allowedRoots;
    std::set<unsigned> allowedParams;
    for (unsigned fa : req.FromArgs) {
      if (fa >= s.Args.size() || s.Args[fa] == kNone)
        continue;
      LocalId far = B.Places.get(s.Args[fa]).Root;
      allowedRoots.insert(far);
      const Local &fl = B.Locals[far];
      if (fl.K == Local::Param || fl.K == Local::Capture)
        allowedParams.insert(fl.Index);
      OriginId fo = fl.Origin;
      if (fo != kNone)
        st.Contains[fo].forEach([&](size_t li) {
          const Loan &l2 = B.Loans[li];
          if (l2.Placeholder)
            allowedParams.insert(l2.Param);
          else if (l2.Place != kNone)
            allowedRoots.insert(B.Places.get(l2.Place).Root);
        });
    }
    const Loan *offender = nullptr;
    st.Contains[argO].forEach([&](size_t li) {
      if (offender)
        return;
      const Loan &l = B.Loans[li];
      if (l.Global) {
        // A global borrow outlives every place a `from` clause could name, so
        // it always satisfies one: a longer lifetime coerces to a shorter.
        return;
      }
      if (l.Placeholder) {
        if (!allowedParams.count(l.Param))
          offender = &l;
        return;
      }
      if (l.Place == kNone) {
        offender = &l; // an untracked (raw) borrow: not vouched for here
        return;
      }
      LocalId lr = B.Places.get(l.Place).Root;
      // A borrow of a global — or of a string literal, which is immortal —
      // outlives every named place: it coerces down.
      if (B.Locals[lr].K == Local::Global || B.Locals[lr].Immortal)
        return;
      if (!allowedRoots.count(lr))
        offender = &l;
    });
    if (offender) {
      auto d = Diags.error(req.Range, "'{}' must borrow from '{}'",
                           req.Name ? req.Name : "argument",
                           req.FromName ? req.FromName : "it");
      if (offender->Place != kNone && !offender->Placeholder && !offender->Global)
        d.related(offender->Range, "it borrows from here instead",
                  "pass something borrowed from the named place, so it cannot "
                  "outlive it");
      else
        d.note("pass something borrowed from the named place, so it cannot "
               "outlive it");
      d.code(283);
    }
  }

  for (const PlaceAccess &a : s.Accesses) {
    if (a.Place == kNone)
      continue;
    // The closing write of an aggregate literal overwrites nothing: its
    // parts are already in place, a field borrowing a sibling included.
    if (s.Aggregate && a.A == Access::Write && a.Place == s.Dst)
      continue;
    for (OriginId o = 0; o < B.Origins.size(); ++o) {
      if (!originLive(o, at.Block, at.Index))
        continue;
      const BitSet &c = st.Contains[o];
      if (!c.any())
        continue;
      c.forEach([&](size_t li) {
        LoanId l = static_cast<LoanId>(li);
        if (l == s.Loan && a.A != Access::Activate)
          return; // the loan this statement makes
        if (a.A == Access::Activate && l == s.Loan)
          return; // its own activation
        bool deep = false;
        if (!conflicts(a, B.Loans[l], l, st, deep))
          return;
        // A move with a known destination carries deep loans through a
        // handle rather than breaking them.
        if (a.A == Access::Move && deep && s.K == Stmt::Assign &&
            s.Src == a.Place && s.Dst != kNone)
          return;
        // A loan held only by the value being moved — a struct borrowing
        // from its own field — travels with it wherever it goes.
        if (a.A == Access::Move && deep) {
          LocalId root = B.Places.get(a.Place).Root;
          bool internal = true;
          for (OriginId h = 0; h < B.Origins.size() && internal; ++h) {
            if (!st.Contains[h].test(l) || !originLive(h, at.Block, at.Index))
              continue;
            const Origin &hg = B.Origins[h];
            if (hg.K != Origin::Local || hg.Owner != root)
              internal = false;
          }
          if (internal)
            return;
        }
        // A value going out of scope while only the result still borrows
        // it is "returns a borrow of a local", said once, at the return.
        if ((a.A == Access::Drop || a.A == Access::StorageDead) &&
            B.Origins[o].K == Origin::Result)
          return;
        // A literal's temporary going out of scope takes nothing with it:
        // the object it named is immortal, and the borrow is of that.
        if ((a.A == Access::Drop || a.A == Access::StorageDead) &&
            B.Loans[l].Place != kNone &&
            B.Locals[B.Places.get(B.Loans[l].Place).Root].Immortal)
          return;
        report(a, l, s, at, st, deep);
      });
    }
  }
}

//===----------------------------------------------------------------------===//
// Results
//===----------------------------------------------------------------------===//

void LoanAnalysis::collectResult() {
  OriginId ro = B.Locals[B.ReturnLocal].Origin;
  if (ro == kNone)
    return;
  Out.ResultHoldsAnything = true;
  std::set<std::pair<unsigned, std::vector<unsigned>>> seen;
  // A placeholder says "somewhere in what parameter `p` lent"; a loan on
  // `p.items` says exactly where. When both are held, the loan is the
  // answer and the placeholder only repeats it more coarsely.
  std::set<unsigned> refined;
  for (BlockId b : B.Exits) {
    State st = In[b];
    const Block &blk = B.Blocks[b];
    for (uint32_t si = 0; si < blk.Stmts.size(); ++si)
      transfer(blk.Stmts[si], Location{b, si}, st);
    st.Contains[ro].forEach([&](size_t li) {
      const Loan &l = B.Loans[li];
      if (l.Placeholder || l.Global || l.Place == kNone)
        return;
      const Local &root = B.Locals[B.Places.get(l.Place).Root];
      if (root.K == Local::Param || root.K == Local::Capture)
        refined.insert(root.Index);
    });
  }
  for (BlockId b : B.Exits) {
    State st = In[b];
    const Block &blk = B.Blocks[b];
    for (uint32_t si = 0; si < blk.Stmts.size(); ++si)
      transfer(blk.Stmts[si], Location{b, si}, st);
    st.Contains[ro].forEach([&](size_t li) {
      const Loan &l = B.Loans[li];
      if (l.Placeholder) {
        if (refined.count(l.Param))
          return;
        FromEntry e;
        e.Param = l.Param;
        e.Path = l.PlaceholderPath;
        if (seen.insert({e.Param, e.Path}).second)
          Out.ResultFrom.push_back(e);
        return;
      }
      if (l.Global) {
        FromEntry e;
        e.Global = true;
        if (seen.insert({kNone, {}}).second)
          Out.ResultFrom.push_back(e);
        return;
      }
      if (l.Place == kNone) {
        Out.ResultUntracked = true;
        return;
      }
      const Place &p = B.Places.get(l.Place);
      const Local &root = B.Locals[p.Root];
      if (p.Root == B.ReturnLocal)
        return; // the result borrows from itself: internal, and owned
      if (root.K == Local::Param || root.K == Local::Capture) {
        // A loan of something a parameter reaches: `from p.field`.
        FromEntry e;
        e.Param = root.Index;
        bool afterDeref = root.RefLike;
        Place prefix{p.Root, {}};
        for (const Projection &pr : p.Proj) {
          if (pr.K == Projection::Deref) {
            // Through a borrow the parameter holds, the target is the
            // caller's. Through a handle it owns — a `String`, an object
            // passed by value — it is this call's, and goes when it does.
            Type *pt = placeType(B, B.Places.intern(prefix));
            if (!pt || pt->is(TypeKind::Pointer) || pt->is(TypeKind::Slice))
              afterDeref = true;
            prefix.Proj.push_back(pr);
            continue;
          }
          prefix.Proj.push_back(pr);
          if (!afterDeref)
            continue;
          if (pr.K == Projection::Field)
            e.Path.push_back(pr.Arg);
          else
            break;
        }
        if (!afterDeref && !root.RefLike) {
          // A borrow of the parameter's own storage: a local of this call.
          auto d = Diags.error(l.Range,
                               "this returns a borrow of '{}', which does "
                               "not outlive the call",
                               B.spell(l.Place));
          d.note("a parameter passed by value is destroyed when this "
                 "function returns, so the caller would be handed an "
                 "address to nothing");
          d.note("return the value itself, or take what is borrowed as a "
                 "`&` parameter so the caller owns it");
          d.code(272);
          return;
        }
        if (seen.insert({e.Param, e.Path}).second)
          Out.ResultFrom.push_back(e);
        return;
      }
      if (root.K == Local::Global || root.Immortal) {
        FromEntry e;
        e.Global = true;
        if (seen.insert({kNone, {}}).second)
          Out.ResultFrom.push_back(e);
        return;
      }
      // Through a local that is itself a borrow — `&slice[i]`, `&(*r).f`:
      // a reborrow. What it may point at is bounded by what that local
      // borrowed, which flowed into the result with it, and is judged there.
      {
        Place prefix{p.Root, {}};
        for (const Projection &pr : p.Proj) {
          if (pr.K == Projection::Deref) {
            Type *pt = placeType(B, B.Places.intern(prefix));
            if (pt && (pt->is(TypeKind::Slice) ||
                       (pt->is(TypeKind::Pointer) && !pt->isRawPointer() &&
                        !pt->isWeakPointer())))
              return;
            break;
          }
          prefix.Proj.push_back(pr);
        }
      }
      // A local, a temporary: gone when this returns.
      auto d = Diags.error(l.Range,
                           "this returns a borrow of '{}', which does not "
                           "outlive the call",
                           B.spell(l.Place));
      d.note("the binding is destroyed when this function returns, so the "
             "caller would be handed an address to nothing");
      if (root.Var)
        d.related(root.Range, "declared here",
                  "return the value itself, or take what is borrowed as a "
                  "parameter so the caller owns it");
      else
        d.note("return the value itself, or take what is borrowed as a "
               "parameter so the caller owns it");
      d.code(272);
    });
  }
}

void LoanAnalysis::run() {
  // What may be written through what is a question with no dataflow in it.
  for (const Block &blk : B.Blocks)
    for (const Stmt &s : blk.Stmts)
      for (const PlaceAccess &a : s.Accesses)
        if (a.Place != kNone)
          checkMutability(a, s);
  // A body that takes no borrow and holds no reference has no loans to
  // follow: most small functions. The result cannot hold anything either.
  bool anyRefLocal = false;
  for (const Local &l : B.Locals)
    if (l.Origin != kNone && B.Origins[l.Origin].K == Origin::Local)
      anyRefLocal = true;
  if (B.Loans.empty() && !anyRefLocal &&
      B.Locals[B.ReturnLocal].Origin == kNone) {
    Out.Skipped = true;
    return;
  }
  computeLiveness();
  seedSyntheticLoans();
  precomputeReroots();
  propagate();
  for (BlockId b = 0; b < B.Blocks.size(); ++b) {
    State st = In[b];
    const Block &blk = B.Blocks[b];
    for (uint32_t si = 0; si < blk.Stmts.size(); ++si) {
      checkStatement(blk.Stmts[si], Location{b, si}, st);
      transfer(blk.Stmts[si], Location{b, si}, st);
      filterDead(st, b, si + 1);
    }
  }
  collectResult();
}

} // namespace

void analyseLoans(Body &body, DiagnosticEngine &diags, LoanResults &out) {
  LoanAnalysis(body, diags, out).run();
}

} // namespace zombie
} // namespace rune
