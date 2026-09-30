//===- CxxMangle.cpp - Itanium C++ name mangling ---------------------------===//
//
// The Itanium C++ ABI (chapter 5.1) spells a declaration's symbol out of its
// scope, its name and its parameter types. Two things make it more than string
// concatenation, and both are done here in full:
//
//  * every scalar has a one-letter code that depends on what the C++ side
//    *wrote*, not on its width — `long` is `l` and `long long` is `x`, though
//    both are `i64` on Linux — so a signature is mangled from the names Rune
//    wrote where those carry that distinction (`c_long`), and from the
//    resolved type where they do not;
//
//  * every prefix and every non-builtin type is a candidate for compression,
//    and its second appearance is `S<n>_` with `n` its position in the order
//    the candidates were first seen. A linker matches symbols byte for byte,
//    so the table has to be kept exactly as the ABI says.
//
//===----------------------------------------------------------------------===//
#include "rune/CxxInterop.h"

#include <algorithm>

namespace rune {

/// The Itanium code for a `c_*` name on `target`, or empty. Kept apart from
/// `cxxScalarType` so the mangler need not carry a `TypeContext`.
std::string cxxScalarCodeOnly(const std::string &name, const CxxTarget &target);

//===----------------------------------------------------------------------===//
// The target
//===----------------------------------------------------------------------===//

CxxTarget cxxTargetFor(const std::string &triple, unsigned pointerBits) {
  CxxTarget t;
  // The architecture is the first component: `x86_64-...`, `i686-...`.
  const std::string arch = triple.substr(0, triple.find('-'));
  auto has = [&](const char *needle) {
    return triple.find(needle) != std::string::npos;
  };
  const bool arm64 = arch == "aarch64" || arch == "arm64" ||
                     arch.rfind("aarch64", 0) == 0 || arch.rfind("arm64", 0) == 0;
  const bool x86_64 = arch == "x86_64" || arch == "amd64";
  if (pointerBits == 0)
    pointerBits = (arm64 || x86_64 || arch.find("64") != std::string::npos) ? 64 : 32;
  const bool is32 = pointerBits == 32;
  t.PointerBits = pointerBits;
  if (has("darwin") || has("macos") || has("ios"))
    t.OS = CxxTarget::Os::Darwin;
  else if (has("windows") || has("mingw") || has("win32"))
    t.OS = CxxTarget::Os::Windows;
  else if (has("linux") || has("freebsd") || has("netbsd") || has("openbsd"))
    t.OS = CxxTarget::Os::Linux;
  else if (arch.rfind("wasm", 0) == 0)
    t.OS = CxxTarget::Os::Wasm;
  t.LongBits = (t.OS == CxxTarget::Os::Windows || is32) ? 32 : 64;
  t.CharUnsigned = arm64 && t.OS != CxxTarget::Os::Darwin;
  t.Itanium = !has("msvc");
  t.ThisCall = t.OS == CxxTarget::Os::Windows && is32;
  t.CtorsReturnThis = t.OS == CxxTarget::Os::Wasm;
  // AAPCS64 leaves the upper bits of a `bool` or a small integer argument
  // unspecified; every other target we build for extends them, and Apple's
  // arm64 variant requires it.
  const bool aapcs = arm64 && t.OS != CxxTarget::Os::Darwin;
  t.BoolZeroExt = !aapcs;
  t.SmallIntExt = !aapcs;
  // `size_t` is `unsigned long` on LP64, `unsigned long long` on Win64 and
  // `unsigned int` on a 32-bit target — except wasm32, where it is a 32-bit
  // `unsigned long`.
  const char sizeCode =
      is32 ? (t.OS == CxxTarget::Os::Wasm ? 'm' : 'j')
           : (t.OS == CxxTarget::Os::Windows ? 'y' : 'm');
  t.OperatorNew = std::string("_Znw") + sizeCode;
  t.OperatorDelete = "_ZdlPv";
  return t;
}

namespace {

/// One C++ scalar alias: its name, its Itanium code on each kind of target,
/// and what it is in Rune's terms.
struct ScalarSpec {
  const char *Name;
  /// Codes for LP64 Linux, Darwin, Win64 and any 32-bit target; a single
  /// code means the same everywhere.
  const char *Linux, *Darwin, *Win64, *Bits32;
};

// `int64_t`, `uint64_t`, `intptr_t`, `size_t` and friends are typedefs, and
// what they are typedefs *of* is what gets mangled — which is why the same
// declaration has a different symbol on each platform, and why a Rune `i64`
// in a signature mangles as the target's `int64_t`.
const ScalarSpec kScalars[] = {
    // `void` only ever appears behind a pointer: `*var c_void` is `void *`,
    // and is a `*var u8` on Rune's side.
    {"c_void", "v", "v", "v", "v"},
    {"c_char", "c", "c", "c", "c"},
    {"c_schar", "a", "a", "a", "a"},
    {"c_uchar", "h", "h", "h", "h"},
    {"c_short", "s", "s", "s", "s"},
    {"c_ushort", "t", "t", "t", "t"},
    {"c_int", "i", "i", "i", "i"},
    {"c_uint", "j", "j", "j", "j"},
    {"c_long", "l", "l", "l", "l"},
    {"c_ulong", "m", "m", "m", "m"},
    {"c_longlong", "x", "x", "x", "x"},
    {"c_ulonglong", "y", "y", "y", "y"},
    {"c_float", "f", "f", "f", "f"},
    {"c_double", "d", "d", "d", "d"},
    {"c_bool", "b", "b", "b", "b"},
    {"c_wchar_t", "w", "w", "w", "w"},
    {"c_char16_t", "Ds", "Ds", "Ds", "Ds"},
    {"c_char32_t", "Di", "Di", "Di", "Di"},
    {"c_int8_t", "a", "a", "a", "a"},
    {"c_uint8_t", "h", "h", "h", "h"},
    {"c_int16_t", "s", "s", "s", "s"},
    {"c_uint16_t", "t", "t", "t", "t"},
    {"c_int32_t", "i", "i", "i", "i"},
    {"c_uint32_t", "j", "j", "j", "j"},
    {"c_int64_t", "l", "x", "x", "x"},
    {"c_uint64_t", "m", "y", "y", "y"},
    {"c_size_t", "m", "m", "y", "j"},
    {"c_ssize_t", "l", "l", "x", "i"},
    {"c_ptrdiff_t", "l", "l", "x", "i"},
    {"c_intptr_t", "l", "l", "x", "i"},
    {"c_uintptr_t", "m", "m", "y", "j"},
};

const ScalarSpec *findScalar(const std::string &name) {
  for (const ScalarSpec &s : kScalars)
    if (name == s.Name)
      return &s;
  return nullptr;
}

const char *codeFor(const ScalarSpec &s, const CxxTarget &t) {
  if (t.PointerBits == 32) {
    // wasm32 spells the pointer-sized types `long` where other 32-bit targets
    // spell them `int`. `long` is 32 bits there too, so only the mangling
    // differs — which is the part that has to match.
    const bool pointerSized = (s.Bits32[0] == 'i' || s.Bits32[0] == 'j') &&
                              (s.Linux[0] == 'l' || s.Linux[0] == 'm');
    if (t.OS == CxxTarget::Os::Wasm && pointerSized)
      return s.Linux;
    return s.Bits32;
  }
  switch (t.OS) {
  case CxxTarget::Os::Darwin: return s.Darwin;
  case CxxTarget::Os::Windows: return s.Win64;
  default: return s.Linux;
  }
}

/// The Rune type behind an Itanium builtin code on `t`.
Type *runeTypeForCode(const std::string &code, const CxxTarget &t,
                      TypeContext &types) {
  if (code == "v") return types.u8();
  if (code == "c") return types.intType(8, !t.CharUnsigned);
  if (code == "a") return types.i8();
  if (code == "h") return types.u8();
  if (code == "s") return types.i16();
  if (code == "t") return types.intType(16, false);
  if (code == "i") return types.i32();
  if (code == "j") return types.u32();
  if (code == "l") return types.intType(t.LongBits, true);
  if (code == "m") return types.intType(t.LongBits, false);
  if (code == "x") return types.i64();
  if (code == "y") return types.u64();
  if (code == "f") return types.f32();
  if (code == "d") return types.f64();
  if (code == "b") return types.boolType();
  if (code == "w")
    return t.OS == CxxTarget::Os::Windows ? types.intType(16, false)
                                          : types.i32();
  if (code == "Ds") return types.intType(16, false);
  if (code == "Di") return types.u32();
  return nullptr;
}

} // namespace

bool isCxxScalarName(const std::string &name) { return findScalar(name) != nullptr; }

Type *cxxScalarType(const std::string &name, const CxxTarget &target,
                    TypeContext &types, std::string *code) {
  const ScalarSpec *s = findScalar(name);
  if (!s)
    return nullptr;
  std::string c = codeFor(*s, target);
  if (code)
    *code = c;
  return runeTypeForCode(c, target, types);
}

std::string cxxOperatorCode(const std::string &op, size_t operandCount) {
  const bool unary = operandCount == 0;
  static const struct { const char *Op, *Code; } kBinary[] = {
      {"new", "nw"},   {"new[]", "na"}, {"delete", "dl"}, {"delete[]", "da"},
      {"~", "co"},     {"+", "pl"},     {"-", "mi"},      {"*", "ml"},
      {"/", "dv"},     {"%", "rm"},     {"&", "an"},      {"|", "or"},
      {"^", "eo"},     {"=", "aS"},     {"+=", "pL"},     {"-=", "mI"},
      {"*=", "mL"},    {"/=", "dV"},    {"%=", "rM"},     {"&=", "aN"},
      {"|=", "oR"},    {"^=", "eO"},    {"<<", "ls"},     {">>", "rs"},
      {"<<=", "lS"},   {">>=", "rS"},   {"==", "eq"},     {"!=", "ne"},
      {"<", "lt"},     {">", "gt"},     {"<=", "le"},     {">=", "ge"},
      {"<=>", "ss"},   {"!", "nt"},     {"&&", "aa"},     {"||", "oo"},
      {"++", "pp"},    {"--", "mm"},    {",", "cm"},      {"->*", "pm"},
      {"->", "pt"},    {"()", "cl"},    {"[]", "ix"},     {"?", "qu"},
  };
  // `+`, `-`, `&` and `*` mean something else with one operand.
  if (unary) {
    if (op == "+") return "ps";
    if (op == "-") return "ng";
    if (op == "&") return "ad";
    if (op == "*") return "de";
  }
  for (const auto &e : kBinary)
    if (op == e.Op)
      return e.Code;
  return "";
}

//===----------------------------------------------------------------------===//
// The mangler
//===----------------------------------------------------------------------===//

void CxxMangler::fail(SourceRange where, const std::string &what,
                      const std::string &hint) {
  if (!Problem.empty())
    return;
  Problem = what;
  ProblemHint = hint;
  ProblemRange = where;
}

std::string CxxMangler::seqId(size_t index) {
  // `S_` is the first; the rest count in base 36 with upper-case digits,
  // starting from zero: `S0_`, `S1_`, ... `S9_`, `SA_`, ... `SZ_`, `S10_`.
  if (index == 0)
    return "S_";
  size_t n = index - 1;
  std::string digits;
  do {
    unsigned d = static_cast<unsigned>(n % 36);
    digits.insert(digits.begin(), static_cast<char>(d < 10 ? '0' + d : 'A' + d - 10));
    n /= 36;
  } while (n);
  return "S" + digits + "_";
}

std::string CxxMangler::sourceName(const std::string &name) {
  return std::to_string(name.size()) + name;
}

bool CxxMangler::trySubstitute(const std::string &key) {
  if (key.empty())
    return false;
  auto it = std::find(Subs.begin(), Subs.end(), key);
  if (it == Subs.end())
    return false;
  Out += seqId(static_cast<size_t>(it - Subs.begin()));
  return true;
}

void CxxMangler::addSubstitution(const std::string &key) {
  if (key.empty())
    return;
  if (std::find(Subs.begin(), Subs.end(), key) == Subs.end())
    Subs.push_back(key);
}

/// Writes a qualified name. The prefix components (namespaces and enclosing
/// classes) join the substitution table one by one as they are spelled; the
/// longest prefix already in it is written as `S<n>_` instead. `std` at the
/// front is the abbreviation `St` and is itself never a candidate.
void CxxMangler::mangleQualifiedName(
    const std::vector<std::string> &scope, const std::string &finalName,
    bool isConstMember, bool finalIsCandidate, const std::string &finalKey,
    const std::vector<std::pair<const TypeRepr *, Type *>> *templateArgs) {
  // The whole thing may already be known — a class type seen before, or one
  // that was a prefix of the function's own name.
  if (finalIsCandidate && !templateArgs && trySubstitute(finalKey))
    return;

  const bool nested = !scope.empty() || isConstMember;
  if (!nested) {
    // `<unscoped-name>`: a global-namespace function, variable or type.
    if (templateArgs) {
      // `<unscoped-template-name> <template-args>`
      const std::string tplKey = "T:" + finalName;
      if (!trySubstitute(tplKey)) {
        Out += finalName;
        addSubstitution(tplKey);
      }
    } else {
      Out += finalName;
    }
  } else {
    Out += "N";
    if (isConstMember)
      Out += "K";
    // Prefix keys: "N:a", "N:a::b", ...
    std::vector<std::string> keys;
    std::string joined;
    for (const std::string &c : scope) {
      joined += joined.empty() ? c : "::" + c;
      keys.push_back("N:" + joined);
    }
    // The longest prefix already substitutable.
    size_t start = 0;
    for (size_t i = keys.size(); i > 0; --i) {
      auto it = std::find(Subs.begin(), Subs.end(), keys[i - 1]);
      if (it != Subs.end()) {
        Out += seqId(static_cast<size_t>(it - Subs.begin()));
        start = i;
        break;
      }
    }
    for (size_t i = start; i < scope.size(); ++i) {
      if (i == 0 && scope[0] == "std") {
        Out += "St";
        continue; // `std` is spelled `St` and never substituted
      }
      Out += sourceName(scope[i]);
      addSubstitution(keys[i]);
    }
    if (templateArgs) {
      // The template name itself is a prefix candidate before its arguments.
      std::string tplKey = "T:" + (joined.empty() ? "" : joined + "::") + finalName;
      if (!trySubstitute(tplKey)) {
        Out += finalName;
        addSubstitution(tplKey);
      }
    } else {
      Out += finalName;
    }
  }
  if (templateArgs) {
    Out += "I";
    for (const auto &a : *templateArgs)
      mangleType(a.first, a.second, a.first ? a.first->Range : SourceRange());
    Out += "E";
  }
  if (nested)
    Out += "E";
  if (finalIsCandidate)
    addSubstitution(finalKey);
}

std::string CxxMangler::builtinCode(Type *ty, SourceRange where) {
  switch (ty->kind()) {
  case TypeKind::Void: return "v";
  case TypeKind::Bool: return "b";
  case TypeKind::Char: return "Di";
  case TypeKind::Float: return ty->floatWidth() == 32 ? "f" : "d";
  case TypeKind::Int: {
    // A fixed-width Rune integer stands for the target's `intN_t` typedef,
    // `usize` for `size_t` and `isize` for `intptr_t`: what such a typedef
    // resolves to is what C++ mangles, and it differs per platform.
    const bool s = ty->isSigned();
    switch (ty->intWidth()) {
    case 8: return s ? "a" : "h";
    case 16: return s ? "s" : "t";
    case 32:
      return s ? "i" : "j";
    case 64: {
      // `int64_t` is `long` on LP64 Linux and `long long` everywhere else;
      // on a 64-bit target `usize` is `size_t`, which is `unsigned long`
      // on both Linux and Darwin and `unsigned long long` on Windows.
      // Nothing distinguishes `i64` from `isize` once resolved, so the
      // `intN_t` reading wins and `c_size_t` is there for the other.
      if (Target.OS == CxxTarget::Os::Linux)
        return s ? "l" : "m";
      return s ? "x" : "y";
    }
    default: break;
    }
    fail(where, "an integer of " + std::to_string(ty->intWidth()) +
                    " bits has no C++ spelling",
         "use i8..i64, u8..u64, or one of the `c_` names such as `c_long`");
    return "i";
  }
  default:
    break;
  }
  fail(where, "'" + ty->toString() + "' is not a C++ type", "");
  return "v";
}

std::string CxxMangler::manglePointer(const TypeRepr *pointeeRepr, Type *pointee,
                                      bool mutablePointee, bool isReference,
                                      SourceRange where) {
  // `const T*` is `PKT`: the qualified type is a candidate of its own, and
  // then the pointer to it is another. Both go in only after the pointee,
  // which is why the pointee is spelled into a side buffer first — the
  // candidate order is inside-out while the spelling is outside-in.
  std::string saved = std::move(Out);
  Out.clear();
  std::string inner = mangleType(pointeeRepr, pointee, where);
  std::string innerText = std::move(Out);
  Out = std::move(saved);

  const std::string qualifiedKey = mutablePointee ? inner : "K:" + inner;
  const std::string key = (isReference ? "R:" : "P:") + qualifiedKey;
  if (trySubstitute(key))
    return key;
  Out += isReference ? "R" : "P";
  if (!mutablePointee) {
    if (!trySubstitute(qualifiedKey)) {
      Out += "K" + innerText;
      addSubstitution(qualifiedKey);
    }
  } else {
    Out += innerText;
  }
  addSubstitution(key);
  return key;
}

std::string CxxMangler::mangleFunctionPointer(
    const std::vector<const TypeRepr *> &params,
    const std::vector<Type *> &paramTypes, const TypeRepr *retRepr, Type *ret,
    bool variadic, SourceRange where) {
  // `PF<ret><params>E`: the function type is a candidate, then the pointer.
  std::string saved = std::move(Out);
  Out.clear();
  std::string key = "F:";
  key += mangleType(retRepr, ret, where);
  key += "(";
  if (paramTypes.empty()) {
    Out += "v";
  } else {
    for (size_t i = 0; i < paramTypes.size(); ++i) {
      key += mangleType(i < params.size() ? params[i] : nullptr, paramTypes[i],
                        where);
      key += ",";
    }
  }
  if (variadic) {
    Out += "z";
    key += "...";
  }
  key += ")";
  std::string fnText = std::move(Out);
  Out = std::move(saved);

  std::string ptrKey = "P:" + key;
  if (trySubstitute(ptrKey))
    return ptrKey;
  Out += "P";
  if (!trySubstitute(key)) {
    Out += "F" + fnText + "E";
    addSubstitution(key);
  }
  addSubstitution(ptrKey);
  return ptrKey;
}

std::string CxxMangler::mangleNominal(NominalDecl *nd, const NamedTypeRepr *repr,
                                      Type *ty, SourceRange where) {
  if (!nd->Cxx) {
    fail(where, "'" + static_cast<Decl *>(nd)->Name +
                    "' is a Rune type, not a C++ one",
         "declare the C++ type inside the `extern \"C++\"` block, or pass a "
         "pointer to something C++ knows");
    return "";
  }
  // An instantiation of a generic struct is a template specialisation: the
  // template's name with the arguments in `I...E`.
  NominalDecl *tmpl = nd->GenericTemplate ? nd->GenericTemplate : nd;
  std::vector<std::pair<const TypeRepr *, Type *>> args;
  const std::vector<Type *> &targs = nd->TypeArguments;
  for (size_t i = 0; i < targs.size(); ++i) {
    const TypeRepr *r = repr && i < repr->GenericArgs.size()
                            ? repr->GenericArgs[i].get()
                            : nullptr;
    args.push_back({r, targs[i]});
  }
  const std::string &name = static_cast<Decl *>(tmpl)->Name;
  std::string joined;
  for (const std::string &c : tmpl->Cxx->Scope)
    joined += c + "::";
  std::string key = "N:" + joined + name;
  if (!args.empty()) {
    // The arguments' identity is their resolved type: the same type
    // written two ways is one specialisation.
    key += "<";
    for (const auto &a : args) {
      char buf[32];
      snprintf(buf, sizeof(buf), "%p", static_cast<const void *>(a.second));
      key += buf;
      key += ",";
    }
    key += ">";
    if (trySubstitute(key))
      return key;
  }
  mangleQualifiedName(tmpl->Cxx->Scope, sourceName(name),
                      /*isConstMember=*/false, /*finalIsCandidate=*/true, key,
                      args.empty() ? nullptr : &args);
  return key;
}

std::string CxxMangler::mangleResolvedType(Type *ty, SourceRange where) {
  if (!ty || ty->isError()) {
    fail(where, "this type could not be resolved", "");
    return "";
  }
  ty = ty->canonical();
  switch (ty->kind()) {
  case TypeKind::Void:
  case TypeKind::Bool:
  case TypeKind::Char:
  case TypeKind::Float:
  case TypeKind::Int: {
    // A builtin is never a candidate, but what is built on it is, so the key
    // still has to say which builtin this was.
    std::string code = builtinCode(ty, where);
    Out += code;
    return "B:" + code;
  }
  case TypeKind::CString:
    return mangleConstCharPointer();
  case TypeKind::Pointer:
    if (ty->isWeakPointer()) {
      fail(where, "a `weak` reference is not a C++ type", "");
      return "";
    }
    return manglePointer(nullptr, ty->pointee(), ty->isMutablePointer(),
                         /*isReference=*/!ty->isRawPointer(), where);
  case TypeKind::CFunction:
    return mangleFunctionPointer({}, ty->params(), nullptr, ty->result(),
                                 ty->isVariadicFunction(), where);
  case TypeKind::Struct:
  case TypeKind::Enum:
    return mangleNominal(ty->nominal(), nullptr, ty, where);
  case TypeKind::Class:
    fail(where, "a Rune class never crosses into C++",
         "C++ sees no reference count; hand it a pointer to a C++ object");
    return "";
  case TypeKind::String:
    fail(where, "`String` is not a C++ type",
         "pass `.$cstr()` as a `CString` (`const char *`)");
    return "";
  case TypeKind::Function:
    fail(where, "a closure is not a C++ type",
         "write `@cfunction(...)` for a bare function pointer");
    return "";
  default:
    fail(where, "'" + ty->toString() + "' is not a C++ type", "");
    return "";
  }
}

std::string CxxMangler::mangleType(const TypeRepr *repr, Type *ty,
                                   SourceRange where) {
  if (repr && repr->Range.isValid())
    where = repr->Range;
  if (!ty && repr)
    ty = repr->Resolved;
  if (repr) {
    switch (repr->Kind) {
    case NodeKind::NamedType: {
      const auto *n = static_cast<const NamedTypeRepr *>(repr);
      if (n->Path.size() == 1) {
        // A C++ scalar name fixes the letter whatever the width.
        std::string code = cxxScalarCodeOnly(n->Path[0], Target);
        if (!code.empty()) {
          Out += code;
          return "B:" + code;
        }
        if (n->Path[0] == "CString")
          return mangleConstCharPointer();
        if (const TypeRepr *target = Resolver.aliasTarget(n))
          return mangleType(target, ty, where);
      }
      if (ty && ty->canonical()->isNominal())
        return mangleNominal(ty->canonical()->nominal(), n, ty->canonical(),
                             where);
      return mangleResolvedType(ty, where);
    }
    case NodeKind::PointerType: {
      const auto *p = static_cast<const PointerTypeRepr *>(repr);
      if (p->IsWeak) {
        fail(where, "a `weak` reference is not a C++ type", "");
        return "";
      }
      Type *pointee = ty && ty->is(TypeKind::Pointer) ? ty->pointee() : nullptr;
      return manglePointer(p->Pointee.get(), pointee, p->IsMutable,
                           /*isReference=*/!p->IsRaw, where);
    }
    case NodeKind::FunctionTypeRepr: {
      const auto *f = static_cast<const FunctionTypeReprNode *>(repr);
      if (!f->IsCFunction) {
        fail(where, "a closure is not a C++ type",
             "write `@cfunction(...)` for a bare function pointer");
        return "";
      }
      std::vector<const TypeRepr *> params;
      for (const auto &p : f->Params)
        params.push_back(p.get());
      Type *fty = ty && ty->is(TypeKind::CFunction) ? ty : nullptr;
      std::vector<Type *> paramTypes;
      if (fty)
        paramTypes = fty->params();
      else
        for (const auto &p : f->Params)
          paramTypes.push_back(p->Resolved);
      return mangleFunctionPointer(params, paramTypes, f->ReturnType.get(),
                                   fty ? fty->result()
                                       : (f->ReturnType ? f->ReturnType->Resolved
                                                        : nullptr),
                                   fty && fty->isVariadicFunction(), where);
    }
    default:
      break;
    }
  }
  return mangleResolvedType(ty, where);
}

/// `const char *` — `PKc`, spelled from its parts so `Kc` and `PKc` join the
/// table exactly as they would for a written `*c_char`.
std::string CxxMangler::mangleConstCharPointer() {
  const std::string qualifiedKey = "K:c";
  const std::string key = "P:K:c";
  if (trySubstitute(key))
    return key;
  Out += "P";
  if (!trySubstitute(qualifiedKey)) {
    Out += "Kc";
    addSubstitution(qualifiedKey);
  }
  addSubstitution(key);
  return key;
}

void CxxMangler::mangleBareParams(const FunctionDecl *fn) {
  size_t count = 0;
  for (const Param &p : fn->Params) {
    if (p.IsSelf)
      continue;
    ++count;
    mangleType(p.TypeAnnotation.get(), p.Ty,
               p.TypeAnnotation ? p.TypeAnnotation->Range : p.Range);
  }
  if (fn->IsVariadic)
    Out += "z";
  else if (count == 0)
    Out += "v";
}

std::string CxxMangler::mangleFunction(const FunctionDecl *fn) {
  Out = "_Z";
  Subs.clear();
  Problem.clear();

  // What the C++ side calls it: `@as` renamed it for Rune only.
  const std::string &written = fn->LinkName.empty() ? fn->Name : fn->LinkName;
  NominalDecl *owner = cxxOwnerOf(fn);

  std::vector<std::string> scope;
  bool isConst = false;
  if (owner) {
    NominalDecl *tmpl = owner->GenericTemplate ? owner->GenericTemplate : owner;
    scope = tmpl->Cxx->Scope;
    scope.push_back(static_cast<Decl *>(tmpl)->Name);
    for (const Param &p : fn->Params)
      if (p.IsSelf)
        isConst = p.SelfByRef && !p.SelfMutable;
  } else {
    scope = fn->CxxScope;
  }

  std::string finalName;
  if (owner && isCxxConstructor(fn))
    finalName = "C1";
  else if (owner && isCxxDestructor(fn))
    finalName = "D1";
  else if (!fn->CxxOperator.empty()) {
    size_t operands = 0;
    for (const Param &p : fn->Params)
      if (!p.IsSelf)
        ++operands;
    finalName = cxxOperatorCode(fn->CxxOperator, operands);
    if (finalName.empty()) {
      fail(fn->NameRange, "'operator " + fn->CxxOperator +
                              "' is not an operator this compiler can name",
           "the arithmetic, comparison, subscript, call and allocation "
           "operators are");
      finalName = "nw";
    }
  } else {
    finalName = sourceName(written);
  }

  // A constructor or destructor is never `const`, and a static member has no
  // `this` to be const on.
  if (owner && !owner->TypeArguments.empty()) {
    // A member of a template specialisation: the class, arguments and all,
    // is the prefix. It is spelled as the type would be and unwrapped — the
    // candidates that spelling adds are exactly the ones the ABI wants.
    Out += "N";
    if (isConst)
      Out += "K";
    std::string saved = std::move(Out);
    Out.clear();
    mangleNominal(owner, nullptr, owner->DeclaredType, fn->NameRange);
    std::string cls = std::move(Out);
    Out = std::move(saved);
    if (cls.size() > 2 && cls.front() == 'N' && cls.back() == 'E')
      cls = cls.substr(1, cls.size() - 2);
    Out += cls + finalName + "E";
  } else {
    mangleQualifiedName(scope, finalName, isConst, /*finalIsCandidate=*/false,
                        "", nullptr);
  }
  mangleBareParams(fn);
  return Out;
}

std::string CxxMangler::mangleVariable(const GlobalVarDecl *g) {
  Out = "_Z";
  Subs.clear();
  Problem.clear();
  const std::string &written = g->LinkName.empty() ? g->Name : g->LinkName;
  if (g->CxxScope.empty())
    return written; // a global-namespace variable is not mangled at all
  mangleQualifiedName(g->CxxScope, sourceName(written), false, false, "",
                      nullptr);
  return Out;
}

std::string cxxScalarCodeOnly(const std::string &name, const CxxTarget &target) {
  const ScalarSpec *s = findScalar(name);
  return s ? codeFor(*s, target) : "";
}

} // namespace rune
