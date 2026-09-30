//===- Config.h - Conditional compilation -----------------------*- C++ -*-===//
//
// `@Config(...)` on a declaration decides whether that declaration exists at
// all. It is answered before anything is checked, so a declaration the
// condition rules out is not merely unused — it is gone, and may name types,
// functions and foreign symbols that exist on no other target.
//
//     @Config(os == "windows")
//     fn consoleWidth() -> i64 { ... }        // uses Win32; never seen elsewhere
//
//     @Config(os == "linux" || os == "macos")
//     fn consoleWidth() -> i64 { ... }        // uses ioctl
//
// A package names keys of its own in `[config]`, and a package that depends
// on it may choose them; both arrive as `--cfg key=value`.
//
//     @Config(backend == vulkan)
//     fn present() { ... }
//
// The expression language is deliberately tiny, because it has to be answered
// without a type checker: a key compared against a string, a bare name that is
// either set or not, and `&&`, `||`, `!` and parentheses over those.
//
//===----------------------------------------------------------------------===//
#ifndef RUNE_CONFIG_H
#define RUNE_CONFIG_H

#include "rune/AST.h"
#include "rune/Diagnostics.h"
#include "rune/Driver.h"

#include <map>
#include <set>
#include <string>

namespace rune {

/// What `@Config` can ask about. Built once from the compiler options and the
/// target triple, then consulted for every condition in the compilation.
struct ConfigSet {
  /// Keys with a value: `os`, `arch`, `family`, `pointer_width`, `endian`,
  /// `target`, `safety`, `opt_level`.
  std::map<std::string, std::string> Values;
  /// Names that are simply set or not: `debug`, and everything `--cfg` named.
  std::set<std::string> Flags;
  /// Which of `Values` the target and the flags decided, rather than the
  /// manifest: those cannot be overridden by a `[config]` entry.
  std::set<std::string> Builtin;

  /// The facts about `opts` and the target it builds for.
  static ConfigSet forOptions(const CompilerOptions &opts);

  /// The value of `key`, or empty when it is not a key this build knows.
  std::string value(const std::string &key) const {
    auto it = Values.find(key);
    return it == Values.end() ? std::string() : it->second;
  }
  bool isKey(const std::string &key) const { return Values.count(key) != 0; }
  bool isSet(const std::string &name) const { return Flags.count(name) != 0; }

  /// Every name this build would answer to, for "did you mean" notes.
  std::vector<std::string> known() const;
};

/// Answers one `@Config(...)`. Reports, and returns true, for an expression it
/// cannot make sense of: a condition nobody can evaluate should not silently
/// delete code.
bool evaluateConfig(const Attribute &attr, const ConfigSet &cfg,
                    DiagnosticEngine &diags);

/// Removes from `m` every declaration whose `@Config` is false, and every
/// member of a surviving declaration whose own `@Config` is false.
///
/// Runs after parsing and before anything is collected, so what it removes was
/// never named, never resolved and never checked.
void applyConfig(Module &m, const ConfigSet &cfg, DiagnosticEngine &diags);

} // namespace rune

#endif
