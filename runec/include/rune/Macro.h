//===- Macro.h - Declarative macros, expanded over tokens ------*- C++ -*-===//
//
// A macro is a rewrite from one run of tokens to another, chosen by pattern:
//
//     macro twice {
//         ($x: expr) => { ($x) + ($x) }
//     }
//
//     twice!(3)        // becomes  (3) + (3)
//
// Expansion happens on the token stream, before anything is parsed. That keeps
// the rest of the compiler unaware of macros entirely — by the time the parser
// runs there are none left — and it means a macro can stand for anything the
// grammar accepts, not only an expression.
//
// Definitions are collected in a pass of their own, so a macro may be used
// above where it is written, like every other declaration in Rune.
//
//===----------------------------------------------------------------------===//
#ifndef RUNE_MACRO_H
#define RUNE_MACRO_H

#include "rune/Diagnostics.h"
#include "rune/MacroEval.h"
#include "rune/Token.h"

#include <map>
#include <string>
#include <vector>

namespace rune {

/// What a `$name: kind` placeholder will match.
enum class FragmentKind {
  Expr,     ///< a balanced run of tokens up to a separator
  Ident,    ///< exactly one name
  Type,     ///< a type, matched like an expression
  Literal,  ///< one literal
  Block,    ///< a `{ ... }` group
  Tokens,   ///< one token tree: a token, or a balanced group
};

/// One `(pattern) => { body }` of a macro.
struct MacroRule {
  std::vector<Token> Pattern;
  std::vector<Token> Body;
  SourceRange Range;
};

struct MacroDef {
  std::string Name;
  std::vector<MacroRule> Rules;
  SourceRange Range;
  /// `pub macro` is in scope everywhere; a plain one only in its own file.
  bool IsPublic = false;
  /// The module it was written in, which is what "its own file" means.
  std::string Module;
  /// The `///` comment above it. Macros are taken out of the token stream
  /// before anything is parsed, so this is the only place their prose can be
  /// kept — and documentation needs it as much as any other declaration's.
  std::string Doc;
};

/// Every macro in scope for a compilation, by name.
using MacroTable = std::map<std::string, MacroDef>;

/// Takes every `macro` declaration out of `toks`.
///
/// With `record`, each is added to `into` and a repeated name is reported;
/// without, they are only removed — which is what a file needs once the table
/// has already been built from every file in the compilation.
void collectMacros(std::vector<Token> &toks, DiagnosticEngine &diags,
                   MacroTable &into, bool record = true,
                   const std::string &module = "");

/// Expands every invocation in `toks` using `table`. Returns false when
/// something was reported.
///
/// `depthLimit` stops a macro that expands to itself; the default is generous
/// enough that only a genuine loop reaches it.
/// `module` names the file being expanded: a macro that is not `pub` is only
/// in scope there.
bool expandMacros(std::vector<Token> &toks, DiagnosticEngine &diags,
                  const MacroTable &table, const std::string &module = "",
                  unsigned depthLimit = 128,
                  const MacroPackage *procs = nullptr);

/// Collects and expands in one go, for a file compiled on its own.
bool expandMacros(std::vector<Token> &toks, DiagnosticEngine &diags,
                  unsigned depthLimit = 128);

} // namespace rune

#endif
