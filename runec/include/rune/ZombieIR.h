//===- ZombieIR.h - The borrow checker's view of a function -----*- C++ -*-===//
//
// Zombie does not check the tree Sema produced. It checks a flat control-flow
// graph lowered from it, in which every value has a *place* — a local and a
// path of projections into it — and every statement says which places it
// reads, writes, moves, borrows or destroys. That is the shape the questions
// have: "is this place still borrowed here?", "was this moved on the way?"
// are questions about places at points, and a tree answers neither easily.
//
// The types here are data. `ZombieIR.cpp` builds a `Body` from a
// `FunctionDecl`; `ZombieMoves.cpp`, `ZombieLoans.cpp` and
// `ZombieSignatures.cpp` read it; nothing outside `Zombie.cpp` sees it.
//
//===----------------------------------------------------------------------===//
#ifndef RUNE_ZOMBIE_IR_H
#define RUNE_ZOMBIE_IR_H

#include "rune/AST.h"
#include "rune/Type.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace rune {
namespace zombie {

using LocalId = uint32_t;
using PlaceId = uint32_t;
using LoanId = uint32_t;
using OriginId = uint32_t;
using BlockId = uint32_t;
using PathId = uint32_t;
constexpr uint32_t kNone = ~0u;

/// A point in the body: a statement, or the block's terminator when `Index`
/// is the statement count.
struct Location {
  BlockId Block = kNone;
  uint32_t Index = 0;
  bool operator==(const Location &o) const {
    return Block == o.Block && Index == o.Index;
  }
  bool operator<(const Location &o) const {
    return Block != o.Block ? Block < o.Block : Index < o.Index;
  }
};

//===----------------------------------------------------------------------===//
// Places
//===----------------------------------------------------------------------===//

/// One step from a value into part of it.
struct Projection {
  enum Kind : uint8_t {
    Field,   ///< `.name` / `.0`: `Arg` is the field or element index
    Deref,   ///< `*p`: through a borrow
    Index,   ///< `[i]`: some element — which one is not tracked
    Variant, ///< the payload of enum variant `Arg`
  } K;
  uint32_t Arg = 0;
  bool operator==(const Projection &o) const { return K == o.K && Arg == o.Arg; }
};

/// A local and a path into it: `v`, `v.a.b`, `(*p).x`, `arr[…]`.
struct Place {
  LocalId Root = kNone;
  std::vector<Projection> Proj;
  bool operator==(const Place &o) const {
    return Root == o.Root && Proj == o.Proj;
  }
};

/// Every distinct place mentioned in one body, numbered. Overlap and prefix
/// questions are answered structurally, so nothing here needs a type.
class PlaceTable {
public:
  PlaceId intern(const Place &p);
  const Place &get(PlaceId id) const { return Places[id]; }
  size_t size() const { return Places.size(); }

