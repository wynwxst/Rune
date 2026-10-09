//===- Manifest.h - Rune.toml, in memory -----------------------*- C++ -*-===//
#ifndef RUNE_MANIFEST_H
#define RUNE_MANIFEST_H

#include <map>
#include <string>
#include <vector>

namespace rune {

/// One `[dependencies]` entry. Only path dependencies are resolved today; a
/// `version` is recorded so the manifest round-trips.
struct Dependency {
  std::string Name;
  std::string Path;     ///< relative to the manifest's directory
  std::string Version;
  /// The registry it must come from, by the name `rune registry list`
  /// shows; empty means whichever configured registry has it.
  std::string Registry;
  /// `config = { backend = "vulkan" }` — what this project chooses for the
  /// keys that package declares in its own `[config]`. Values are written as
  /// they would be compared: a string as itself, a number or a boolean as it
  /// prints.
  std::map<std::string, std::string> Config;
};

/// A Rust crate in `[dependencies]`: `name = { cargo = "rust/name" }`.
///
/// Cargo builds it as a static library, and the build reads its
/// `#[no_mangle] pub extern "C"` functions, `#[repr(C)]` structs and enums
/// and its constants into a Rune module of the dependency's name — so
/// `import name` is all a Rune file needs to call it. See Cargo.h.
struct CargoCrate {
  std::string Name;                  ///< the module Rune code imports
  std::string Path;                  ///< the crate's directory, absolute
  std::vector<std::string> Features; ///< `features = ["simd"]`
  bool DefaultFeatures = true;       ///< `default-features = false`
};

/// A cross-compilation target named in `[target.<name>]`.
///
/// It needs a `triple`, or a foreign target to start from (see Targets.h).
/// Everything else describes the toolchain that builds for it — which is the
/// part a foreign target can usually find, and otherwise cannot be guessed,
/// because a cross toolchain is installed wherever its owner put it.
struct TargetSpec {
  std::string Name;        ///< the table's key, e.g. "mingw"
  /// `base = "wasm"`: the foreign target this one starts from, every key
  /// below overriding what it would have found. A table named after a
  /// foreign target, with no `triple`, starts from that one.
  std::string Base;
  std::string Triple;      ///< LLVM target triple
  std::string Cc;          ///< link driver, e.g. "x86_64-w64-mingw32-gcc"
  /// C++ compiler for this target's `cxx-sources`, e.g.
  /// "x86_64-w64-mingw32-g++". Derived from `cc` when not given.
  std::string Cxx;
  std::string Ar;          ///< archiver; derived from `cc` when not given
  std::string Sysroot;
  /// `sdk`: where the WASI SDK was unpacked, for a target based on `wasm`.
  std::string Sdk;
  std::string RuntimeDir;  ///< where this target's libruneruntime.a lives
  /// How to run a binary built for this target on this machine, e.g. "wine"
  /// or "qemu-aarch64". Empty means it cannot be run here.
  std::string Runner;
  /// `c-flags`: added to every C compile for this target, the runtime's
  /// included.
  std::vector<std::string> CFlags;
  std::vector<std::string> LinkLibraries;
  std::vector<std::string> LinkPaths;
  std::vector<std::string> LinkArgs;
  /// `linker`: the program that links, when it is not `cc`. A compiler
  /// driver (`i686-elf-gcc`, `clang`) or a linker itself (`i686-elf-ld`,
  /// `ld.lld`); may carry arguments of its own (`"clang --target=..."`).
  /// `"build-script"` hands the link to the package's build.rune.
  std::string Linker;
  /// `linker-kind`: `"driver"` or `"ld"` — how link flags are spelled for
  /// it. Worked out from `linker`'s name when not given.
  std::string LinkerKind;
  /// `default-flags`: whether the build adds flags of its own, to compiles,
  /// to the runtime or to the link. -1 when the table does not say: then a
  /// table with a `triple` of its own (no `base`) is *raw* — it gets none,
  /// and its runtime is built for it alone — and one starting from a
  /// foreign target keeps that target's.
  int DefaultFlags = -1;
};

/// An extra executable declared with `[[bin]]`.
struct BinaryTarget {
  std::string Name;
  std::string Path;
};

/// What a source file says it produces. A file that says nothing is a
/// component: importable, compiled into whatever names it.
enum class OutputKind { Component, Executable, Library, Object, Assembly, LLVM };

const char *outputKindName(OutputKind k);
/// The extension the kind's output carries, "" for an executable.
const char *outputKindSuffix(OutputKind k);

/// A file in `src/` that declares an output of its own, and so becomes a
/// target rather than being folded into the package library.
struct OutputRoot {
  std::string Name;   ///< the file's stem
  std::string Path;
  OutputKind Kind = OutputKind::Component;
};

/// A local unit: a folder directly under `src/`, compiled on its own into a
/// library the rest of the package imports. `src/net/http.rune` is module
/// `net::http`; `src/net/net.rune`, when there is one, is `net` itself.
struct LocalUnit {
  std::string Name;                 ///< the folder's name
  std::string Dir;                  ///< absolute
  std::vector<std::string> Sources; ///< every .rune under it
  std::vector<std::string> Uses;    ///< other units it imports
};

struct Manifest {
  // [package]
  std::string Name = "unnamed";
  std::string Version = "0.1.0";
  std::string Description;
  std::string License;
  std::string Edition = "2025";
  std::vector<std::string> Authors;

