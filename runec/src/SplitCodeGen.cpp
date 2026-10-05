//===- SplitCodeGen.cpp - Building one module's machine code in pieces -*- C++ -*-===//
//
// See SplitCodeGen.h for the shape of it. Two rules carry the correctness:
//
//   * a piece refers to a definition that lives elsewhere only by a name the
//     linker can see, so a local symbol named from another piece is made a
//     hidden external one; and
//   * a piece defines only what lives there, plus its own copy of any private
//     constant that is pure data — a string, a table of numbers — whose
//     address nobody can tell apart from another copy's.
//
//===----------------------------------------------------------------------===//
#include "rune/SplitCodeGen.h"

#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <numeric>
#include <vector>

namespace rune {

namespace {

using Mask = uint32_t;
constexpr unsigned MaxPieces = 32;

/// The global values `root` names, through constant expressions and
/// aggregates. False when one of them is a `blockaddress`, which names a
/// place inside a particular function and so cannot cross a cut.
bool globalsIn(const llvm::Constant *root,
               llvm::SmallPtrSetImpl<const llvm::Constant *> &seen,
               std::vector<llvm::GlobalValue *> &out) {
  std::vector<const llvm::Constant *> work{root};
  while (!work.empty()) {
    const llvm::Constant *c = work.back();
    work.pop_back();
    if (!seen.insert(c).second)
      continue;
    if (isa<llvm::BlockAddress>(c))
      return false;
    if (auto *gv = dyn_cast<llvm::GlobalValue>(c)) {
      out.push_back(const_cast<llvm::GlobalValue *>(gv));
      continue;
    }
    for (const llvm::Use &op : c->operands())
      if (auto *inner = dyn_cast<llvm::Constant>(op.get()))
        work.push_back(inner);
  }
  return true;
}

/// A private constant that is nothing but data: every piece that reads it
/// may carry a copy, because no one can observe which copy they read.
bool isDuplicable(const llvm::GlobalVariable &g) {
  if (!g.hasLocalLinkage() || !g.isConstant() || !g.hasGlobalUnnamedAddr() ||
      g.hasComdat() || g.isThreadLocal() || !g.hasInitializer() ||
      g.hasSection())
    return false;
  llvm::SmallPtrSet<const llvm::Constant *, 8> seen;
  std::vector<llvm::GlobalValue *> named;
  return globalsIn(g.getInitializer(), seen, named) && named.empty();
}

/// Makes `gv` something another object can name, without letting anything
/// outside the program see it.
void externalise(llvm::GlobalValue &gv) {
  gv.setLinkage(llvm::GlobalValue::ExternalLinkage);
  gv.setVisibility(llvm::GlobalValue::HiddenVisibility);
  gv.setDSOLocal(true);
}

} // namespace

bool splitModule(llvm::Module &m, unsigned units, CodegenSplit &out) {
  units = std::min(units, MaxPieces);
  if (units < 2 || !m.getModuleInlineAsm().empty() || !m.alias_empty() ||
      !m.ifunc_empty())
    return false;

  // 1. What has to stay together: a function with a body, and everything
  //    that shares its COMDAT.
  struct Group {
    std::vector<llvm::Function *> Members;
    size_t Weight = 0;
  };
  std::vector<Group> groups;
  std::unordered_map<const llvm::Comdat *, size_t> groupOfComdat;
  std::unordered_map<const llvm::GlobalValue *, size_t> groupOf;
  for (llvm::Function &f : m) {
    if (f.isDeclaration())
      continue;
    size_t g = groups.size();
    if (const llvm::Comdat *c = f.getComdat()) {
      auto [it, fresh] = groupOfComdat.try_emplace(c, groups.size());
      g = it->second;
      if (fresh)
        groups.emplace_back();
    } else {
      groups.emplace_back();
    }
    groups[g].Members.push_back(&f);
    // A function costs its instructions, plus the fixed price of being a
    // function at all: a prologue, an epilogue, a symbol.
    groups[g].Weight += f.getInstructionCount() + 16;
    groupOf[&f] = g;
  }
  if (groups.size() < 2)
    return false;
  units = static_cast<unsigned>(std::min<size_t>(units, groups.size()));

  // 2. Balanced by size: the heaviest group first, each to whichever piece
  //    is lightest so far. Ties go by position, so the cut is the same every
  //    time the same module is cut.
  std::vector<size_t> order(groups.size());
  std::iota(order.begin(), order.end(), 0);
  std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    return groups[a].Weight > groups[b].Weight;
  });
  std::vector<size_t> load(units, 0);
  std::vector<unsigned> pieceOfGroup(groups.size(), 0);
  for (size_t g : order) {
    unsigned best = 0;
    for (unsigned p = 1; p < units; ++p)
      if (load[p] < load[best])
        best = p;
    pieceOfGroup[g] = best;
    load[best] += groups[g].Weight;
  }

  // 3. What each function names. Read once; it decides where the globals
  //    go and which locals have to be seen across a cut.
  std::unordered_map<const llvm::GlobalValue *, Mask> home;
  for (size_t g = 0; g < groups.size(); ++g)
    for (llvm::Function *f : groups[g].Members)
      home[f] = Mask(1) << pieceOfGroup[g];

  std::vector<std::pair<unsigned, std::vector<llvm::GlobalValue *>>> sites;
  for (llvm::Function &f : m) {
    if (f.isDeclaration())
      continue;
    const unsigned piece = pieceOfGroup[groupOf[&f]];
    llvm::SmallPtrSet<const llvm::Constant *, 32> seen;
    std::vector<llvm::GlobalValue *> named;
    for (const llvm::Instruction &inst : llvm::instructions(f))
      for (const llvm::Use &op : inst.operands())
        if (auto *c = dyn_cast<llvm::Constant>(op.get()))
          if (!globalsIn(c, seen, named))
            return false;
    // The personality routine and prefix data hang off the function itself.
    for (const llvm::Constant *c :
         {f.hasPersonalityFn() ? f.getPersonalityFn() : nullptr,
          f.hasPrefixData() ? f.getPrefixData() : nullptr,
          f.hasPrologueData() ? f.getPrologueData() : nullptr})
      if (c && !globalsIn(c, seen, named))
        return false;
    sites.push_back({piece, std::move(named)});
  }

  // A global variable lives with the first function that uses it; one that
  // shares a COMDAT with a function lives with that function; the table of
  // constructors and anything nobody uses lives in the first piece. A
  // constant that is pure data is copied to every piece that reads it.
  std::unordered_map<const llvm::Comdat *, Mask> comdatHome;
  for (const auto &[c, g] : groupOfComdat)
    comdatHome[c] = Mask(1) << pieceOfGroup[g];
  std::unordered_map<const llvm::GlobalVariable *, bool> duplicable;
  for (llvm::GlobalVariable &g : m.globals())
    if (!g.isDeclaration())
      duplicable[&g] = isDuplicable(g);
  for (const auto &[piece, named] : sites)
    for (llvm::GlobalValue *gv : named) {
      auto *g = dyn_cast<llvm::GlobalVariable>(gv);
      if (!g || g->isDeclaration())
        continue;
      Mask &h = home[g];
      if (duplicable[g])
        h |= Mask(1) << piece;
      else if (!h)
        h = Mask(1) << piece;
    }
  for (llvm::GlobalVariable &g : m.globals()) {
    if (g.isDeclaration() || duplicable[&g])
      continue;
    if (const llvm::Comdat *c = g.getComdat()) {
      auto [it, fresh] = comdatHome.try_emplace(c, home[&g] ? home[&g] : 1);
      home[&g] = it->second;
      continue;
    }
    if (g.hasAppendingLinkage() || !home[&g])
      home[&g] = 1;
  }

  // Initialisers name things too, from every piece their variable is in.
  const size_t functionSites = sites.size();
  for (llvm::GlobalVariable &g : m.globals()) {
    if (g.isDeclaration() || duplicable[&g])
      continue;
    llvm::SmallPtrSet<const llvm::Constant *, 32> seen;
    std::vector<llvm::GlobalValue *> named;
    if (!globalsIn(g.getInitializer(), seen, named))
      return false;
    const Mask h = home[&g];
    for (unsigned p = 0; p < units; ++p)
      if (h & (Mask(1) << p))
        sites.push_back({p, named});
  }
  // A string a type's metadata names is copied to wherever that metadata is.
  for (size_t i = functionSites; i < sites.size(); ++i)
    for (llvm::GlobalValue *gv : sites[i].second)
      if (auto *g = dyn_cast<llvm::GlobalVariable>(gv))
        if (!g->isDeclaration() && duplicable[g])
          home[g] |= Mask(1) << sites[i].first;

  // 4. A local named from a piece it does not live in is made visible to
  //    the linker — and to nothing beyond the program.
  for (const auto &[piece, named] : sites)
    for (llvm::GlobalValue *gv : named) {
      if (!gv->hasLocalLinkage() || gv->isDeclaration())
        continue;
      auto *g = dyn_cast<llvm::GlobalVariable>(gv);
      if (g && duplicable[g])
        continue;
      if (!(home[gv] & (Mask(1) << piece)))
        externalise(*gv);
    }

  // 5. Every definition by name — the one thing that survives the trip
  //    through bitcode — and the module, once.
  out.Units = units;
  out.Home.clear();
  for (llvm::GlobalObject &go : m.global_objects()) {
    if (go.isDeclaration())
      continue;
    if (!go.hasName())
      go.setName("__rune.unnamed");
    out.Home[go.getName().str()] = home[&go] ? home[&go] : 1;
  }
  out.Bitcode.clear();
  llvm::raw_svector_ostream os(out.Bitcode);
  llvm::WriteBitcodeToFile(m, os);
  return true;
}

