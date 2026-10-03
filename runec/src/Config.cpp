#include "rune/Config.h"

#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>

#include <algorithm>

namespace rune {

namespace {

/// The name Rune uses for an operating system, which is the one people write
/// rather than the one the triple spells. A triple says `darwin`, `macosx` or
/// `apple`; everybody writing a condition means `macos`.
std::string osName(const llvm::Triple &t) {
  if (t.isOSWindows())
    return "windows";
  if (t.isMacOSX())
    return "macos";
  if (t.isiOS())
    return "ios";
  if (t.isAndroid())
    return "android";
  if (t.isOSLinux())
    return "linux";
  if (t.isOSFreeBSD())
    return "freebsd";
  if (t.isOSOpenBSD())
    return "openbsd";
  if (t.isOSNetBSD())
    return "netbsd";
  if (t.isOSSolaris())
    return "solaris";
  if (t.isWasm())
    return "wasi";
  return t.getOSName().str();
}

/// Likewise for the architecture: the canonical name, not the spelling.
std::string archName(const llvm::Triple &t) {
  switch (t.getArch()) {
  case llvm::Triple::aarch64:
  case llvm::Triple::aarch64_be:
    return "aarch64";
  case llvm::Triple::x86_64:
    return "x86_64";
  case llvm::Triple::x86:
    return "x86";
  case llvm::Triple::arm:
  case llvm::Triple::armeb:
  case llvm::Triple::thumb:
  case llvm::Triple::thumbeb:
    return "arm";
  case llvm::Triple::riscv32:
    return "riscv32";
  case llvm::Triple::riscv64:
    return "riscv64";
  case llvm::Triple::wasm32:
    return "wasm32";
  case llvm::Triple::wasm64:
    return "wasm64";
  case llvm::Triple::ppc64:
  case llvm::Triple::ppc64le:
    return "powerpc64";
  default:
    return t.getArchName().str();
  }
}

std::string familyName(const llvm::Triple &t) {
  if (t.isOSWindows())
    return "windows";
  if (t.isWasm())
    return "wasm";
  return "unix";
}

const char *safetyName(SafetyLevel s) {
  switch (s) {
  case SafetyLevel::None: return "none";
  case SafetyLevel::Minimal: return "minimal";
  default: return "full";
  }
}

/// How close two names are, so a misspelling can be pointed at the right one.
/// Ordinary edit distance, capped: a suggestion that is barely closer than
/// everything else is not a suggestion.
size_t editDistance(const std::string &a, const std::string &b) {
  std::vector<size_t> prev(b.size() + 1), cur(b.size() + 1);
  for (size_t j = 0; j <= b.size(); ++j)
    prev[j] = j;
  for (size_t i = 1; i <= a.size(); ++i) {
    cur[0] = i;
    for (size_t j = 1; j <= b.size(); ++j)
      cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1,
                         prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
    prev = cur;
  }
  return prev[b.size()];
}

/// Attaches a "did you mean" note when something in `cfg` is close enough.
void suggest(DiagBuilder &d, const std::string &name, const ConfigSet &cfg) {
  std::string best;
  size_t bestScore = 3; // further than this and it is a different word
  for (const std::string &known : cfg.known()) {
    size_t score = editDistance(name, known);
    if (score < bestScore) {
      bestScore = score;
      best = known;
    }
  }
  if (!best.empty())
    d.note("did you mean `{}`?", best);
}

/// One `key == "value"` or `key != "value"`.
///
/// The left side names something about the build and the right side is what it
/// might be. Written the other way round it reads backwards, so both orders
/// are accepted and mean the same thing.
/// The text a `@Config` comparison's right-hand side stands for, or "" when
/// the expression is not one a condition may compare against.
///
/// A manifest's `[config]` values are strings, numbers and booleans, so all
/// three are written the way they were written there — and a bare word is
/// accepted too, because `backend == vulkan` is what people write and there
/// is nothing else it could mean.
bool configValueText(const Expr *e, std::string &out) {
  if (const auto *s = dyn_cast<StringLitExpr>(e)) {
    out = s->Value;
    return true;
  }
  if (const auto *i = dyn_cast<IntLitExpr>(e)) {
    out = std::to_string(i->Value);
    if (i->IsNegated)
      out = "-" + out;
    return true;
  }
  if (const auto *b = dyn_cast<BoolLitExpr>(e)) {
    out = b->Value ? "true" : "false";
    return true;
  }
  if (const auto *r = dyn_cast<DeclRefExpr>(e)) {
    out = r->joined();
    return true;
  }
  return false;
}

/// One `key == value` or `key != value`, in either order.
///
/// The left side names something about the build and the right side is what
/// it might be. Written the other way round it reads backwards, so both
/// orders are accepted and mean the same thing.
bool evalComparison(const Expr *lhs, const Expr *rhs, bool wantEqual,
                    SourceRange range, const ConfigSet &cfg,
                    DiagnosticEngine &diags, bool &ok) {
  const auto *lhsName = dyn_cast<DeclRefExpr>(lhs);
  const auto *rhsName = dyn_cast<DeclRefExpr>(rhs);

  // Whichever side names a key this build knows is the key; the other is the
  // value. When neither does, the left is reported as the key it was meant
  // to be, which is where the mistake is.
  const DeclRefExpr *key = nullptr;
  const Expr *valueExpr = nullptr;
  if (lhsName && cfg.isKey(lhsName->joined())) {
    key = lhsName;
    valueExpr = rhs;
  } else if (rhsName && cfg.isKey(rhsName->joined())) {
    key = rhsName;
    valueExpr = lhs;
  } else {
    key = lhsName ? lhsName : rhsName;
    valueExpr = lhsName ? rhs : lhs;
  }

  std::string text;
  if (!key || !valueExpr || !configValueText(valueExpr, text)) {
    auto d = diags.error(range,
                         "a `@Config` comparison needs a name on one side and "
                         "a value on the other");
    d.note("for example `os == \"windows\"`, `backend == vulkan` or "
           "`api_level == 3`")
        .code(112);
    ok = false;
    return true;
  }

  const std::string name = key->joined();
  if (!cfg.isKey(name)) {
    auto d = diags.error(key->Range, "`{}` is not something `@Config` can "
                                     "compare", name);
    d.note("comparable keys are os, arch, family, pointer_width, endian, "
           "target, safety, memory, runtime, opt_level and overflow_checks, "
           "plus "
           "whatever `[config]` in the manifest names");
    d.note("a name set with `--cfg` is written on its own, not compared");
    suggest(d, name, cfg);
    d.code(112);
    ok = false;
    return true;
  }

  const bool equal = cfg.value(name) == text;
  return wantEqual ? equal : !equal;
}

bool eval(const Expr *e, const ConfigSet &cfg, DiagnosticEngine &diags,
          bool &ok) {
  if (!e) {
    ok = false;
    return true;
  }

  if (const auto *lit = dyn_cast<BoolLitExpr>(e))
    return lit->Value;

  // A bare name: set, or not.
  if (const auto *ref = dyn_cast<DeclRefExpr>(e)) {
    const std::string name = ref->joined();
    if (cfg.isKey(name)) {
      auto d = diags.error(e->Range,
                           "`{}` has a value, so it has to be compared", name);
      d.note("write `{} == \"{}\"` rather than `{}` on its own", name,
             cfg.value(name), name);
      d.code(112);
      ok = false;
      return true;
    }
    return cfg.isSet(name);
  }

  if (const auto *un = dyn_cast<UnaryExpr>(e)) {
    if (un->Op == UnaryOp::Not)
      return !eval(un->Operand.get(), cfg, diags, ok);
    auto d = diags.error(e->Range, "`@Config` understands `!`, `&&` and `||`, "
                                   "and nothing else");
    d.code(112);
    ok = false;
    return true;
  }

  if (const auto *bin = dyn_cast<BinaryExpr>(e)) {
    switch (bin->Op) {
    case BinaryOp::LogicalAnd: {
      // Both sides are evaluated whatever the first says, so a mistake in the
      // second is reported rather than short-circuited past.
      bool l = eval(bin->LHS.get(), cfg, diags, ok);
      bool r = eval(bin->RHS.get(), cfg, diags, ok);
      return l && r;
    }
    case BinaryOp::LogicalOr: {
      bool l = eval(bin->LHS.get(), cfg, diags, ok);
      bool r = eval(bin->RHS.get(), cfg, diags, ok);
      return l || r;
    }
    case BinaryOp::Eq:
    case BinaryOp::Ne:
      return evalComparison(bin->LHS.get(), bin->RHS.get(),
                            bin->Op == BinaryOp::Eq, bin->Range, cfg, diags,
                            ok);
    default:
      break;
    }
  }

  // A condition never assigns, so `backend = vulkan` can only mean the
  // comparison — and it is what people write, so it is what it means.
  if (const auto *as = dyn_cast<AssignExpr>(e))
    if (as->Op == AssignOp::Assign)
      return evalComparison(as->LHS.get(), as->RHS.get(), /*wantEqual=*/true,
                            as->Range, cfg, diags, ok);

  auto d = diags.error(e->Range, "this is not something `@Config` can answer");
  d.note("a condition is a key compared with a string, a name set with "
         "`--cfg`, or those joined by `&&`, `||` and `!`");
  d.code(112);
  ok = false;
  return true;
}

} // namespace

ConfigSet ConfigSet::forOptions(const CompilerOptions &opts) {
  ConfigSet cfg;
  const llvm::Triple triple(llvm::Triple::normalize(
      opts.TargetTriple.empty() ? llvm::sys::getDefaultTargetTriple()
                                : opts.TargetTriple));

  cfg.Values["os"] = osName(triple);
  cfg.Values["arch"] = archName(triple);
  cfg.Values["family"] = familyName(triple);
  cfg.Values["target"] = triple.str();
  cfg.Values["pointer_width"] =
      std::to_string(llvm::Triple::getArchPointerBitWidth(triple.getArch()));
  cfg.Values["endian"] = triple.isLittleEndian() ? "little" : "big";
  cfg.Values["safety"] = safetyName(opts.Safety);
  cfg.Values["memory"] = memoryModeName(opts.Memory);
  // `none` for `@runtime(none)`: no C library and no hosted runtime, so the
  // standard library keeps to what the freestanding one provides.
  cfg.Values["runtime"] = opts.Freestanding ? "none" : "hosted";
  cfg.Values["opt_level"] = std::to_string(opts.OptLevel);
  cfg.Values["overflow_checks"] = opts.overflowChecksEnabled() ? "on" : "off";
  for (const auto &kv : cfg.Values)
    cfg.Builtin.insert(kv.first);
  // How much of the freestanding runtime a `@runtime(none)` program brings:
  // `full`, or `minimal` — what the generated code cannot run without. A
  // package chooses, with `[config] freestanding_type = "minimal"` or
  // `--cfg freestanding_type=minimal`, so it is not one of the builtins.
  cfg.Values["freestanding_type"] = "full";

  if (opts.DebugInfo)
    cfg.Flags.insert("debug");
  for (const std::string &name : opts.ConfigFlags)
    cfg.Flags.insert(name);
  // A package's own keys, from `[config]` in its manifest or `--cfg k=v` on
  // the command line. They are compared exactly as `os` and `arch` are; the
  // builtin ones are what the target says and are not open to being told
  // otherwise, so those are kept.
  for (const auto &kv : opts.ConfigValues) {
    if (cfg.Builtin.count(kv.first))
      continue;
    cfg.Values[kv.first] = kv.second;
  }
  return cfg;
}

std::vector<std::string> ConfigSet::known() const {
  std::vector<std::string> all;
  for (const auto &kv : Values)
    all.push_back(kv.first);
  all.insert(all.end(), Flags.begin(), Flags.end());
  return all;
}

bool evaluateConfig(const Attribute &attr, const ConfigSet &cfg,
                    DiagnosticEngine &diags) {
  if (attr.Args.size() != 1) {
    auto d = diags.error(attr.Range, "`@Config` takes one condition");
    d.note("for example `@Config(os == \"windows\")`").code(112);
    return true;
  }
  bool ok = true;
  const bool result = eval(attr.Args[0].get(), cfg, diags, ok);
  // A condition nobody could evaluate has already been reported. Keeping the
  // declaration is the safer answer: deleting code on the strength of an
  // expression the compiler did not understand would turn one diagnostic into
  // a cascade of "cannot find" further on.
  return ok ? result : true;
}

namespace {

/// True when `d` carries a `@Config` that this build says no to.
bool excluded(const Decl *d, const ConfigSet &cfg, DiagnosticEngine &diags) {
  for (const Attribute &a : d->Attrs)
    if (a.Name == "Config" || a.Name == "config")
      if (!evaluateConfig(a, cfg, diags))
        return true;
  return false;
}

/// Drops the excluded entries of a vector of owning pointers, in place.
template <typename T>
void filter(std::vector<std::unique_ptr<T>> &items, const ConfigSet &cfg,
            DiagnosticEngine &diags) {
  items.erase(std::remove_if(items.begin(), items.end(),
                             [&](const std::unique_ptr<T> &p) {
                               return p && excluded(p.get(), cfg, diags);
                             }),
              items.end());
}

/// The members of a declaration that may carry a condition of their own: a
/// method that only exists on one platform, a field that only some builds
/// have, a variant only some targets can represent.
void applyToMembers(Decl *d, const ConfigSet &cfg, DiagnosticEngine &diags) {
  if (auto *nd = dyn_cast<NominalDecl>(d)) {
    filter(nd->Fields, cfg, diags);
    filter(nd->Methods, cfg, diags);
    // Fields are laid out by index, so the ones that survive have to be
    // renumbered — a gap would make every later field read the wrong memory.
    unsigned index = 0;
    for (auto &f : nd->Fields)
      f->Index = index++;
  }
  if (auto *e = dyn_cast<EnumDecl>(d))
    filter(e->Variants, cfg, diags);
  if (auto *e = dyn_cast<ExtendDecl>(d))
    filter(e->Methods, cfg, diags);
  if (auto *b = dyn_cast<BindDecl>(d))
    filter(b->Methods, cfg, diags);
  if (auto *mk = dyn_cast<MarkDecl>(d))
    filter(mk->Methods, cfg, diags);
  if (auto *ex = dyn_cast<ExternDecl>(d)) {
    filter(ex->Functions, cfg, diags);
    filter(ex->Globals, cfg, diags);
  }
}

} // namespace

void applyConfig(Module &m, const ConfigSet &cfg, DiagnosticEngine &diags) {
  m.Decls.erase(std::remove_if(m.Decls.begin(), m.Decls.end(),
                               [&](const DeclPtr &d) {
                                 return d && excluded(d.get(), cfg, diags);
                               }),
                m.Decls.end());
  for (const DeclPtr &d : m.Decls)
    if (d)
      applyToMembers(d.get(), cfg, diags);
}

} // namespace rune
