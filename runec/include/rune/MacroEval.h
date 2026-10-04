//===- MacroEval.h - Procedural macros, run while compiling ----*- C++ -*-===//
//
// A procedural macro is an ordinary Rune function marked `#macro`, in a file
// that says it is a macro package:
//
//     #type(Macros)
//     import std::Macro
//
//     #macro
//     pub fn twice(input: Macro::Tokens) -> Macro::Tokens { ... }
//
// The file is a **package of its own**: the compiler builds it first, for the
// machine doing the compiling, and then runs it to expand each invocation.
// Two things follow, and they are the point of doing it this way rather than
// interpreting the body:
//
//  * a macro is ordinary compiled Rune, so generics, marks and the whole
//    standard library work in one;
//  * nothing the macro package imports or declares reaches the program, which
//    sees only the tokens that come back.
//
// The conversation is two files and a process. The compiler writes the macro's
// name and the invocation's tokens to one, runs the package with that file and
// an empty one to answer in, and reads what the macro wrote. A macro that
// crashes therefore cannot take the compiler with it, and running a macro
// package by hand is a perfectly good way to see what it produces.
//
// A `#macro fn` may also be written in an ordinary file. It is lifted out —
// with the file's `std` imports, at its own line and column — and built into
// the same package; the program never sees it. A library keeps both kinds as
// source in its `.rul`, so an importer builds them into its own package:
// that is how a library exports its macros.
//
//===----------------------------------------------------------------------===//
#ifndef RUNE_MACROEVAL_H
#define RUNE_MACROEVAL_H

#include "rune/Diagnostics.h"
#include "rune/Token.h"

#include <map>
#include <string>
#include <vector>

namespace rune {

/// One `#macro fn` in a macro package.
struct ProcMacro {
  std::string Name;
  SourceRange Range;    ///< where it was declared, for diagnostics
  std::string Module;   ///< the macro package module it lives in
  std::string Doc;
};

/// Every procedural macro in a compilation, by name.
using ProcMacroTable = std::map<std::string, ProcMacro>;

/// A built macro package: the program, and what it answers to.
struct MacroPackage {
  std::string Program;   ///< the executable the package was built into
  ProcMacroTable Macros;
  bool usable() const { return !Program.empty() && !Macros.empty(); }
};

/// True when this file opens with `#type(Macros)`, which is what makes it a
/// macro package rather than part of the program.
bool declaresMacroPackage(const std::vector<Token> &toks);

/// Records every `#macro fn` the file declares. Nothing is removed: a macro
/// package is compiled, so its functions have to stay where they are.
void collectProcMacros(const std::vector<Token> &toks, DiagnosticEngine &diags,
                       ProcMacroTable &into, const std::string &module);

/// True when the file declares a `#macro fn` anywhere in it.
bool hasProcMacros(const std::vector<Token> &toks);

/// Takes every `#macro fn` out of `toks` without a word: it belongs to the
/// macro package, which is built from it separately, and not to the program.
void stripProcMacros(std::vector<Token> &toks);

/// A `#macro fn` may be written in any file, not only in a macro package.
/// This lifts the ones in `file` out: they are taken out of `toks`, and what
/// is returned is the source of a macro-package module that holds them — the
/// file's own text with everything but those functions and its `std`
/// imports blanked out, so a diagnostic about one points at the line and
/// column it was written on. Empty when the file declares none.
///
/// `stamp` is mixed with what the lifted functions say, token by token, so
/// the cached package is rebuilt when a macro changes and not when the code
/// around it does.
std::string liftProcMacros(std::vector<Token> &toks, const std::string &buffer,
                           uint32_t startOffset, uint64_t &stamp);

/// Mixes what `toks` say into `stamp` — kinds and spellings, never
/// positions or comments, so moving a macro or rewording a comment does not
/// call for a new package.
void stampTokens(const std::vector<Token> &toks, uint64_t &stamp);

/// FNV-1a: mixes `s` into `stamp`.
void stampText(const std::string &s, uint64_t &stamp);

/// Runs `m` over `input` and appends what it hands back to `out`.
///
/// `at` is the invocation, which is what any diagnostic the macro raises
/// points at. Returns false when something was reported.
bool runProcMacro(const ProcMacro &m, const std::string &program,
                  const std::vector<Token> &input, SourceRange at,
                  DiagnosticEngine &diags, std::vector<Token> &out);

/// The source a dispatcher for these macros is written as: a `main` that
/// hands each name to the function it belongs to. The compiler writes this
/// beside the macro package's own files and builds the two together.
std::string macroDispatcherSource(const ProcMacroTable &macros,
                                  const std::vector<std::string> &modules);

} // namespace rune

#endif
