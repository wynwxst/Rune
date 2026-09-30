//===- CxxInterop.h - Calling C++ from Rune -------------------*- C++ -*-===//
//
// An `extern "C++"` block declares what a C++ library exports, and the
// compiler does what a C++ compiler would do at the call: spell the symbol
// the Itanium ABI gives the declaration, and pass each argument the way the
// target's C++ ABI passes it. This header holds the two pieces the front end
// needs for that — a description of the target's C++ conventions, and the
// mangler. The calling-convention half lives in CodeGen, which has the data
// layout.
//
//===----------------------------------------------------------------------===//
#ifndef RUNE_CXX_INTEROP_H
#define RUNE_CXX_INTEROP_H

#include "rune/AST.h"
#include "rune/Type.h"

#include <string>
#include <vector>

namespace rune {

/// What the target's C++ compiler would assume: how wide `long` is, which
/// typedef `int64_t` resolves to, whether member functions use `thiscall`.
/// Filled in from the target triple by `cxxTargetFor`.
struct CxxTarget {
  enum class Os : uint8_t { Linux, Darwin, Windows, Other };
  Os OS = Os::Other;
  unsigned PointerBits = 64;
  /// 32 on Windows and on every 32-bit target, 64 elsewhere.
  unsigned LongBits = 64;
  /// Plain `char` is unsigned on AArch64 Linux and on Windows ARM.
  bool CharUnsigned = false;
  /// False for an MSVC environment, whose mangling this compiler does not
  /// speak. Nothing C++ can be declared for such a target.
  bool Itanium = true;
  /// i386 Windows: non-static member functions take `this` in ECX.
  bool ThisCall = false;
  /// Whether a `bool` argument is passed zero-extended (Darwin and x86 do;
  /// AAPCS64 on Linux does not promise it, so neither side may assume it).
  bool BoolZeroExt = true;
  /// Whether `i8`/`i16` arguments are sign- or zero-extended to 32 bits.
  bool SmallIntExt = true;
  /// `operator new(size_t)` and `operator delete(void*)`: the symbols the
  /// C++ runtime exports, which differ only by how `size_t` is spelled.
  std::string OperatorNew;
  std::string OperatorDelete;
};

/// Derives the conventions from a normalized target triple such as
/// `arm64-apple-darwin` or `x86_64-w64-windows-gnu`. `pointerBits` comes from
/// the target's data layout; 0 lets the triple decide.
CxxTarget cxxTargetFor(const std::string &triple, unsigned pointerBits = 0);

/// C++'s own scalar type names, spelled `c_int`, `c_long`, `c_size_t` and so
/// on. Each is an alias for the Rune integer or float it is on `target`; in an
/// `extern "C++"` signature it also fixes how the parameter is mangled, which
/// the Rune type alone cannot: `long` and `long long` are both `i64` on
/// Linux and mangle differently. Returns null when `name` is not one of them;
/// otherwise the Rune type, with the Itanium code in `code` when asked for.
Type *cxxScalarType(const std::string &name, const CxxTarget &target,
                    TypeContext &types, std::string *code = nullptr);

/// True for the name of a C++ scalar alias.
bool isCxxScalarName(const std::string &name);

/// True when `fn` was declared in an `extern "C++"` block.
inline bool isCxxExtern(const FunctionDecl *fn) {
  return fn && fn->IsExtern && fn->ExternABI == "C++";
}

/// The C++ class or struct `fn` is a member of, or null for a free function.
inline NominalDecl *cxxOwnerOf(const FunctionDecl *fn) {
  if (!fn || !fn->Parent)
    return nullptr;
  auto *nd = dyn_cast<NominalDecl>(fn->Parent);
  return nd && nd->Cxx ? nd : nullptr;
}

/// The constructor and destructor are the members Rune calls `init` and
/// `deinit`, whatever `@as` renamed them to on Rune's side.
inline bool isCxxConstructor(const FunctionDecl *fn) {
  if (!cxxOwnerOf(fn))
    return false;
  const std::string &written = fn->LinkName.empty() ? fn->Name : fn->LinkName;
  return written == "init";
}
inline bool isCxxDestructor(const FunctionDecl *fn) {
  if (!cxxOwnerOf(fn))
    return false;
  const std::string &written = fn->LinkName.empty() ? fn->Name : fn->LinkName;
  return written == "deinit";
}

/// What the mangler needs to ask Sema: whether a written type name is an
/// alias, so `type Handle = *var Foo` in a signature mangles as what it
/// stands for.
class CxxNameResolver {
public:
  virtual ~CxxNameResolver() = default;
  /// The type an alias was declared as, or null when `n` is not an alias.
  virtual const TypeRepr *aliasTarget(const NamedTypeRepr *n) = 0;
};

/// The Itanium C++ ABI's name mangling, for the declarations an
/// `extern "C++"` block can make: functions and member functions of every
/// flavour, variables, and the class, struct, enum, pointer, reference and
/// function-pointer types their signatures mention. Compression — the `S_`
/// substitutions — is implemented in full, because a linker accepts nothing
/// less than the exact spelling.
///
/// Types are mangled from what was *written* where that matters (`c_long` is
/// `l` however wide it is) and from the resolved type otherwise. A type that
/// has no C++ spelling — a `String`, a closure, a Rune enum — is reported
/// through `Problem` and the result is unusable.
class CxxMangler {
public:
  CxxMangler(const CxxTarget &target, CxxNameResolver &resolver)
      : Target(target), Resolver(resolver) {}