std::unique_ptr<llvm::Module> loadPiece(const CodegenSplit &split,
                                        unsigned index,
                                        llvm::LLVMContext &ctx,
                                        std::string &err) {
  llvm::MemoryBufferRef buffer(
      llvm::StringRef(split.Bitcode.data(), split.Bitcode.size()),
      "codegen unit");
  // Lazily: only the bodies that live here are ever read.
  llvm::Expected<std::unique_ptr<llvm::Module>> loaded =
      llvm::getLazyBitcodeModule(buffer, ctx);
  if (!loaded) {
    err = llvm::toString(loaded.takeError());
    return nullptr;
  }
  std::unique_ptr<llvm::Module> m = std::move(*loaded);
  if (llvm::Error e = m->materializeMetadata()) {
    err = llvm::toString(std::move(e));
    return nullptr;
  }
  const Mask mine = Mask(1) << index;
  auto livesHere = [&](const llvm::GlobalObject &go) {
    auto it = split.Home.find(go.getName().str());
    return it != split.Home.end() && (it->second & mine);
  };

  for (llvm::Function &f : *m) {
    if (!f.isMaterializable() && f.isDeclaration())
      continue;
    if (livesHere(f)) {
      if (llvm::Error e = f.materialize()) {
        err = llvm::toString(std::move(e));
        return nullptr;
      }
      continue;
    }
    // Somebody else's: a declaration of it is all this piece needs.
    f.deleteBody();
    f.setComdat(nullptr);
  }
  for (llvm::GlobalVariable &g : llvm::make_early_inc_range(m->globals())) {
    if (g.isDeclaration() || livesHere(g))
      continue;
    if (g.use_empty()) {
      g.eraseFromParent();
      continue;
    }
    // What the debug information says about it is said where it lives.
    g.eraseMetadata(llvm::LLVMContext::MD_dbg);
    g.setInitializer(nullptr);
    g.setComdat(nullptr);
    g.setLinkage(llvm::GlobalValue::ExternalLinkage);
  }
  // What is left unnamed and unused here is not this piece's to declare.
  for (llvm::Function &f : llvm::make_early_inc_range(*m))
    if (f.isDeclaration() && f.use_empty() && !f.isIntrinsic())
      f.eraseFromParent();
  return m;
}

} // namespace rune
