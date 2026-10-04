//===- Driver.h - Compiler options and pipeline ----------------*- C++ -*-===//
#ifndef RUNE_DRIVER_H
#define RUNE_DRIVER_H

#include <cstdint>
#include <map>
#include <filesystem>
#include <string>
#include <utility>
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

/// How the program keeps its heap alive. `Zombie` is the default; whatever
/// compiles under it also compiles under `Arc`, which accepts more (it can
/// share where Zombie must move or borrow).
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
  Shared,     ///< Emit a native shared library (.dylib / .so / .dll).
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
  /// `--cfg <name>`: names `#Config` should treat as set.
  std::vector<std::string> ConfigFlags;
  /// `--cfg <key>=<value>`: keys `#Config` compares against, the way `os` and
  /// `arch` are compared. A package's `[config]` table arrives this way.
  std::vector<std::pair<std::string, std::string>> ConfigValues;
  std::vector<std::string> ImportPaths;  ///< -I: where to find .rul libraries
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
  /// Set while building a `#type(Macros)` package, which is an ordinary
  /// compilation of those very files: without it they would be skipped again
  /// and the package would be empty.
  bool MacroPackage = false;
  /// Extra arguments appended to the link command verbatim.
  std::vector<std::string> LinkArgs;
  /// `--link-cxx`: link the target's C++ runtime. Set by the compiler itself
  /// when an `extern "C++"` block is seen; a build tool passes it when a
  /// package has C++ sources that only reach Rune through `extern "C"`.
  bool LinkCxx = false;
  /// `--linker-kind ld`: the link program is a linker run directly
  /// (`i686-elf-ld`, `ld.lld`), not a compiler driver, so what the compiler
  /// adds is spelled as a linker takes it — `-T x`, not `-Wl,-T,x` — and
  /// nothing only a driver understands is passed at all.
  bool LinkerIsLd = false;
  /// `--no-default-link-args`: the link command is the objects, `-o`, and
  /// exactly what `--link-arg`, `-L` and `-l` said — nothing of the
  /// compiler's own (`-nostdlib`, `-static`, `-lm`, the runtime archive...).
  bool NoDefaultLinkArgs = false;
  /// `--cxx-stdlib`: which C++ runtime `LinkCxx` links, `libc++` or
  /// `libstdc++`. Empty means the platform's own: libc++ on Apple, FreeBSD
  /// and wasm, libstdc++ elsewhere. C++ built with `-stdlib=libc++` on Linux
  /// — an LLVM built that way, say — needs `libc++`.
  std::string CxxStdlib;

  OutputKind Output = OutputKind::Executable;
  DumpKind Dump = DumpKind::Nothing;
  SafetyLevel Safety = SafetyLevel::Full;
  OverflowChecks Overflow = OverflowChecks::Default;
  MemoryMode Memory = MemoryMode::Zombie;
  /// Report the Zombie borrow checker's findings inside the standard library
  /// as well as in the program. On by default now that the library is clean,
  /// so a regression there is caught; `--no-zombie-stdlib` silences it (the
  /// bodies are still read for their summaries either way).
  bool ZombieStdlib = true;

  unsigned OptLevel = 2;
  unsigned ErrorLimit = 20;
  bool DebugInfo = false;
  /// True when `-c`, `--emit-lib` and friends set `Output`; a file's own
  /// `#type` then leaves it alone.
  bool OutputKindFromFlag = false;
  bool ForceColor = false;
  bool NoColor = false;
  /// `--diagnostic-format json`: one JSON object per diagnostic on stderr.
  bool JsonDiagnostics = false;
  /// `--diagnostic-format short`: `file:line:col: error: message` lines.
  bool ShortDiagnostics = false;
  /// `--source <path>=<file>`: compile `path` as the text in `file` says,
  /// reporting it as `path`. How an editor has an unsaved buffer checked.
  std::map<std::string, std::string> SourceOverrides;
  /// `--query-members <file>:<start>:<end>`: after checking, print the type
  /// of the expression spanning those bytes of `file`, and its members, as
  /// JSON on stdout — and produce nothing else.
  std::string QueryFile;
  uint32_t QueryStart = 0;
  uint32_t QueryEnd = 0;
  /// `--query-hints <file>`: after checking, print the types of the file's
  /// unannotated bindings and the parameter names of its positional
  /// arguments, as JSON — what an editor shows greyed out and a formatter
  /// can write in.
  bool QueryHints = false;
  bool WarningsAsErrors = false;
  bool NoWarnings = false;
  bool NoStdlib = false;
  /// Objects compiled from imported `.rul` libraries are linked into this
  /// artefact. They may call the freestanding runtime by name, so its entry
  /// points have to survive even where this module does not call them.
  bool LinksRuneLibraries = false;
  /// `--tiers`: build the standard library freestanding and say, for every
  /// function, whether it works there (`bare`) or what it needs from a hosted
  /// build. `TierOut`, when set, receives the answers instead of stdout.
  bool TierReport = false;
  std::map<std::string, std::string> *TierOut = nullptr;
  /// `--runtime none` (or `#runtime(none)` at the top of a file): build a
  /// freestanding program. Nothing hosted is linked — no C library, no
  /// `libruneruntime.a` — and what the generated code needs of a runtime
  /// comes from `runetime/freestanding.rune`, compiled in, and from the
  /// program's own `#panicHandler`, `#allocator` and `#deallocator`.
  bool Freestanding = false;
  /// `--entry none` (or `#entry(none)`): generate no `main`. The program's
  /// own `#export`ed function is where execution starts.
  bool NoEntry = false;
  /// Where `runetime/` is: the freestanding runtime's source.
  std::string RunetimeDir;
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
    // On wherever safety checks are, whatever the optimisation level:
    // `-O2` is the default, and an overflow should not stop trapping just
    // because nobody asked for `-O0`. A release build turns it off.
    return Safety != SafetyLevel::None;
  }

  bool zombie() const { return Memory == MemoryMode::Zombie; }
};

/// The spelling `--memory` and `[build] memory` use.
const char *memoryModeName(MemoryMode mode);

/// The directory the running compiler is in.
std::filesystem::path hostExecutableDir();

/// The runtime built for the machine doing the compiling: installed beside
/// the compiler, or the build tree's.
std::string hostRuntimeLibDir();

/// Runs the whole pipeline for `opts`. Returns a process exit code.
int compileWithOptions(const CompilerOptions &opts);

/// Parses argv, then calls compileWithOptions.
int runCompilerMain(int argc, char **argv);

} // namespace rune

#endif