  /// The symbol for a function or member function declared in an
  /// `extern "C++"` block.
  std::string mangleFunction(const FunctionDecl *fn);
  /// The symbol for a variable declared in one.
  std::string mangleVariable(const GlobalVarDecl *g);

  /// Set when something in the signature could not be given a C++ spelling.
  /// `ProblemRange` points at it and `Problem` says what it was.
  bool failed() const { return !Problem.empty(); }
  const std::string &problem() const { return Problem; }
  SourceRange problemRange() const { return ProblemRange; }
  const std::string &problemHint() const { return ProblemHint; }

private:
  const CxxTarget &Target;
  CxxNameResolver &Resolver;
  std::string Out;
  /// The substitution table: canonical keys of every candidate seen so far,
  /// in order. An entry's index is its `S<n>_` number.
  std::vector<std::string> Subs;
  std::string Problem, ProblemHint;
  SourceRange ProblemRange;

  void fail(SourceRange where, const std::string &what, const std::string &hint);

  bool trySubstitute(const std::string &key);
  void addSubstitution(const std::string &key);
  static std::string seqId(size_t index);
  static std::string sourceName(const std::string &name);

  /// `<nested-name>` / `<unscoped-name>` for a declaration or a class type.
  /// `finalName` is the last component, already spelled (`5Twine`, `C1`,
  /// `nw`); `finalIsCandidate` says whether the whole path joins the table,
  /// which a type does and a function's own name does not. `key` receives
  /// the canonical key of the whole path.
  void mangleQualifiedName(const std::vector<std::string> &scope,
                           const std::string &finalName, bool isConstMember,
                           bool finalIsCandidate, const std::string &finalKey,
                           const std::vector<std::pair<const TypeRepr *, Type *>>
                               *templateArgs);

  /// One type, from its written form when available and its resolved form
  /// otherwise. Returns the canonical key (empty for a builtin).
  std::string mangleType(const TypeRepr *repr, Type *ty, SourceRange where);
  std::string mangleResolvedType(Type *ty, SourceRange where);
  std::string manglePointer(const TypeRepr *pointeeRepr, Type *pointee,
                            bool mutablePointee, bool isReference,
                            SourceRange where);
  std::string mangleNominal(NominalDecl *nd, const NamedTypeRepr *repr,
                            Type *ty, SourceRange where);
  std::string mangleConstCharPointer();
  std::string mangleFunctionPointer(const std::vector<const TypeRepr *> &params,
                                    const std::vector<Type *> &paramTypes,
                                    const TypeRepr *retRepr, Type *ret,
                                    bool variadic, SourceRange where);
  /// The code for a Rune builtin as the target's C++ spells it.
  std::string builtinCode(Type *ty, SourceRange where);
  void mangleBareParams(const FunctionDecl *fn);
};

/// The Itanium `<operator-name>` for a C++ operator written as it appears
/// after the keyword (`new`, `[]`, `+`), given how many operands the member
/// takes besides `this`. Empty when it is not an operator this compiler knows.
std::string cxxOperatorCode(const std::string &op, size_t operandCount);

} // namespace rune

#endif