  /// True when `a`'s path is a prefix of `b`'s (including equality): `v` is
  /// a prefix of `v.x`. `Index` steps match any `Index` step.
  bool isPrefixOf(PlaceId a, PlaceId b) const;
  /// True when the two places may share memory: one is a prefix of the other.
  bool overlaps(PlaceId a, PlaceId b) const;
  /// True when reaching the place goes through a `Deref`.
  bool throughDeref(PlaceId id) const;
  /// The place with the last projection removed, or `kNone` for a root.
  PlaceId parent(PlaceId id);
  /// `id` with one more projection.
  PlaceId project(PlaceId id, Projection p);
  /// The longest prefix of `id` that does not go through a `Deref` — what a
  /// move out of `id` leaves partially moved.
  PlaceId ownedPrefix(PlaceId id);

private:
  std::vector<Place> Places;
  std::map<std::vector<uint64_t>, PlaceId> Index;
};

//===----------------------------------------------------------------------===//
// Locals, origins and loans
//===----------------------------------------------------------------------===//

/// Something a place can be rooted at.
struct Local {
  enum Kind : uint8_t {
    User,     ///< a binding the program wrote
    Param,    ///< a parameter, `self` included
    Temp,     ///< a value the lowering had to name
    Return,   ///< the result slot
    Global,   ///< a global, one local per declaration
    Capture,  ///< inside a closure body: a captured variable
  } K = Temp;
  VarDecl *Var = nullptr;
  Type *Ty = nullptr;
  std::string Name;
  SourceRange Range;
  /// The local holds a borrow of something without its type saying so: a
  /// class-typed `&self`, a pattern binding out of borrowed content (§3.5 of
  /// the plan), a captured variable. Its projections start with a `Deref`.
  bool RefLike = false;
  bool RefMutable = false;
  /// The origin of what the local holds, when its type carries a reference
  /// (a borrow, a struct with reference fields, a closure with captures);
  /// `kNone` otherwise.
  OriginId Origin = kNone;
  /// True when the value needs destroying at the end of its scope.
  bool Owned = false;
  /// Parameter index, for `Param`; capture index, for `Capture`.
  unsigned Index = 0;
};

/// Where a reference may have come from: a set of loans. One per local that
/// carries a reference, plus one placeholder per reference-typed parameter,
/// one for globals, and one for the result.
struct Origin {
  enum Kind : uint8_t {
    Local,       ///< owned by a local of this body
    Placeholder, ///< a parameter: whatever the caller lent
    Global,      ///< something a global owns
    Result,      ///< the value being returned
    Untracked,   ///< reached through a raw pointer: satisfies anything
  } K = Local;
  LocalId Owner = kNone;   ///< `Local`: the local; `Placeholder`: the param
  unsigned Param = 0;      ///< `Placeholder`: parameter index
};

/// One borrow the body takes, or stands in for.
struct Loan {
  PlaceId Place = kNone;
  bool Mutable = false;
  /// A `&var` receiver: reserved when evaluated, exclusive only once the
  /// call activates it. Until then it behaves as a shared loan.
  bool TwoPhase = false;
  Location At;
  SourceRange Range;
  /// Set for the synthetic loan a placeholder origin holds: `Param` is the
  /// parameter it stands for. Such a loan is never "taken" anywhere.
  bool Placeholder = false;
  unsigned Param = 0;
  /// Set for the synthetic loan of the global origin.
  bool Global = false;
  /// When a view narrows a receiver borrow, the loan covers these places
  /// instead of `Place`: one per field the callee touches.
  std::vector<PlaceId> Alt;
};

//===----------------------------------------------------------------------===//
// Statements
//===----------------------------------------------------------------------===//

/// What a statement does to a place.
enum class Access : uint8_t {
  Read,        ///< reads the value where it is (a copy of a scalar, a test)
  Write,       ///< overwrites the whole place
  Move,        ///< takes the value out, leaving the place uninitialised
  Shared,      ///< takes a `&` borrow
  Mut,         ///< takes a `&var` borrow
  Reserve,     ///< takes a two-phase `&var` borrow (shared until activated)
  Activate,    ///< the reserved borrow becomes exclusive
  Drop,        ///< destroys the value at the end of its scope
  StorageDead, ///< the place stops existing
};

struct PlaceAccess {
  PlaceId Place = kNone;
  Access A = Access::Read;
  SourceRange Range;
  const Expr *Source = nullptr;   ///< the expression, for `MovedOut` marks
};

/// A subset edge `'a ⊆ 'b` introduced at a statement: everything `a` may
/// point at, `b` may point at from here on.
struct Subset {
  OriginId From = kNone;
  OriginId Into = kNone;
};

struct Stmt {
  enum Kind : uint8_t {
    Assign,      ///< `Dst` receives a value; `Accesses` say what was read
    Borrow,      ///< `Dst = &Src` (or `&var`): issues `Loan`
    Call,        ///< `Dst = callee(operands)`; `Callee`/`Site` for summaries
    Activate,    ///< the two-phase `Loan` becomes exclusive
    Drop,        ///< scope end: destroy `Dst`
    StorageLive, ///< `Dst`'s root comes into scope
    StorageDead, ///< `Dst`'s root leaves scope
    FakeRead,    ///< a read that borrows nothing: a `match` discriminant
    Nop,
  } K = Nop;
  PlaceId Dst = kNone;
  PlaceId Src = kNone;
  LoanId Loan = kNone;
  std::vector<PlaceAccess> Accesses;
  std::vector<Subset> Subsets;
  /// Origins whose old contents this statement discards: a whole-local write
  /// of a reference forgets what it used to point at.
  std::vector<OriginId> Clears;
  /// `Call`: what is being called, and the places its arguments were in, in
  /// parameter order (`self` first for a method), `kNone` where an argument
  /// was a constant. Filled in by lowering; `ZombieSignatures` reads the
  /// callee's summary and appends `Subsets` and view-shaped loans.
  FunctionDecl *Callee = nullptr;
  const CallExpr *Site = nullptr;
  std::vector<PlaceId> Args;
  /// `Call` through a value rather than a declaration: the place holding it.
  PlaceId CalleeValue = kNone;
  /// `Assign` of a struct literal whose type has internal references: the
  /// literal, so the loop can be checked (§3.3 of the plan).
  const StructLitExpr *Literal = nullptr;
  /// True when the value moved into `Dst` came out of a heap handle's
  /// pointee — loans under `Src`'s `Deref` follow it (§4.4 re-rooting).
  bool MoveOfHandle = false;
  /// Loans this statement issues into `Dst`'s origin besides `Loan`: the
  /// derived loans of a call's implicit borrows (see `applySummaries`).
  std::vector<LoanId> Issues;
  /// Written inside `unsafe { }`.
  bool Unsafe = false;
  /// The final write of an aggregate literal: every part has already been
  /// written into `Dst`, so this marks the whole initialised and kills
  /// nothing — the loans its parts carried in are exactly what it holds.
  bool Aggregate = false;
  /// An internal-reference field being written (`body: &String from
  /// self.text`): what it holds must borrow from `InternalTarget`, the
  /// sibling field's object, and nothing else (E0297).
  PlaceId InternalTarget = kNone;
  const FieldDecl *InternalField = nullptr;
  /// `Call`: a caller-side `from` on a parameter (`item: &Item from list`).
  /// Each entry says argument `Arg`'s origin must stay within the origins of
  /// the arguments at `FromArgs` (and `global` when `Global`) — checked once
  /// the loans are known (E0283). Filled in by `applySummaries`.
  struct FromRequirement {
    unsigned Arg = 0;
    std::vector<unsigned> FromArgs;
    bool Global = false;
    const char *Name = nullptr;      ///< the constrained parameter's name
    const char *FromName = nullptr;  ///< the place it must borrow from
    SourceRange Range;
  };
  std::vector<FromRequirement> FromReqs;
  SourceRange Range;
  const Node *Source = nullptr;
};

struct Terminator {
  enum Kind : uint8_t {
    Goto,
    Branch,   ///< two successors: `Succ[0]` when the condition holds
    Switch,   ///< one successor per case, the last is the default
    Return,
    Unreachable,
  } K = Unreachable;
  std::vector<BlockId> Succ;
  /// `Branch`/`Switch`: the place tested (a `Read` access).
  PlaceId Tested = kNone;
  SourceRange Range;
};

struct Block {
  std::vector<Stmt> Stmts;
  Terminator Term;
  std::vector<BlockId> Preds;
};

//===----------------------------------------------------------------------===//
// The body
//===----------------------------------------------------------------------===//

/// What a borrow checked body remembers for the code generator: per local,
/// whether its drop is unconditional, never, or guarded by a flag per moved
/// path.
struct DropPlan {
  enum Kind : uint8_t { Always, Never, Flagged };
  struct Path {
    std::vector<Projection> Proj;   ///< empty for the whole local
    Kind K = Always;
  };
  std::vector<Path> Paths;
};

struct Body {
  FunctionDecl *Fn = nullptr;
  /// Set for a closure body, which is lowered on its own with the closure's
  /// captures as locals.
  const ClosureExpr *Closure = nullptr;
  std::vector<Local> Locals;
  std::vector<Block> Blocks;
  PlaceTable Places;
  std::vector<Loan> Loans;
  std::vector<Origin> Origins;
  OriginId GlobalOrigin = kNone;
  OriginId UntrackedOrigin = kNone;
  LocalId ReturnLocal = kNone;
  BlockId Entry = 0;
  /// The blocks every `Return` terminator lives in.
  std::vector<BlockId> Exits;
  /// Bodies of the closures written inside this one, checked with it.
  std::vector<std::unique_ptr<Body>> Closures;
  /// Where each local is mentioned, for "used again here" notes.
  std::unordered_map<LocalId, std::vector<SourceRange>> Uses;

