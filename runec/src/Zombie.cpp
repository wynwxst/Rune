//===- Zombie.cpp - Running the borrow checker over a program ---*- C++ -*-===//
//
// See Zombie.h for what the checker is. This file is the order things happen
// in: every body is lowered, the bodies are arranged by who calls whom, and
// each is analysed once its callees' summaries exist — in parallel within a
// level, with diagnostics captured per body and replayed in queue order so
// two runs print the same thing.
//
//===----------------------------------------------------------------------===//
#include "rune/Zombie.h"
#include "rune/Parallel.h"
#include "rune/ZombieIR.h"
#include "rune/ZombieInternal.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>
#include <thread>

#include <iostream>
#include <sstream>

namespace rune {
namespace zombie {

//===----------------------------------------------------------------------===//
// Dumping
//===----------------------------------------------------------------------===//

static const char *accessName(Access a) {
  switch (a) {
  case Access::Read: return "read";
  case Access::Write: return "write";
  case Access::Move: return "move";
  case Access::Shared: return "&";
  case Access::Mut: return "&var";
  case Access::Reserve: return "reserve";
  case Access::Activate: return "activate";
  case Access::Drop: return "drop";
  case Access::StorageDead: return "dead";
  }
  return "?";
}

static std::string spellPlace(const Body &b, PlaceId p) {
  if (p == kNone)
    return "_";
  const Place &pl = b.Places.get(p);
  std::string s = "%" + std::to_string(pl.Root);
  if (pl.Root < b.Locals.size() && !b.Locals[pl.Root].Name.empty())
    s += "(" + b.Locals[pl.Root].Name + ")";
  for (const Projection &pr : pl.Proj) {
    switch (pr.K) {
    case Projection::Field: s += "." + std::to_string(pr.Arg); break;
    case Projection::Deref: s += ".*"; break;
    case Projection::Index: s += "[]"; break;
    case Projection::Variant: s += "@" + std::to_string(pr.Arg); break;
    }
  }
  return s;
}

void dumpBody(const Body &b, std::ostream &os) {
  os << "fn " << (b.Closure ? "<closure>" : (b.Fn ? b.Fn->Name : "?"))
     << " {\n";
  for (size_t i = 0; i < b.Locals.size(); ++i) {
    const Local &l = b.Locals[i];
    os << "  %" << i << " ";
    switch (l.K) {
    case Local::User: os << "let "; break;
    case Local::Param: os << "param "; break;
    case Local::Temp: os << "tmp "; break;
    case Local::Return: os << "result "; break;
    case Local::Global: os << "global "; break;
    case Local::Capture: os << "capture "; break;
    }
    os << l.Name << ": " << (l.Ty ? l.Ty->toString() : "?");
    if (l.RefLike)
      os << (l.RefMutable ? " [&var]" : " [&]");
    if (l.Owned)
      os << " [owned]";
    if (l.Origin != kNone)
      os << " '" << l.Origin;
    os << "\n";
  }
  for (size_t i = 0; i < b.Origins.size(); ++i) {
    const Origin &o = b.Origins[i];
    os << "  '" << i << " ";
    switch (o.K) {
    case Origin::Local: os << "local of %" << o.Owner; break;
    case Origin::Placeholder: os << "placeholder for param " << o.Param; break;
    case Origin::Global: os << "global"; break;
    case Origin::Result: os << "result"; break;
    case Origin::Untracked: os << "untracked"; break;
    }
    os << "\n";
  }
  for (size_t bi = 0; bi < b.Blocks.size(); ++bi) {
    const Block &blk = b.Blocks[bi];
    os << "  bb" << bi << ":";
    if (!blk.Preds.empty()) {
      os << "  // from";
      for (BlockId p : blk.Preds)
        os << " bb" << p;
    }
    os << "\n";
    for (const Stmt &s : blk.Stmts) {
      os << "    ";
      switch (s.K) {
      case Stmt::Assign: os << spellPlace(b, s.Dst) << " = "; break;
      case Stmt::Borrow:
        os << spellPlace(b, s.Dst) << " = &"
           << (s.Loan != kNone && b.Loans[s.Loan].Mutable ? "var " : "")
           << spellPlace(b, s.Src) << "  // loan L" << s.Loan;
        break;
      case Stmt::Call:
        os << spellPlace(b, s.Dst) << " = call "
           << (s.Callee ? s.Callee->Name : "<value>") << "(";
        for (size_t i = 0; i < s.Args.size(); ++i)
          os << (i ? ", " : "") << spellPlace(b, s.Args[i]);
        os << ")";
        break;
      case Stmt::Activate: os << "activate L" << s.Loan; break;
      case Stmt::Drop: os << "drop " << spellPlace(b, s.Dst); break;
      case Stmt::StorageLive: os << "live " << spellPlace(b, s.Dst); break;
      case Stmt::StorageDead: os << "dead " << spellPlace(b, s.Dst); break;
      case Stmt::FakeRead: os << "fakeread"; break;
      case Stmt::Nop: os << "nop"; break;
      }
      if (!s.Accesses.empty()) {
        os << "  {";
        for (size_t i = 0; i < s.Accesses.size(); ++i)
          os << (i ? ", " : "") << accessName(s.Accesses[i].A) << " "
             << spellPlace(b, s.Accesses[i].Place);
        os << "}";
      }
      for (const Subset &sub : s.Subsets)
        os << "  '" << sub.From << " ⊆ '" << sub.Into;
      for (OriginId o : s.Clears)
        os << "  clear '" << o;
      if (s.MoveOfHandle)
        os << "  [handle]";
      os << "\n";
    }
    os << "    ";
    switch (blk.Term.K) {
    case Terminator::Goto: os << "goto bb" << blk.Term.Succ[0]; break;
    case Terminator::Branch:
      os << "branch " << spellPlace(b, blk.Term.Tested) << " ? bb"
         << blk.Term.Succ[0] << " : bb" << blk.Term.Succ[1];
      break;
    case Terminator::Switch:
      os << "switch " << spellPlace(b, blk.Term.Tested) << " ->";
      for (BlockId t : blk.Term.Succ)
        os << " bb" << t;
      break;
    case Terminator::Return: os << "return"; break;
    case Terminator::Unreachable: os << "unreachable"; break;
    }
    os << "\n";
  }
  for (size_t i = 0; i < b.Loans.size(); ++i) {
    const Loan &l = b.Loans[i];
    os << "  L" << i << ": " << (l.Mutable ? "&var " : "&")
       << spellPlace(b, l.Place) << (l.TwoPhase ? " (two-phase)" : "")
       << " at bb" << l.At.Block << "[" << l.At.Index << "]\n";
  }
  os << "}\n";
  for (const auto &c : b.Closures)
    dumpBody(*c, os);
}

//===----------------------------------------------------------------------===//
// Checking
//===----------------------------------------------------------------------===//

namespace {

/// One body being checked: its graph and what the passes found.
struct Unit {
  FunctionDecl *Fn = nullptr;
  std::unique_ptr<Body> Graph;
  size_t QueueIndex = 0;
  bool Imported = false;
  std::vector<const FunctionDecl *> Callees;
  std::vector<Diagnostic> Findings;
  FnSummary Summary;
  /// `--dump-zombie` output, printed in queue order once every thread is
  /// done.
  std::string Dump;
};

/// Every function a body (and the closures inside it) calls.
void collectCallees(const Body &b, std::vector<const FunctionDecl *> &out) {
  for (const Block &blk : b.Blocks)
    for (const Stmt &s : blk.Stmts)
      if (s.K == Stmt::Call && s.Callee)
        out.push_back(s.Callee);
  for (const auto &c : b.Closures)
    collectCallees(*c, out);
}

Stats gStats;
std::mutex gStatsMutex;

struct Stopwatch {
  std::chrono::steady_clock::time_point Start = std::chrono::steady_clock::now();
  double lap() {
    auto now = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(now - Start).count();
    Start = now;
    return ms;
  }
};

/// Runs the passes over one body — closures first, since the parent's view
/// of a closure is only what it captures — and returns its summary.
FnSummary analyseUnit(Body &body, const SummaryTable &table,
                      DiagnosticEngine &diags, std::ostream *dump) {
  for (auto &c : body.Closures) {
    // A closure's result and captures are its own business; its summary
    // is not consulted (calls through values use the conservative rule).
    analyseUnit(*c, table, diags, nullptr);
  }
  Stopwatch watch;
  Stats local;
  applySummaries(body, table, diags);
  local.SummariesMs += watch.lap();
  MoveResults moves;
  analyseMoves(body, diags, moves);
  local.MovesMs += watch.lap();
  LoanResults loans;
  analyseLoans(body, diags, loans);
  local.LoansMs += watch.lap();
  if (loans.Skipped)
    local.Skipped = 1;
  FnSummary sum = inferSummary(body, loans, table, diags);
  local.InferMs += watch.lap();
  local.Bodies = 1;
  {
    std::lock_guard<std::mutex> lock(gStatsMutex);
    gStats.SummariesMs += local.SummariesMs;
    gStats.MovesMs += local.MovesMs;
    gStats.LoansMs += local.LoansMs;
    gStats.InferMs += local.InferMs;
    gStats.Bodies += 1;
    gStats.Skipped += local.Skipped;
  }
  if (dump) {
    dumpBody(body, *dump);
    *dump << "  summary:";
    if (sum.ResultOwned)
      *dump << " result owned;";
    else {
      *dump << " result from";
      for (const FromEntry &e : sum.ResultFrom) {
        if (e.Global) {
          *dump << " global";
          continue;
        }
        *dump << " " << e.Param;
        for (unsigned f : e.Path)
          *dump << "." << f;
      }
      if (sum.ResultUntracked)
        *dump << " untracked";
      *dump << ";";
    }
    for (size_t i = 0; i < sum.Views.size(); ++i) {
      *dump << " view[" << i << "]=";
      if (sum.Views[i].Whole)
        *dump << "whole";
      else {
        *dump << "{";
        for (const View::Entry &e : sum.Views[i].Entries) {
          *dump << " ";
          for (size_t k = 0; k < e.Path.size(); ++k)
            *dump << (k ? "." : "") << e.Path[k];
          *dump << (e.Write ? "(w)" : "(r)");
        }
        *dump << " }";
      }
    }
    *dump << "\n";
  }
  return sum;
}

/// Tarjan's strongly connected components over `succ`, in reverse
/// topological order (callees before callers).
struct Tarjan {
  const std::vector<std::vector<size_t>> &Succ;
  std::vector<int> Index, Low;
  std::vector<bool> OnStack;
  std::vector<size_t> Stack;
  std::vector<std::vector<size_t>> Components;
  int Next = 0;
  explicit Tarjan(const std::vector<std::vector<size_t>> &succ)
      : Succ(succ), Index(succ.size(), -1), Low(succ.size(), 0),
        OnStack(succ.size(), false) {}
  void run() {
    for (size_t v = 0; v < Succ.size(); ++v)
      if (Index[v] < 0)
        visit(v);
  }
  void visit(size_t v) {
    // Iterative, so a deep call chain does not become a deep C++ stack.
    struct Frame { size_t V; size_t I; };
    std::vector<Frame> frames{{v, 0}};
    Index[v] = Low[v] = Next++;
    Stack.push_back(v);
    OnStack[v] = true;
    while (!frames.empty()) {
      Frame &f = frames.back();
      if (f.I < Succ[f.V].size()) {
        size_t w = Succ[f.V][f.I++];
        if (Index[w] < 0) {
          Index[w] = Low[w] = Next++;
          Stack.push_back(w);
          OnStack[w] = true;
          frames.push_back({w, 0});
        } else if (OnStack[w]) {
          Low[f.V] = std::min(Low[f.V], Index[w]);
        }
        continue;
      }
      if (Low[f.V] == Index[f.V]) {
        std::vector<size_t> comp;
        size_t w;
        do {
          w = Stack.back();
          Stack.pop_back();
          OnStack[w] = false;
          comp.push_back(w);
        } while (w != f.V);
        Components.push_back(std::move(comp));
      }
      size_t done = f.V;
      frames.pop_back();
      if (!frames.empty())
        Low[frames.back().V] = std::min(Low[frames.back().V], Low[done]);
    }
  }
};

} // namespace

const Stats &lastStats() { return gStats; }

void checkProgram(const std::vector<FunctionDecl *> &queue,
                  const std::vector<FunctionDecl *> &imported,
                  DiagnosticEngine &diags, DumpKind dump, bool reportStdlib) {
  std::vector<Unit> units;
  std::unordered_map<const FunctionDecl *, size_t> unitOf;
  for (size_t i = 0; i < queue.size(); ++i) {
    if (!queue[i] || !queue[i]->Body || unitOf.count(queue[i]))
      continue;
    Unit u;
    u.Fn = queue[i];
    u.QueueIndex = i;
    unitOf[u.Fn] = units.size();
    units.push_back(std::move(u));
  }
  for (FunctionDecl *fn : imported) {
    if (!fn || !fn->Body || unitOf.count(fn))
      continue;
    Unit u;
    u.Fn = fn;
    u.Imported = true;
    u.QueueIndex = queue.size() + units.size();
    unitOf[fn] = units.size();
    units.push_back(std::move(u));
  }
  if (units.empty())
    return;

  gStats = Stats{};
  // 1. Lower every body, in parallel; lowering reports nothing.
  parallelFor(units.size(), [&](size_t i) {
    Stopwatch watch;
    units[i].Graph = lowerBody(units[i].Fn, diags);
    collectCallees(*units[i].Graph, units[i].Callees);
    double ms = watch.lap();
    std::lock_guard<std::mutex> lock(gStatsMutex);
    gStats.LowerMs += ms;
  });

  // 2. Who needs whom: an edge to every callee that is in the program.
  //    (A callee with everything written down could be skipped — that is
  //    the "explicit annotations delete edges" refinement.)
  std::vector<std::vector<size_t>> succ(units.size());
  for (size_t i = 0; i < units.size(); ++i) {
    std::set<size_t> seen;
    for (const FunctionDecl *c : units[i].Callees) {
      auto it = unitOf.find(c);
      if (it == unitOf.end() || it->second == i || !seen.insert(it->second).second)
        continue;
      succ[i].push_back(it->second);
    }
  }
  Tarjan t(succ);
  t.run();
  std::vector<size_t> compOf(units.size());
  for (size_t c = 0; c < t.Components.size(); ++c)
    for (size_t v : t.Components[c])
      compOf[v] = c;

  // 3. Components in dependency order, as many at once as there are cores.
  //    A component is ready when every component it calls into is done; a
  //    pool of workers takes ready components off a queue, so nothing waits
  //    for a "level" to finish that it does not itself depend on.
  SummaryTable table;
  for (Unit &u : units)
    table.add(u.Fn);
  table.seal();

  const size_t nc = t.Components.size();
  std::vector<std::vector<size_t>> dependents(nc);
  std::vector<size_t> pending(nc, 0);
  for (size_t c = 0; c < nc; ++c) {
    std::set<size_t> callees;
    for (size_t v : t.Components[c])
      for (size_t w : succ[v])
        if (compOf[w] != c)
          callees.insert(compOf[w]);
    pending[c] = callees.size();
    for (size_t d : callees)
      dependents[d].push_back(c);
  }

  auto analyseComponent = [&](size_t c) {
    const std::vector<size_t> &members = t.Components[c];
    bool recursive = members.size() > 1;
    if (!recursive)
      for (const FunctionDecl *w : units[members[0]].Callees)
        if (w == units[members[0]].Fn)
          recursive = true;
    // A recursive group starts from its declarations and is re-read until
    // its summaries stop changing. Only the last round's findings are kept.
    const size_t maxRounds = recursive ? 8 : 1;
    for (size_t round = 0; round < maxRounds; ++round) {
      bool changed = false;
      for (size_t v : members) {
        Unit &u = units[v];
        u.Findings.clear();
        // The graph is rewritten by `applySummaries`; a fresh lowering per
        // round keeps rounds independent.
        if (round > 0)
          u.Graph = lowerBody(u.Fn, diags);
        std::ostringstream dumped;
        diags.beginCapture(&u.Findings);
        FnSummary sum = analyseUnit(
            *u.Graph, table, diags,
            dump == DumpKind::Zombie && !u.Imported ? &dumped : nullptr);
        diags.endCapture();
        u.Dump = dumped.str();
        if (round == 0 || !(u.Summary == sum))
          changed = true;
        u.Summary = sum;
        table.publish(u.Fn, sum);
      }
      if (round > 0) {
        std::lock_guard<std::mutex> lock(gStatsMutex);
        gStats.Rounds += 1;
      }
      if (!changed)
        break;
    }
  };

  {
    std::mutex m;
    std::condition_variable cv;
    std::deque<size_t> ready;
    size_t done = 0;
    for (size_t c = 0; c < nc; ++c)
      if (pending[c] == 0)
        ready.push_back(c);
    auto worker = [&]() {
      for (;;) {
        size_t c;
        {
          std::unique_lock<std::mutex> lock(m);
          cv.wait(lock, [&] { return !ready.empty() || done == nc; });
          if (ready.empty())
            return;
          c = ready.front();
          ready.pop_front();
        }
        analyseComponent(c);
        std::lock_guard<std::mutex> lock(m);
        ++done;
        for (size_t d : dependents[c])
          if (--pending[d] == 0)
            ready.push_back(d);
        cv.notify_all();
      }
    };
    const unsigned threads =
        static_cast<unsigned>(std::min<size_t>(parallelism(), nc));
    std::vector<std::thread> pool;
    for (unsigned i = 1; i < threads; ++i)
      pool.emplace_back(worker);
    worker();
    for (std::thread &th : pool)
      th.join();
  }

  // 4. Findings in queue order, whatever order the threads ran in.
  std::vector<size_t> order(units.size());
  for (size_t i = 0; i < units.size(); ++i)
    order[i] = i;
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    return units[a].QueueIndex < units[b].QueueIndex;
  });
  // The same source is checked once per instantiation of a generic; a
  // finding is said once, not once per type argument.
  std::set<std::pair<unsigned, std::string>> said;
  for (size_t i : order) {
    if (units[i].Imported)
      continue;
    const std::string &mod = units[i].Fn->ModulePath;
    bool stdlib = mod == "std" || mod.rfind("std::", 0) == 0;
    if (stdlib && !reportStdlib)
      continue;
    if (!units[i].Dump.empty())
      std::cout << units[i].Dump;
    std::vector<Diagnostic> once;
    for (const Diagnostic &d : units[i].Findings)
      if (said.insert({d.Range.begin().raw(), d.Message}).second)
        once.push_back(d);
    diags.replay(once);
  }
}

} // namespace zombie
} // namespace rune
