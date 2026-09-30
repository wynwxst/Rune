//===- Targets.h - What a build is for --------------------------*- C++ -*-===//
//
// A build is for one machine. Usually that is this one; `--target` names
// another, and this file turns the name into a toolchain: which compiler
// links, which archiver packs the runtime, which sysroot holds the libc, and
// what — if anything — can run the result here.
//
// Names come from two places. A handful of *foreign targets* are built in:
// `wasm`, `windows`, `linux-arm64` and the like, each knowing its triple and
// where its toolchain is usually installed, so `rune build --target wasm`
// needs no configuration at all. A package may also describe its own in
// `[target.<name>]`, either from scratch with a `triple`, or as a foreign
// target with a few keys changed (`base = "wasm"`).
//
//===----------------------------------------------------------------------===//
#ifndef RUNE_PM_TARGETS_H
#define RUNE_PM_TARGETS_H

#include "Manifest.h"

#include <string>
#include <vector>

namespace rune {

/// How a foreign target's toolchain is found on this machine.
enum class ToolchainKind {
  /// A GNU cross toolchain on `PATH`, named after its triple:
  /// `<prefix>-gcc`, `<prefix>-g++`, `<prefix>-ar`.
  GnuPrefix,
  /// The WASI SDK: one directory holding clang, llvm-ar and a sysroot with
  /// wasi-libc in it. Found through `sdk`, `$WASI_SDK_PATH`, or the places
  /// its installers put it.
  WasiSdk,
  /// This machine's clang and ld.lld, told the target: one compiler for
  /// every bare-metal target, since there is no libc to match.
  Clang,
};

/// A platform Rune can build for by name alone.
struct ForeignTarget {
  std::string Name;                 ///< what `--target` takes: "wasm"
  std::vector<std::string> Aliases; ///< other spellings: "wasi", the triple
  std::string Summary;              ///< one line for `rune targets`
  std::string Triple;
  ToolchainKind Toolchain = ToolchainKind::GnuPrefix;
  std::string ToolPrefix;           ///< GnuPrefix: "x86_64-w64-mingw32"
  /// Commands that can start a built program here, best first. The first
  /// whose program is on `PATH` is used.
  std::vector<std::string> Runners;
  /// Flags every C compile for the target needs — the runtime's and a
  /// package's own `c-sources`.
  std::vector<std::string> CFlags;
  /// Native libraries the runtime needs on this target.
  std::vector<std::string> LinkLibraries;
  /// Added to the link command whatever links it.
  std::vector<std::string> LinkArgs;
  /// Flags that belong to the toolchain this target finds, not to the
  /// target: clang's `--target=...`, `-fuse-ld=lld`. They are used only
  /// while that toolchain is: a package that names its own `cc` gets none of
  /// the compile ones, and one that names its own `cc` or `linker` none of
  /// the link ones. Nothing is forced on a toolchain it was not written for.
  std::vector<std::string> ToolchainCFlags;
  std::vector<std::string> ToolchainLinkArgs;
  /// Bare metal: no operating system, so no hosted runtime — the program is
  /// built as `@runtime(none)` whatever its sources say.
  bool Freestanding = false;
  /// How to get the toolchain, for when it is missing.
  std::string InstallHint;
};

/// Every foreign target, in the order `rune targets` lists them.
const std::vector<ForeignTarget> &foreignTargets();

/// The foreign target called `name`, by its name or an alias; null when none.
const ForeignTarget *findForeignTarget(const std::string &name);

/// A target, resolved once from the root package's manifest and the command
/// line, then carried through every step of the build.
struct ResolvedTarget {
  bool Active = false;         ///< false means an ordinary host build
  std::string Name;            ///< what `target/<name>/` is called
  std::string Triple;
  std::string Cc;
  std::string Cxx;
  std::string Ar;
  std::string Sysroot;
  std::string RuntimeDir;
  std::string Runner;          ///< wine, wasmtime, ... ; empty = cannot run
  /// The runner the target would use if it were installed, for saying what
  /// to install when `Runner` is empty.
  std::string WantedRunner;
  std::vector<std::string> CFlags;
  std::vector<std::string> LinkLibraries;
  std::vector<std::string> LinkPaths;
  std::vector<std::string> LinkArgs;
  /// Bare metal: see `ForeignTarget::Freestanding`.
  bool Freestanding = false;
  /// The program that links, when it is not `Cc`; see `TargetSpec::Linker`.
  std::string Linker;
  /// `driver` or `ld`, as given; `linkerKindOf` works it out otherwise.
  std::string LinkerKind;
  /// False: the compiler adds no link flags of its own either
  /// (`runec --no-default-link-args`).
  bool DefaultFlags = true;

  /// Enough to name what a build produces. Parsing the whole triple here
  /// would mean linking LLVM into the package driver for two questions.
  bool isWindows() const {
    return Triple.find("windows") != std::string::npos ||
           Triple.find("mingw") != std::string::npos;
  }
  bool isWasm() const { return Triple.rfind("wasm", 0) == 0; }
  std::string exeSuffix() const {
    if (isWindows())
      return ".exe";
    if (isWasm())
      return ".wasm";
    return "";
  }
};

/// Why a target could not be resolved: a line to fail with and the notes
/// that say what to do about it.
struct TargetProblem {
  std::string Message;
  std::vector<std::string> Notes;
};

/// Turns `--target <name>` into the toolchain to build with.
///
/// `name` is, in order: a `[target.<name>]` table; a foreign target or one
/// of its aliases; or a bare triple, which covers a target that needs no
/// configuration beyond the triple.
bool resolveTarget(const Manifest &m, const std::string &name,
                   ResolvedTarget &out, TargetProblem &problem);

/// The WASI SDK's directory: `configured` when given, otherwise wherever it
/// is usually installed. Empty when there is none.
std::string findWasiSdk(const std::string &configured = "");

/// The full path of `program` on `PATH`, or empty. A path with a slash in it
/// is taken as it is, if it exists.
std::string findOnPath(const std::string &program);

/// How the target's linker takes its flags: `"ld"` for a linker run
/// directly (`ld`, `i686-elf-ld`, `ld.lld`, `wasm-ld`), `"driver"` for a
/// compiler that links (`cc`, `i686-elf-gcc`, `clang`). `linker-kind` says
/// so outright; otherwise the linker's name does.
std::string linkerKindOf(const ResolvedTarget &t);

/// A linker script on the link line, in the spelling the linker takes.
std::vector<std::string> linkerScriptArgs(const ResolvedTarget &t,
                                          const std::string &script);

/// The archiver for a target: `ar` when configured, else derived from `cc`.
std::string archiverFor(const ResolvedTarget &t);

/// The C++ driver for a target: `cxx` when configured, else derived from `cc`.
std::string cxxDriverFor(const ResolvedTarget &t);

/// Whether `cc`'s own position-independence flag belongs on a compile for
/// `t`. WebAssembly is linked statically, and `-fPIC` there asks for dynamic
/// linking's imports instead.
bool wantsPic(const ResolvedTarget &t);

/// `rune targets`: the foreign targets and the package's own, each with
/// whether it can be built — and run — on this machine.
int listTargets(const Manifest *m);

} // namespace rune

#endif
