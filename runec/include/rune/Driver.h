//===- Driver.h - Compiler options and pipeline ----------------*- C++ -*-===//
#ifndef RUNE_DRIVER_H
#define RUNE_DRIVER_H

#include <string>
#include <vector>

namespace rune {

/// How much runtime checking the code generator inserts.
enum class SafetyLevel {
  None,    ///< No checks at all; every operation is trusted.
  Minimal, ///< Nil checks only.
  Full,    ///< Bounds, nil, overflow, division and leak reporting. (default)
};

/// Whether `+`, `-`, `*` and unary `-` on integers trap when the result does
/// not fit. `Default` follows the build: a debug build (`-O0`) traps, an
/// optimised one wraps — the same split as Rust's, chosen so the mistake is
/// caught while the program is being written and costs nothing once it is
/// shipped. `--safety none` never traps.
enum class OverflowChecks {
  Default,
  On,
  Off,
};

/// How the program keeps its heap alive.
///
/// `Arc` is automatic reference counting: every class, `String`, closure and
/// mark object carries a count, copies retain and scope exits release, and
/// the ownership pass in `Ownership.cpp` is advisory. `Zombie` is single
/// ownership checked at compile time: no count is ever touched, an owned value
/// moves, and the borrow checker in `Zombie*.cpp` proves that every borrow is
/// gone before what it points at is. Under `Zombie` the checker's findings are
/// what makes the generated code sound, so they are errors at every
/// `--safety` level.
enum class MemoryMode {
  Arc,
  Zombie,
};

enum class OutputKind {
  Executable, ///< Link into a runnable program.
  Object,     ///< Emit a .o only.
  Library,    ///< Emit a .rul (object + interface metadata).
  LLVMIR,     ///< Emit textual .ll.
  Assembly,   ///< Emit target .s.
  Docs,       ///< Emit a documentation sidecar (.rdoc) and nothing else.
  None,       ///< Type-check only.
};

enum class DumpKind {
  Nothing,
  Tokens,
  AST,
  Symbols,
  Types,
  Zombie,  ///< The borrow checker's own view of each body: places, loans, drops.
};

struct CompilerOptions {
  std::vector<std::string> Inputs;
  /// `--cfg <name>`: names `@Config` should treat as set.
  std::vector<std::string> ConfigFlags;
  std::vector<std::string> ImportPaths;  ///< -L: where to find .rul libraries
  std::vector<std::string> LinkLibraries;///< -l: extra native libraries
  std::vector<std::string> LinkPaths;    ///< -L for the system linker
  std::string OutputPath;
  std::string ModuleName;                ///< Defaults to the first input's stem.
  std::string TargetTriple;              ///< Empty means host.
  /// The C toolchain driver used to link. Empty means `cc`, or `$RUNE_CC`.
  /// A cross build names its own, e.g. `x86_64-w64-mingw32-gcc`.
  std::string LinkDriver;
  /// `--sysroot` handed to the link driver; empty leaves it alone.
  std::string Sysroot;
  /// Extra arguments appended to the link command verbatim.
  std::vector<std::string> LinkArgs;

  OutputKind Output = OutputKind::Executable;
  DumpKind Dump = DumpKind::Nothing;
  SafetyLevel Safety = SafetyLevel::Full;
  OverflowChecks Overflow = OverflowChecks::Default;
  MemoryMode Memory = MemoryMode::Arc;
  /// Report the Zombie borrow checker's findings inside the standard library
  /// as well as in the program. On by default now that the library is clean,
  /// so a regression there is caught; `--no-zombie-stdlib` silences it (the
  /// bodies are still read for their summaries either way).
  bool ZombieStdlib = true;

  unsigned OptLevel = 0;
  unsigned ErrorLimit = 20;
  bool DebugInfo = false;
  /// True when `-c`, `--emit-lib` and friends set `Output`; a file's own
  /// `@type` then leaves it alone.
  bool OutputKindFromFlag = false;
  bool ForceColor = false;
  bool NoColor = false;
  bool WarningsAsErrors = false;
  bool NoWarnings = false;
  bool NoStdlib = false;
  /// `--emit-docs --docs-stdlib`: write the standard library's own modules
  /// into the sidecar, rather than only the modules being compiled. This is
  /// how `rune doc std::io` gets a reference for a library that is never
  /// the package being documented.
  bool DocsStdlib = false;
  bool Verbose = false;
  bool TimeReport = false;
  std::string StdlibDir;
  std::string RuntimeLibDir;

  /// True when integer arithmetic should trap on overflow for this build.
  bool overflowChecksEnabled() const {
    switch (Overflow) {
    case OverflowChecks::On: return true;
    case OverflowChecks::Off: return false;
    case OverflowChecks::Default: break;
    }
    return OptLevel == 0 && Safety != SafetyLevel::None;
  }

  bool zombie() const { return Memory == MemoryMode::Zombie; }
};

/// The spelling `--memory` and `[build] memory` use.
const char *memoryModeName(MemoryMode mode);

/// Runs the whole pipeline for `opts`. Returns a process exit code.
int compileWithOptions(const CompilerOptions &opts);

/// Parses argv, then calls compileWithOptions.
int runCompilerMain(int argc, char **argv);

} // namespace rune

#endif