  // Layout, discovered from the package directory.
  std::string Root;             ///< directory containing Rune.toml
  std::string LibraryRoot;      ///< src/lib.rune, empty when absent
  std::string BinaryRoot;       ///< src/main.rune, empty when absent
  std::vector<std::string> Sources;   ///< the .rune files directly in src/
  /// The folders under src/, each a unit compiled before the package's own
  /// files, in an order where a unit comes after those it imports.
  std::vector<LocalUnit> Units;
  std::vector<std::string> TestFiles; ///< every .rune under tests/
  std::vector<BinaryTarget> Binaries;
  /// Every file under `src/` that declares its own output, discovered by
  /// reading what each file says. `main.rune` and `lib.rune` are here too.
  std::vector<OutputRoot> Roots;

  // [build]
  std::string Safety = "full";  ///< none | minimal | full
  /// `memory`: "zombie" (single ownership proven by the borrow checker, no
  /// counting at all — the default) or "arc" (reference counting). Read from
  /// the root package only: one program is one memory model, and every
  /// dependency is built for the root's.
  std::string Memory = "zombie";
  /// [build] emit = "llvm-ir" — what `rune build` produces for this package
  /// when the command line does not say. Empty means an executable.
  std::string Emit;
  unsigned OptLevel = 2;
  bool Debug = true;
  /// `overflow-checks`: 1 forces integer overflow to trap, 0 forces it to
  /// wrap, and -1 (unset) leaves it to the profile — trap in a debug build,
  /// wrap in a release one.
  int OverflowChecks = -1;
  bool WarningsAsErrors = false;
  bool NoStdlib = false;
  /// `[build] runtime = "none"`, or `#runtime(none)` atop a source file: a
  /// freestanding program — no C library, no hosted runtime. Like `memory`,
  /// the root package decides for the whole build.
  bool Freestanding = false;
  /// `[build] entry = "none"`, or `#entry(none)`: no generated `main`.
  bool NoEntry = false;
  std::vector<std::string> LinkLibraries; ///< [build] link = ["m", "z"]
  std::vector<std::string> LinkPaths;
  /// [build] c-sources = ["c/shim.c"] — compiled with the same toolchain the
  /// rest of the build uses, so a package with a C half cross-compiles too.
  std::vector<std::string> CSources;
  std::vector<std::string> CFlags;       ///< [build] c-flags
  /// [build] cxx-sources = ["cxx/shim.cpp"] — the same, for a C++ half. A
  /// package that has one links the C++ runtime, whether or not its Rune
  /// declares the functions in an `extern "C++"` block.
  std::vector<std::string> CxxSources;
  std::vector<std::string> CxxFlags;     ///< [build] cxx-flags
  /// [build] cxx-standard = "c++17" — what `-std=` the C++ half is built to.
  std::string CxxStandard;
  /// `[build] cfg = ["fast-math"]` — names `#Config(...)` should treat as set.
  /// A dependency's name is set too, so a package can ask whether it has one.
  std::vector<std::string> ConfigFlags;
  /// `[config] backend = "metal"` — keys of this package's own, compared in
  /// `#Config(backend == metal)`. A package that depends on this one may
  /// choose them instead; see `Dependency::Config`.
  std::map<std::string, std::string> Config;
  std::vector<std::string> LinkArgs;     ///< [build] link-args
  /// [build] linker-script = "kernel.ld" — where each section goes, for a
  /// program with no operating system to load it. Relative to the manifest.
  std::string LinkerScript;

  // [dependencies]
  std::vector<Dependency> Dependencies;
  /// `[dependencies]` entries with `cargo = "..."`: Rust crates, built by
  /// Cargo and bound into Rune modules.
  std::vector<CargoCrate> CargoCrates;

  // [target.<name>]
  std::vector<TargetSpec> Targets;
  /// The target named in `[build] target = "..."`, used when none is asked
  /// for on the command line. Empty means the host.
  std::string DefaultTarget;

  /// Looks up a configured target by name; null when there is none.
  const TargetSpec *findTarget(const std::string &name) const;

  bool producesLibrary() const { return !LibraryRoot.empty(); }
  bool producesBinary() const { return !BinaryRoot.empty() || !Binaries.empty(); }
  /// Files with no output of their own: the package's shared code.
  std::vector<std::string> componentSources() const;
};

/// Reads `<dir>/Rune.toml` and scans the package layout. `error` is filled in
/// on failure.
/// Reads `Rune.toml` from `dir`.
///
/// A package with nothing under `src/` is normally an error — there would be
/// nothing to build. `requireSources = false` accepts one anyway, which is
/// what documentation needs: `docs/` is written by hand, and a package that
/// is only prose is a package worth documenting.
bool loadManifest(const std::string &dir, Manifest &out, std::string &error,
                  bool requireSources = true);

/// The text written by `rune new`.
std::string defaultManifestText(const std::string &name, bool isLibrary);

} // namespace rune

#endif
