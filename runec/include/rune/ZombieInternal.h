//===- ZombieInternal.h - What the checker's passes hand each other -*- C++ -*-===//
//
// The pieces of the Zombie borrow checker (Zombie.h) share these. Nothing
// outside `Zombie*.cpp` includes this file.
//
//===----------------------------------------------------------------------===//
#ifndef RUNE_ZOMBIE_INTERNAL_H
#define RUNE_ZOMBIE_INTERNAL_H

#include "rune/Diagnostics.h"
#include "rune/ZombieIR.h"

#include <atomic>
#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

namespace rune {
namespace zombie {

//===----------------------------------------------------------------------===//
// Summaries: what a caller needs to know about a callee
//===----------------------------------------------------------------------===//

/// One place a result may borrow from: a parameter and a field path under
/// what it points at. `Param == kNone` with `Global` set is the global origin.
struct FromEntry {
  unsigned Param = kNone;
  std::vector<unsigned> Path;
  bool Global = false;
  /// Not a borrow *of* the parameter but of what it already carries: the
  /// borrows inside a `T` that happens to be `&Row`. Taking one borrows
  /// nothing of the argument's own place.
  bool Carried = false;
  bool operator==(const FromEntry &o) const {
    return Param == o.Param && Path == o.Path && Global == o.Global &&
           Carried == o.Carried;
  }
};

/// What a function touches through one reference parameter.
struct View {
  struct Entry {
    std::vector<unsigned> Path;   ///< field indices under the parameter
    bool Write = false;
    bool operator==(const Entry &o) const {
      return Path == o.Path && Write == o.Write;
    }
  };
  bool Whole = true;              ///< reaches the parameter as a whole
  std::vector<Entry> Entries;
  bool operator==(const View &o) const {
    return Whole == o.Whole && Entries == o.Entries;
  }
};

struct FnSummary {
  /// Where the result borrows from. Empty with `ResultOwned` when the
  /// result carries no reference.
  std::vector<FromEntry> ResultFrom;
  bool ResultOwned = true;
  /// The result may point at something reached through a raw pointer, which
  /// only an explicit clause can vouch for.
  bool ResultUntracked = false;
  /// Per parameter: an explicit `from` on the parameter's own type — a
  /// requirement on the caller's argument.
  std::vector<std::vector<FromEntry>> ParamFrom;
  /// Per parameter: the view. Parameters that are not references have
  /// `Whole` views nothing consults.
  std::vector<View> Views;
  /// Written down rather than inferred, so fixed before the body is read.
  bool ResultExplicit = false;
  std::vector<bool> ViewExplicit;
  /// The body was never analysed (extern, trusted, a requirement): what is
  /// here is the declared or default contract.
  bool Declared = false;
  bool operator==(const FnSummary &o) const {
    return ResultFrom == o.ResultFrom && ResultOwned == o.ResultOwned &&
           ResultUntracked == o.ResultUntracked && Views == o.Views;
  }
};

/// Every function's summary, published as each is finished. Readers on
/// other threads only ever ask about callees that are already published —
/// the scheduler sees to that — so a slot is written by one thread and read
/// by others only after its flag says so.
class SummaryTable {
public:
  void add(const FunctionDecl *fn) {
    Index[fn] = Entries.size();
    Entries.emplace_back();
  }
  void seal() {
    Ready = std::make_unique<std::atomic<bool>[]>(Entries.size());
    for (size_t i = 0; i < Entries.size(); ++i)
      Ready[i].store(false, std::memory_order_relaxed);
  }
  /// The published summary, or null when there is none (yet).
  const FnSummary *find(const FunctionDecl *fn) const {
    auto it = Index.find(fn);
    if (it == Index.end() || !Ready[it->second].load(std::memory_order_acquire))
      return nullptr;
    return &Entries[it->second];
  }
  void publish(const FunctionDecl *fn, FnSummary sum) {
    auto it = Index.find(fn);
    if (it == Index.end())
      return;
    Entries[it->second] = std::move(sum);
    Ready[it->second].store(true, std::memory_order_release);
  }

private:
  std::unordered_map<const FunctionDecl *, size_t> Index;
  std::vector<FnSummary> Entries;
  std::unique_ptr<std::atomic<bool>[]> Ready;
};

/// The summary a signature declares before any body is read: explicit
/// `from` clauses and views, and the defaults where nothing was written.
FnSummary declaredSummary(const FunctionDecl *fn);

/// Reads `fn`'s summary out of `table`, or the declared one when it is not
/// there yet (a callee in the same recursive group, on the first round).
const FnSummary &summaryFor(const FunctionDecl *fn, const SummaryTable &table,
                            FnSummary &scratch);

//===----------------------------------------------------------------------===//
// Passes
//===----------------------------------------------------------------------===//

struct MoveResults {
  std::unordered_map<VarDecl *, DropPlan> Drops;
  std::set<const Expr *> MovedExprs;
};

/// Move/initialisation dataflow: E0273, E0274, E0277, and drop plans.
void analyseMoves(Body &body, DiagnosticEngine &diags, MoveResults &out);

/// Rewrites every call in `body` in the light of its callee's summary:
/// subset edges from arguments to the result, derived loans for implicit
/// borrows, view-shaped receiver loans, and caller-side `from` checks.
void applySummaries(Body &body, const SummaryTable &table,
                    DiagnosticEngine &diags);

/// Splits what a struct or tuple local holds by field: subset edges from a
/// field read take that field's origin, edges and loans into a field land in
/// that field's, a whole value's in every field's, and a full write of one
/// field forgets only its own. Runs after `applySummaries`.
void refineFieldOrigins(Body &body);

struct LoanResults {
  /// Which placeholder loans the result may hold, for inference.
  std::vector<FromEntry> ResultFrom;
  bool ResultUntracked = false;
  bool ResultHoldsAnything = false;
  /// The body had nothing to follow and the dataflow was not run.
  bool Skipped = false;
};

/// Liveness, loans and the location-sensitive origin dataflow: E0270,
/// E0272, E0275, E0278, E0279, E0280. Fills `out` for signature inference.
void analyseLoans(Body &body, DiagnosticEngine &diags, LoanResults &out);

/// Infers `body`'s summary from what the loan analysis found and what the
/// body does through its parameters, and checks it against the declared
/// one (E0281, E0299).
FnSummary inferSummary(Body &body, const LoanResults &loans,
                       const SummaryTable &table, DiagnosticEngine &diags);

} // namespace zombie
} // namespace rune

#endif
