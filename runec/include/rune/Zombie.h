//===- Zombie.h - The Zombie borrow checker --------------------*- C++ -*-===//
//
// Zombie is the second of Rune's two memory models. Under `--memory arc` a
// class, a `String`, a closure or a mark object carries a reference count and
// `Ownership.cpp` offers advice. Under `--memory zombie` nothing is counted:
// every such value has exactly one owner, moves when it is handed on, is
// destroyed at the end of the scope that owns it, and is reached from
// anywhere else only through a borrow that this checker proves is gone before
// the owner is.
//
// The checker is place-based and flow-sensitive, after the model Niko
// Matsakis describes in "Borrow checking without lifetimes" and the Polonius
// formulation of Rust's borrow checker:
//
//   * a *place* is a local and a path into it (`v`, `v.a`, `r.x`, `arr[…]`);
//   * a *loan* is one borrow of a place, shared or exclusive;
//   * an *origin* is the set of loans a reference may hold — one per local
//     that carries a reference, one placeholder per reference parameter;
//   * a loan is *live* at a point when some live origin still contains it,
//     and an access that conflicts with a live loan is an error.
//
// Where a returned reference is borrowed from, and which fields a function
// touches through each reference parameter, are *inferred* from the body and
// recorded as the function's summary; callers are checked against summaries,
// so the whole program is checked one function at a time, in call-graph
// order, in parallel within a level. `from` clauses and views written in the
// source are contracts the summary is checked against.
//
// This header is the whole of what the rest of the compiler sees. `Zombie.cpp`
// orchestrates; `ZombieIR.cpp` lowers a body to the graph in `ZombieIR.h`;
// `ZombieMoves.cpp`, `ZombieLoans.cpp` and `ZombieSignatures.cpp` answer the
// questions; `ZombieDiagnostics.cpp` says what was found.
//
//===----------------------------------------------------------------------===//
#ifndef RUNE_ZOMBIE_H
#define RUNE_ZOMBIE_H

#include "rune/AST.h"
#include "rune/Diagnostics.h"
#include "rune/Driver.h"

#include <functional>
#include <ostream>
#include <vector>

namespace rune {

class TypeContext;

namespace zombie {

struct Body;

/// Lowers one function body to the checker's graph. Exposed for `--dump-zombie`.
std::unique_ptr<Body> lowerBody(FunctionDecl *fn, DiagnosticEngine &diags);

/// Prints a lowered body: locals, blocks, statements, loans.
void dumpBody(const Body &body, std::ostream &os);

/// True when values of `t` hold a borrow somewhere inside.
bool carriesReference(Type *t);
/// True when a value of `t` has to be destroyed under Zombie.
bool needsDrop(Type *t);
/// True when a `for` loop over a cursor of type `t` that is somebody's place
/// advances a copy of it rather than driving it where it is: plain data,
/// nothing owned, nothing borrowed. The one rule both the borrow checker
/// and the code generator ask, in either memory model.
bool cursorIsCopied(Type *t);

/// Where the checker's time went in the last `checkProgram`, in
/// milliseconds of thread time, and how many bodies took each route. For
/// `--time`.
struct Stats {
  double LowerMs = 0, SummariesMs = 0, MovesMs = 0, LoansMs = 0, InferMs = 0;
  size_t Bodies = 0, Skipped = 0, Rounds = 0;
};
const Stats &lastStats();

/// Checks every queued body. `queue` is what Sema collected — every
/// non-generic function and instantiation with a body, in the order they
/// were checked, which is the order diagnostics come back in. `imported`
/// holds bodies from libraries: read for their summaries, never reported on.
///
/// Findings are always errors: the code generator relies on them. Findings
/// inside the standard library are kept only with `reportStdlib` — its
/// bodies are still read for their summaries.
///
/// A body `deferrable` answers true for — the standard library's, an
/// imported library's — is analysed only when the program's own bodies reach
/// it through calls: nothing else consults its summary, and what it would
/// find is the library's business, checked when the library is (see
/// `--zombie-whole-stdlib`). Those left out are returned. Their lowering
/// still records what the code generator reads, so whatever of them it
/// emits goes through `prepareDeferred` first.
std::vector<FunctionDecl *>
checkProgram(const std::vector<FunctionDecl *> &queue,
             const std::vector<FunctionDecl *> &imported,
             DiagnosticEngine &diags, DumpKind dump, bool reportStdlib,
             const std::function<bool(const FunctionDecl *)> &deferrable = {});

/// Lowers a body `checkProgram` deferred and runs the move pass over it —
/// the two parts of the checker that leave answers on the AST for the code
/// generator. Neither consults another function's summary, so the result is
/// what a whole-program check would have left. Findings are reported when
/// `report` is set, and dropped otherwise.
void prepareDeferred(FunctionDecl *fn, DiagnosticEngine &diags, bool report);

} // namespace zombie
} // namespace rune

#endif
