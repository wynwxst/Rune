//===- MacroEval.h - Procedural macros, run while compiling ----*- C++ -*-===//
//
// A procedural macro is an ordinary Rune function marked `@macro`, in a file
// that says it is a macro package:
//
//     @type(Macros)
//     import std::Macro
//
//     @macro
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
//===----------------------------------------------------------------------===//
#ifndef RUNE_MACROEVAL_H
#define RUNE_MACROEVAL_H

#include "rune/Diagnostics.h"
#include "rune/Token.h"

#include <map>
#include <string>
#include <vector>

namespace rune {

/// One `@macro fn` in a macro package.
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

/// True when this file opens with `@type(Macros)`, which is what makes it a
/// macro package rather than part of the program.
bool declaresMacroPackage(const std::vector<Token> &toks);

/// Records every `@macro fn` the file declares. Nothing is removed: a macro
/// package is compiled, so its functions have to stay where they are.
void collectProcMacros(const std::vector<Token> &toks, DiagnosticEngine &diags,
                       ProcMacroTable &into, const std::string &module);

/// Reports a `@macro fn` written outside a macro package and takes it out of
/// the stream, so the grammar does not then trip over it.
void rejectProcMacros(std::vector<Token> &toks, DiagnosticEngine &diags);

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
