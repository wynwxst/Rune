//===- ZombieSignatures.cpp - Summaries at function boundaries --*- C++ -*-===//
//
// A caller never reads a callee's body. It reads the callee's *summary*:
// where the result borrows from, and which fields the callee touches through
// each reference parameter. Summaries are inferred from bodies and checked
// against what was written (`from` clauses, views); an explicit clause is
// the contract and the inferred one must fit inside it.
//
// Two directions live here. `applySummaries` rewrites a body's calls in the
// light of its callees' summaries — subset edges into the result, derived
// loans for the borrows a call keeps, receiver borrows narrowed to the fields
// a view names. `inferSummary` reads what the loan analysis found and what
// the body did through its parameters, and produces the summary the body's
// own callers will see.
//
//===----------------------------------------------------------------------===//
#include "rune/Zombie.h"
#include "rune/ZombieIR.h"
#include "rune/ZombieInternal.h"

namespace rune {
namespace zombie {

namespace {

/// Every `from` clause written anywhere inside a type, outside function
/// types (which own theirs).
void collectClauses(const TypeRepr *t, std::vector<const OriginClause *> &out) {
  if (!t)
    return;
  if (t->Origin)
    out.push_back(t->Origin.get());
  switch (t->Kind) {
  case NodeKind::PointerType:
    collectClauses(cast<PointerTypeRepr>(t)->Pointee.get(), out);
    break;
  case NodeKind::ArrayType:
    collectClauses(cast<ArrayTypeRepr>(t)->Element.get(), out);
    break;
  case NodeKind::SliceType:
    collectClauses(cast<SliceTypeRepr>(t)->Element.get(), out);
    break;
  case NodeKind::OptionalType:
    collectClauses(cast<OptionalTypeRepr>(t)->Element.get(), out);
    break;
  case NodeKind::UniqType:
    collectClauses(cast<UniqTypeRepr>(t)->Element.get(), out);
    break;
  case NodeKind::TupleType:
    for (auto &e : cast<TupleTypeRepr>(t)->Elements)
      collectClauses(e.get(), out);
    break;
  case NodeKind::NamedType:
    for (auto &a : cast<NamedTypeRepr>(t)->GenericArgs)
      collectClauses(a.get(), out);
    break;
  default:
    break;
  }
}

bool isRefParam(const Param &p) {
  if (p.IsSelf)
    return p.SelfByRef;
  return p.Ty && p.Ty->is(TypeKind::Pointer) && !p.Ty->isRawPointer();
}

bool paramWritable(const Param &p) {
  if (p.IsSelf)
    return p.SelfMutable;
  return p.Ty && p.Ty->is(TypeKind::Pointer) && p.Ty->isMutablePointer();
}

} // namespace

//===----------------------------------------------------------------------===//
// Declared summaries
//===----------------------------------------------------------------------===//

FnSummary declaredSummary(const FunctionDecl *fn) {
  FnSummary s;
  s.Declared = true;
  if (!fn)
    return s;
  const size_t n = fn->Params.size();
  s.ParamFrom.assign(n, {});
  s.Views.assign(n, View{});
  s.ViewExplicit.assign(n, false);

  for (size_t i = 0; i < n; ++i) {
    const Param &p = fn->Params[i];
    std::vector<const OriginClause *> pclauses;
    collectClauses(p.TypeAnnotation.get(), pclauses);
    for (const OriginClause *c : pclauses)
      for (const OriginPlace &pl : c->Places) {
        FromEntry e;
        if (pl.Root == OriginPlace::RootKind::Global)
          e.Global = true;
        else if (pl.Root == OriginPlace::RootKind::Param) {
          e.Param = static_cast<unsigned>(pl.ParamIndex);
          e.Path = pl.FieldPath;
        } else
          continue;
        s.ParamFrom[i].push_back(e);
      }
    if (p.HasView) {
      s.ViewExplicit[i] = true;
      s.Views[i].Whole = false;
      for (const FieldPathRepr &f : p.View) {
        View::Entry e;
        e.Path = f.Resolved;
        e.Write = paramWritable(p);
        s.Views[i].Entries.push_back(e);
      }
    }
  }

  Type *ret = fn->Ty ? fn->Ty->result() : nullptr;
  s.ResultOwned = !ret || !carriesReference(ret);
  std::vector<const OriginClause *> clauses;
  collectClauses(fn->ReturnType.get(), clauses);
  if (!clauses.empty()) {
    s.ResultExplicit = true;
    for (const OriginClause *c : clauses)
      for (const OriginPlace &pl : c->Places) {
        FromEntry e;
        if (pl.Root == OriginPlace::RootKind::Global)
          e.Global = true;
        else if (pl.Root == OriginPlace::RootKind::Param) {
          e.Param = static_cast<unsigned>(pl.ParamIndex);
          e.Path = pl.FieldPath;
        } else
          continue;
        s.ResultFrom.push_back(e);
      }
  } else if (!s.ResultOwned) {
    // The defaults when nothing can be inferred: `self`, or the one
    // reference parameter there is.
    int only = -1;
    int count = 0;
    for (size_t i = 0; i < n; ++i)
      if (isRefParam(fn->Params[i])) {
        if (fn->Params[i].IsSelf) {
          only = static_cast<int>(i);
          count = 1;
          break;
        }
        only = static_cast<int>(i);
        ++count;
      }
    if (count == 1) {
      FromEntry e;
      e.Param = static_cast<unsigned>(only);
      s.ResultFrom.push_back(e);
    } else {
      s.ResultUntracked = true; // nothing to go on
    }
  }
  return s;
}

const FnSummary &summaryFor(const FunctionDecl *fn, const SummaryTable &table,
                            FnSummary &scratch) {
  if (const FnSummary *found = table.find(fn))
    return *found;
  scratch = declaredSummary(fn);
  return scratch;
}

//===----------------------------------------------------------------------===//
// Applying summaries to calls
//===----------------------------------------------------------------------===//

void applySummaries(Body &body, const SummaryTable &table,
                    DiagnosticEngine &diags) {
  (void)diags;
  auto originOf = [&](PlaceId p) -> OriginId {
    if (p == kNone)
      return kNone;
    return body.Locals[body.Places.get(p).Root].Origin;
  };
  for (BlockId bi = 0; bi < body.Blocks.size(); ++bi) {
    for (uint32_t si = 0; si < body.Blocks[bi].Stmts.size(); ++si) {
      Stmt &s = body.Blocks[bi].Stmts[si];
      if (s.K != Stmt::Call)
        continue;
      OriginId into = originOf(s.Dst);

      // A call through a value: the result may borrow from anything given.
      if (!s.Callee) {
        if (into != kNone) {
          for (PlaceId a : s.Args) {
            OriginId o = originOf(a);
            if (o != kNone && o != into)
              s.Subsets.push_back({o, into});
          }
          OriginId o = originOf(s.CalleeValue);
          if (o != kNone && o != into)
            s.Subsets.push_back({o, into});
          // An implicit borrow of a place handed to such a call is kept.
          for (PlaceAccess &a : s.Accesses)
            if (a.A == Access::Shared || a.A == Access::Mut) {
              Loan l;
              l.Place = a.Place;
              l.Mutable = a.A == Access::Mut;
              l.Range = a.Range;
              l.At = Location{bi, si};
              LoanId id = static_cast<LoanId>(body.Loans.size());
              body.Loans.push_back(l);
              s.Issues.push_back(id);
              body.HasBorrows = true;
            }
        }
        continue;
      }

      FnSummary scratch;
      const FnSummary &sum = summaryFor(s.Callee, table, scratch);
      const std::vector<Param> &params = s.Callee->Params;

      // A caller-side `from` on a parameter (`item: &Item from list`) becomes a
      // requirement to check once loans are known: the argument's origin must
      // stay within the origins of the arguments it names (E0283).
      for (size_t i = 0; i < sum.ParamFrom.size() && i < s.Args.size(); ++i) {
        if (sum.ParamFrom[i].empty() || s.Args[i] == kNone)
          continue;
        Stmt::FromRequirement req;
        req.Arg = static_cast<unsigned>(i);
        req.Name = i < params.size() ? params[i].Name.c_str() : "argument";
        req.Range = s.Range;
        for (const FromEntry &e : sum.ParamFrom[i]) {
          if (e.Global) {
            req.Global = true;
            if (!req.FromName)
              req.FromName = "global";
          } else if (e.Param < s.Args.size() && s.Args[e.Param] != kNone) {
            req.FromArgs.push_back(e.Param);
            if (!req.FromName && e.Param < params.size())
              req.FromName = params[e.Param].Name.c_str();
          }
        }
        if (!req.FromArgs.empty() || req.Global)
          s.FromReqs.push_back(std::move(req));
      }

      // Which argument was borrowed implicitly for the call, and how.
      auto accessOf = [&](PlaceId arg) -> PlaceAccess * {
        if (arg == kNone)
          return nullptr;
        for (PlaceAccess &a : s.Accesses)
          if (a.Place == arg &&
              (a.A == Access::Shared || a.A == Access::Mut ||
               a.A == Access::Reserve))
            return &a;
        // A receiver reached through a reference is borrowed at `*r`.
        PlaceId deref = body.Places.project(arg, Projection{Projection::Deref, 0});
        for (PlaceAccess &a : s.Accesses)
          if (a.Place == deref &&
              (a.A == Access::Shared || a.A == Access::Mut ||
               a.A == Access::Reserve))
            return &a;
        return nullptr;
      };

      // What each argument's implicit borrow covers, after the callee's
      // view has narrowed it: one place per field the callee touches, or
      // the whole pointee when the callee reaches all of it.
      struct Covered {
        PlaceId Place;
        bool Write;
      };
      std::vector<std::vector<Covered>> covered(s.Args.size());
      for (size_t i = 0; i < params.size() && i < s.Args.size(); ++i) {
        PlaceId arg = s.Args[i];
        if (arg == kNone)
          continue;
        // A `&var` receiver was reserved by the statement before this one;
        // the reservation is the access to narrow, and the loan behind it.
        // An explicit `&place` handed straight to the call is the same
        // shape: the temporary holding it is used nowhere else, so its
        // loan may be cut down to what the callee touches.
        Loan *reserved = nullptr;
        PlaceAccess *acc = nullptr;
        if (i == 0 && s.Loan != kNone && body.Loans[s.Loan].TwoPhase) {
          reserved = &body.Loans[s.Loan];
          Location at = reserved->At;
          if (at.Block < body.Blocks.size() &&
              at.Index < body.Blocks[at.Block].Stmts.size()) {
            Stmt &borrow = body.Blocks[at.Block].Stmts[at.Index];
            for (PlaceAccess &a : borrow.Accesses)
              if (a.A == Access::Reserve)
                acc = &a;
          }
        } else {
          acc = accessOf(arg);
          if (!acc) {
            const Place &ap = body.Places.get(arg);
            const Local &al = body.Locals[ap.Root];
            if (al.K == Local::Temp && ap.Proj.empty()) {
              // Find the borrow that made the temporary, if one did.
              for (Block &blk : body.Blocks) {
                for (Stmt &st : blk.Stmts)
                  if (st.K == Stmt::Borrow && st.Dst == arg &&
                      st.Loan != kNone) {
                    reserved = &body.Loans[st.Loan];
                    for (PlaceAccess &a : st.Accesses)
                      if (a.A == Access::Shared || a.A == Access::Mut)
                        acc = &a;
                  }
                if (acc)
                  break;
              }
            }
          }
        }
        if (!acc)
          continue;
        PlaceId base = acc->Place;
        Type *bt = placeType(body, base);
        if (bt && bt->is(TypeKind::Class))
          base = body.Places.project(base, Projection{Projection::Deref, 0});
        bool writes = acc->A == Access::Mut || acc->A == Access::Reserve;
        if (i >= sum.Views.size() || sum.Views[i].Whole) {
          covered[i].push_back({base, writes});
          continue;
        }
        const View &v = sum.Views[i];
        std::vector<PlaceAccess> narrowed;
        std::vector<PlaceId> written, all;
        for (const View::Entry &e : v.Entries) {
          PlaceId p = base;
          for (unsigned f : e.Path)
            p = body.Places.project(p, Projection{Projection::Field, f});
          bool w = e.Write && writes;
          PlaceAccess na = *acc;
          na.Place = p;
          if (acc->A == Access::Reserve)
            na.A = w ? Access::Reserve : Access::Shared;
          else if (acc->A == Access::Mut)
            na.A = w ? Access::Mut : Access::Shared;
          else
            na.A = Access::Shared;
          narrowed.push_back(na);
          covered[i].push_back({p, w});
          all.push_back(p);
          if (w)
            written.push_back(p);
        }
        // Replace the whole-place access by the narrowed ones, wherever it
        // lives.
        std::vector<PlaceAccess> &list =
            reserved ? body.Blocks[reserved->At.Block]
                           .Stmts[reserved->At.Index]
                           .Accesses
                     : s.Accesses;
        size_t idx = static_cast<size_t>(acc - list.data());
        list.erase(list.begin() + static_cast<long>(idx));
        list.insert(list.begin() + static_cast<long>(idx), narrowed.begin(),
                    narrowed.end());
        // The loan behind the borrow covers only what the callee touches
        // — for a two-phase reservation, only what it writes; the
        // activation is of the same places.
        if (reserved) {
          if (reserved->TwoPhase) {
            if (written.empty()) {
              reserved->Mutable = false;
              reserved->Alt = all;
            } else {
              reserved->Alt = written;
            }
            for (PlaceAccess &a : s.Accesses)
              if (a.A == Access::Activate)
                a.Place = !written.empty() ? written[0]
                          : !all.empty()   ? all[0]
                                           : a.Place;
          } else {
            reserved->Alt = all;
          }
        }
      }

      if (into == kNone)
        continue;

      // Where the result borrows from: the argument's own loans, and a
      // loan of what the callee said it keeps — cut down to what its view
      // says it touches.
      for (const FromEntry &e : sum.ResultFrom) {
        if (e.Global) {
          s.Subsets.push_back({body.GlobalOrigin, into});
          continue;
        }
        unsigned pi = e.Param;
        if (pi >= s.Args.size() || pi >= params.size())
          continue;
        PlaceId arg = s.Args[pi];
        if (arg == kNone)
          continue;
        OriginId o = originOf(arg);
        if (o != kNone && o != into)
          s.Subsets.push_back({o, into});
        if (covered[pi].empty())
          continue;
        // The place the clause names, under the argument's pointee.
        PlaceId fromPlace = covered[pi].front().Place;
        {
          // `covered` places all start at the pointee; strip a view's
          // fields back off by recomputing from the access base.
          const Place &cp = body.Places.get(fromPlace);
          Place basePlace = cp;
          // The base is the shortest covered place's prefix without view
          // fields: recover it from the access itself.
          PlaceAccess *acc = nullptr;
          for (PlaceAccess &a : s.Accesses)
            if (a.Place == arg || body.Places.isPrefixOf(arg, a.Place)) {
              acc = &a;
              break;
            }
          (void)acc;
          (void)basePlace;
        }
        PlaceId base = arg;
        {
          Type *at = placeType(body, arg);
          if (at && at->is(TypeKind::Pointer))
            base = body.Places.project(base, Projection{Projection::Deref, 0});
          Type *bt = placeType(body, base);
          if (bt && bt->is(TypeKind::Class))
            base = body.Places.project(base, Projection{Projection::Deref, 0});
        }
        fromPlace = base;
        for (unsigned f : e.Path)
          fromPlace = body.Places.project(fromPlace,
                                          Projection{Projection::Field, f});
        for (const Covered &c : covered[pi]) {
          PlaceId loanPlace;
          if (body.Places.isPrefixOf(fromPlace, c.Place))
            loanPlace = c.Place;
          else if (body.Places.isPrefixOf(c.Place, fromPlace))
            loanPlace = fromPlace;
          else
            continue;
          Loan l;
          l.Place = loanPlace;
          l.Mutable = c.Write;
          l.Range = s.Range;
          for (const PlaceAccess &a : s.Accesses)
            if (a.Place == c.Place)
              l.Range = a.Range;
          l.At = Location{bi, si};
          LoanId id = static_cast<LoanId>(body.Loans.size());
          body.Loans.push_back(l);
          s.Issues.push_back(id);
          body.HasBorrows = true;
        }
      }
      if (sum.ResultUntracked)
        s.Subsets.push_back({body.UntrackedOrigin, into});
    }
  }
}

//===----------------------------------------------------------------------===//
// Inference
//===----------------------------------------------------------------------===//

FnSummary inferSummary(Body &body, const LoanResults &loans,
                       const SummaryTable &table, DiagnosticEngine &diags) {
  FunctionDecl *fn = body.Fn;
  FnSummary declared = declaredSummary(fn);
  FnSummary out = declared;
  out.Declared = false;
  const size_t n = fn ? fn->Params.size() : 0;

  Type *ret = fn && fn->Ty ? fn->Ty->result() : nullptr;
  out.ResultOwned = !ret || !carriesReference(ret);

  //=== The result ========================================================//
  if (!out.ResultOwned) {
    if (declared.ResultExplicit) {
      // The body must fit inside what was written.
      for (const FromEntry &inferred : loans.ResultFrom) {
        // A global borrow outlives everything a `from` clause could name, so
        // it satisfies any of them: the caller keeps the named place alive,
        // which is more than a `'static` result needs. A longer lifetime
        // coerces to a shorter one.
        bool covered = inferred.Global;
        for (const FromEntry &d : declared.ResultFrom) {
          if (inferred.Global && d.Global)
            covered = true;
          if (!inferred.Global && !d.Global && d.Param == inferred.Param &&
              d.Path.size() <= inferred.Path.size() &&
              std::equal(d.Path.begin(), d.Path.end(), inferred.Path.begin()))
            covered = true;
        }
        if (covered)
          continue;
        std::string what = inferred.Global ? "a global"
                           : inferred.Param < n
                               ? "'" + fn->Params[inferred.Param].Name + "'"
                               : "a parameter";
        std::string said;
        for (size_t i = 0; i < declared.ResultFrom.size(); ++i) {
          const FromEntry &d = declared.ResultFrom[i];
          if (i)
            said += ", ";
          said += d.Global ? "global"
                           : (d.Param < n ? fn->Params[d.Param].Name : "?");
        }
        auto d = diags.error(fn->ReturnType ? fn->ReturnType->Range
                                            : fn->NameRange,
                             "this returns a borrow of {}, but the signature "
                             "says the result borrows from {}",
                             what, said.empty() ? "nothing" : said);
        d.note("a `from` clause is a contract: the body may borrow from "
               "less than it says, never more");
        d.code(281);
      }
      out.ResultFrom = declared.ResultFrom;
      out.ResultUntracked = false;
    } else {
      out.ResultFrom = loans.ResultFrom;
      out.ResultUntracked = loans.ResultUntracked;
      if (loans.ResultUntracked && !(fn && fn->IsZombieTrusted)) {
        auto d = diags.error(fn->ReturnType ? fn->ReturnType->Range
                                            : fn->NameRange,
                             "'{}' returns a borrow but does not say from "
                             "what",
                             fn->Name);
        d.note("the result points at something reached through a raw "
               "pointer, so nothing about it can be inferred");
        d.note("add `from <parameter>` or `from global` to the result type");
        d.code(282);
      }
    }
  }

  //=== Views =============================================================//
  out.Views.assign(n, View{});
  out.ViewExplicit.assign(n, false);
  for (size_t i = 0; i < n; ++i) {
    const Param &p = fn->Params[i];
    out.ViewExplicit[i] = declared.ViewExplicit[i];
    if (!isRefParam(p) && !(p.IsSelf && p.SelfByRef)) {
      out.Views[i].Whole = true;
      continue;
    }
    LocalId local = static_cast<LocalId>(i);
    View v;
    v.Whole = false;
    auto touch = [&](const std::vector<unsigned> &path, bool write) {
      for (View::Entry &e : v.Entries)
        if (e.Path == path) {
          e.Write |= write;
          return;
        }
      View::Entry e;
      e.Path = path;
      e.Write = write;
      v.Entries.push_back(e);
    };
    auto pathUnder = [&](PlaceId place, std::vector<unsigned> &path,
                         bool &whole) -> bool {
      const Place &pl = body.Places.get(place);
      if (pl.Root != local)
        return false;
      const Local &root = body.Locals[local];
      // The reference value itself, not what it reaches: `self` on a class
      // is the object, an ordinary `&T` local is a pointer whose copying
      // is handled by the caller of this lambda.
      if (pl.Proj.empty()) {
        whole = true;
        return true;
      }
      if (!root.RefLike && pl.Proj[0].K != Projection::Deref) {
        // A field of the pointer value itself — not possible for `&T`.
        whole = true;
        return true;
      }
      size_t k = 0;
      while (k < pl.Proj.size() && pl.Proj[k].K == Projection::Deref)
        ++k;
      for (; k < pl.Proj.size(); ++k) {
        if (pl.Proj[k].K == Projection::Field)
          path.push_back(pl.Proj[k].Arg);
        else
          break;
      }
      whole = path.empty();
      return true;
    };
    for (const Block &blk : body.Blocks) {
      for (const Stmt &s : blk.Stmts) {
        for (const PlaceAccess &a : s.Accesses) {
          if (a.Place == kNone)
            continue;
          std::vector<unsigned> path;
          bool whole = false;
          if (!pathUnder(a.Place, path, whole))
            continue;
          bool write = a.A == Access::Write || a.A == Access::Mut ||
                       a.A == Access::Reserve || a.A == Access::Move ||
                       a.A == Access::Drop || a.A == Access::Activate;
          if (a.A == Access::StorageDead)
            continue;
          const Place &pl = body.Places.get(a.Place);
          if (pl.Proj.empty() && !body.Locals[local].RefLike) {
            // The reference value itself: copying it lets anything be
            // reached through the copy.
            if (a.A == Access::Read || a.A == Access::Move)
              v.Whole = true;
            continue;
          }
          if (whole)
            v.Whole = true;
          else
            touch(path, write);
        }
        if (s.K == Stmt::Call) {
          FnSummary scratch;
          const FnSummary *cs =
              s.Callee ? &summaryFor(s.Callee, table, scratch) : nullptr;
          for (size_t j = 0; j < s.Args.size(); ++j) {
            PlaceId arg = s.Args[j];
            if (arg == kNone)
              continue;
            std::vector<unsigned> path;
            bool whole = false;
            if (!pathUnder(arg, path, whole))
              continue;
            const Place &pl = body.Places.get(arg);
            bool refItself = pl.Proj.empty() && !body.Locals[local].RefLike;
            if (!cs || j >= cs->Views.size() || cs->Views[j].Whole) {
              if (refItself || whole)
                v.Whole = true;
              else
                touch(path, true);
              continue;
            }
            for (const View::Entry &e : cs->Views[j].Entries) {
              std::vector<unsigned> full = path;
              full.insert(full.end(), e.Path.begin(), e.Path.end());
              touch(full, e.Write);
            }
          }
        }
      }
    }
    if (declared.ViewExplicit[i]) {
      // The body must stay inside the view it declared.
      const View &d = declared.Views[i];
      auto coveredBy = [&](const std::vector<unsigned> &path) {
        for (const View::Entry &e : d.Entries)
          if (e.Path.size() <= path.size() &&
              std::equal(e.Path.begin(), e.Path.end(), path.begin()))
            return true;
        return false;
      };
      std::string viewText = "{ ";
      for (size_t k = 0; k < p.View.size(); ++k) {
        if (k)
          viewText += ", ";
        for (size_t q = 0; q < p.View[k].Path.size(); ++q)
          viewText += (q ? "." : "") + p.View[k].Path[q];
      }
      viewText += " }";
      if (v.Whole) {
        auto e = diags.error(p.Range,
                             "'{}' reaches all of '{}', but its view is "
                             "`{}`",
                             fn->Name, p.Name, viewText);
        e.note("something passes the whole parameter on, or calls a "
               "method that touches all of it");
        e.note("name every field the body touches, or drop the view");
        e.code(299);
      } else {
        for (const View::Entry &e : v.Entries)
          if (!coveredBy(e.Path)) {
            std::string field;
            Type *t = p.Ty;
            for (unsigned f : e.Path) {
              Type *inner = t;
              while (inner && inner->is(TypeKind::Pointer))
                inner = inner->pointee();
              NominalDecl *nd = inner && inner->isNominal() ? inner->nominal()
                                                            : nullptr;
              std::string name = std::to_string(f);
              Type *next = nullptr;
              if (nd) {
                std::vector<NominalDecl *> chain{nd};
                if (auto *c = dyn_cast<ClassDecl>(static_cast<Decl *>(nd)))
                  for (ClassDecl *sc = c->Super; sc; sc = sc->Super)
                    chain.push_back(sc);
                for (NominalDecl *cn : chain)
                  for (const auto &fd : cn->Fields)
                    if (fd->Index == f) {
                      name = fd->Name;
                      next = fd->Ty;
                    }
              }
              field += (field.empty() ? "" : ".") + name;
              t = next;
            }
            auto err = diags.error(
                p.Range, "'{}' {} '{}.{}', which is outside its view `{}`",
                fn->Name, e.Write ? "writes" : "reads", p.Name, field,
                viewText);
            err.note("a view is a promise to callers about which fields "
                     "the body reaches; add the field, or drop the view");
            err.code(299);
            break;
          }
      }
      out.Views[i] = d;
    } else {
      out.Views[i] = v;
    }
  }
  return out;
}

} // namespace zombie
} // namespace rune