  /// True when the body took any borrow, held any reference-carrying local,
  /// or moved anything owned — the cheap test for "nothing to check".
  bool HasBorrows = false;
  bool HasMoves = false;

  LocalId localFor(const VarDecl *v) const {
    auto it = ByVar.find(v);
    return it == ByVar.end() ? kNone : it->second;
  }
  std::unordered_map<const VarDecl *, LocalId> ByVar;

  std::string spell(PlaceId p) const;
  std::string spell(const Place &p) const;
};

/// The type of what `p` holds, following the projections; null when the
/// path cannot be typed (a raw place, an error).
Type *placeType(const Body &b, PlaceId p);

/// A dense bit set over `[0, n)`, the currency of every dataflow here.
class BitSet {
public:
  BitSet() = default;
  explicit BitSet(size_t n) : Words((n + 63) / 64), N(n) {}
  void resize(size_t n) { Words.assign((n + 63) / 64, 0); N = n; }
  size_t size() const { return N; }
  bool test(size_t i) const { return (Words[i / 64] >> (i % 64)) & 1; }
  void set(size_t i) { Words[i / 64] |= uint64_t(1) << (i % 64); }
  void reset(size_t i) { Words[i / 64] &= ~(uint64_t(1) << (i % 64)); }
  void clear() { for (auto &w : Words) w = 0; }
  bool any() const { for (auto w : Words) if (w) return true; return false; }
  bool operator==(const BitSet &o) const { return Words == o.Words; }
  bool operator!=(const BitSet &o) const { return Words != o.Words; }
  /// `*this |= o`; true when something was added.
  bool unite(const BitSet &o) {
    bool changed = false;
    for (size_t i = 0; i < Words.size() && i < o.Words.size(); ++i) {
      uint64_t before = Words[i];
      Words[i] |= o.Words[i];
      changed |= Words[i] != before;
    }
    return changed;
  }
  void intersect(const BitSet &o) {
    for (size_t i = 0; i < Words.size(); ++i)
      Words[i] &= i < o.Words.size() ? o.Words[i] : 0;
  }
  void subtract(const BitSet &o) {
    for (size_t i = 0; i < Words.size() && i < o.Words.size(); ++i)
      Words[i] &= ~o.Words[i];
  }
  template <typename F> void forEach(F &&fn) const {
    for (size_t w = 0; w < Words.size(); ++w) {
      uint64_t bits = Words[w];
      while (bits) {
        unsigned b = __builtin_ctzll(bits);
        fn(w * 64 + b);
        bits &= bits - 1;
      }
    }
  }

private:
  std::vector<uint64_t> Words;
  size_t N = 0;
};

} // namespace zombie
} // namespace rune

#endif
