//===- main.cpp - `rune`, the Rune package manager -------------*- C++ -*-===//
//
// Cargo-shaped front end over `runec`:
//
//   rune new <name> [--lib]   scaffold a package
//   rune init [--lib]         scaffold in the current directory
//   rune build [--release]    build the package and its dependencies
//   rune run [--release] [-- args...]
//   rune test [--release]     build and run everything under tests/
//   rune doc                  read docs/ and the source, write target/<p>/docs
//   rune check                type-check without producing output
//   rune clean                remove target/
//
// Dependencies are built first, each into its own `target/<profile>/`, and
// their `.rul` files are collected into this package's deps directory.
//
// The build is a graph rather than a walk. Every package that can be reached
// from the root is resolved first, and what it produces becomes steps ordered
// only by what genuinely needs what: two packages that do not depend on each
// other compile at the same time, as do a package's own binaries once its
// library exists. A step is skipped when a digest of everything it reads —
// its sources, its dependencies, the compiler and the standard library —
// matches the one recorded beside its output.
//
//===----------------------------------------------------------------------===//

#include "Console.h"
#include "Fingerprint.h"
#include "Jobs.h"
#include "Manifest.h"
#include "Registry.h"
#include "Targets.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <unistd.h>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace pm = rune::pm;
using namespace rune;

namespace {

//===----------------------------------------------------------------------===//
// Console output lives in Console.cpp, shared with the registry.
//===----------------------------------------------------------------------===//

using pm::c;
using pm::failLine;
using pm::gColor;
using pm::note;
using pm::okLine;
using pm::quote;
using pm::runeHome;
using pm::status;
using pm::warnLine;

//===----------------------------------------------------------------------===//
// Locating runec
//===----------------------------------------------------------------------===//

std::string gExecutableDir;
/// `-v`, for the parts of a build that run before an Options is in hand.
bool gVerboseBuild = false;

/// `--no-open`: say where a page is instead of showing it, for a machine
/// with no browser to show it in, or a script that only wants it built.
bool gNoBrowser = false;

std::string findCompiler() {
  if (const char *env = getenv("RUNEC"))
    return env;
  std::error_code ec;
  // Installed layout and build-tree layout both put runec beside rune.
  for (const fs::path &candidate :
       {fs::path(gExecutableDir) / "runec",
        fs::path(gExecutableDir) / ".." / "bin" / "runec"}) {
    // A regular file, not merely something by that name: run from the root
    // of this repository, `./runec` is the compiler's *source* directory.
    if (fs::is_regular_file(candidate, ec))
      return fs::absolute(candidate).lexically_normal().string();
  }
  return "runec";
}

/// The directory `rune` itself was run from. `argv[0]` carries one only when
/// the program was named by a path; run through `PATH` it is a bare name, and
/// the directory is whichever entry of `PATH` holds it.
std::string executableDirOf(const char *argv0) {
  fs::path given(argv0 ? argv0 : "");
  if (given.has_parent_path())
    return given.parent_path().string();
  if (const char *path = getenv("PATH")) {
    std::string entries = path;
    size_t start = 0;
    while (start <= entries.size()) {
      size_t end = entries.find(':', start);
      if (end == std::string::npos)
        end = entries.size();
      std::string dir = entries.substr(start, end - start);
      std::error_code ec;
      if (!dir.empty() && fs::is_regular_file(fs::path(dir) / given, ec))
        return dir;
      start = end + 1;
    }
  }
  return ".";
}

int runCommand(const std::string &cmd, bool verbose) {
  if (verbose)
    std::cerr << c("\x1b[2m") << "  " << cmd << c("\x1b[0m") << "\n";
  int rc = std::system(cmd.c_str());
  // std::system encodes the exit status; recover the process's own code.
  if (rc == -1)
    return 127;
  return (rc & 0x7F) ? 128 + (rc & 0x7F) : ((rc >> 8) & 0xFF);
}

//===----------------------------------------------------------------------===//
// Build options shared by every command
//===----------------------------------------------------------------------===//

struct Options {
  bool Release = false;
  bool Verbose = false;
  /// `--memory <mode>`, or the root manifest's `[build] memory`. Applies to
  /// every package in the build: a program is one memory model throughout.
  std::string Memory = "zombie";
  /// A freestanding build: the root manifest says `runtime = "none"` (or a
  /// source says `@runtime(none)`), or the target is bare metal. Everything
  /// in the build is compiled without the hosted runtime.
  bool Freestanding = false;
  bool CheckOnly = false;
  bool RunAll = false;         ///< `rune run --all`
  /// `--emit <kind>`, or `[build] emit`. When set, this package's own roots
  /// are emitted in that form instead of being linked or archived — one file
  /// per root, named after it. Dependencies still build as libraries, because
  /// that is what this package needs from them in order to compile at all.
  bool EmitSet = false;
  OutputKind Emit = OutputKind::LLVM;
  std::string RunTarget;       ///< `rune run <name>`
  /// `rune doc --open`: show the page once it is written.
  bool OpenDocs = false;
  /// `rune doc std::io`: the standard library module to show, from the
  /// cached reference, rather than this package's documentation.
  std::string DocModule;
  std::string PackageDir = ".";
  std::vector<std::string> ProgramArgs;
  std::string TargetName;      ///< `--target`, before it is resolved
  /// `--cfg <name>`, joined with whatever the manifest asked for.
  std::vector<std::string> ConfigFlags;
  /// `--cfg key=value` on `rune`'s own command line: a key for this build,
  /// over whatever the manifest said.
  std::vector<std::pair<std::string, std::string>> ConfigValues;
  ResolvedTarget Target;
};

std::string profileName(const Options &o) {
  return o.Release ? "release" : "debug";
}

/// The spellings `--emit` and `[build] emit` accept. Returns false for a name
/// that is none of them.
bool parseEmitKind(const std::string &name, OutputKind &out) {
  static const std::map<std::string, OutputKind> kinds = {
      {"llvm-ir", OutputKind::LLVM},   {"llvm", OutputKind::LLVM},
      {"ir", OutputKind::LLVM},        {"ll", OutputKind::LLVM},
      {"asm", OutputKind::Assembly},   {"assembly", OutputKind::Assembly},
      {"s", OutputKind::Assembly},     {"obj", OutputKind::Object},
      {"object", OutputKind::Object},  {"o", OutputKind::Object},
      {"lib", OutputKind::Library},    {"library", OutputKind::Library},
      {"rul", OutputKind::Library},    {"exe", OutputKind::Executable},
      {"bin", OutputKind::Executable}, {"executable", OutputKind::Executable},
  };
  std::string lower;
  for (char ch : name)
    lower += static_cast<char>(tolower(static_cast<unsigned char>(ch)));
  auto it = kinds.find(lower);
  if (it == kinds.end())
    return false;
  out = it->second;
  return true;
}

/// The `runec` flag that asks for `k`. Empty for an executable, which is what
/// the compiler does when nothing says otherwise.
const char *emitFlagFor(OutputKind k) {
  switch (k) {
  case OutputKind::Library:  return " --emit-lib";
  case OutputKind::Object:   return " -c";
  case OutputKind::Assembly: return " --emit-asm";
  case OutputKind::LLVM:     return " --emit-llvm";
  default: return "";
  }
}

/// The command that runs a built executable on *this* machine. A host build
/// runs itself; a cross build needs an emulator, and without one there is
/// nothing sensible to run.
std::string launchCommand(const Options &o, const std::string &exe,
                          const std::string &args = "") {
  std::string cmd;
  // `{}` in a runner is where the executable goes — `qemu -kernel {} -s` —
  // and the arguments still come last. Without one, the executable does.
  std::string runner = o.Target.Active ? o.Target.Runner : std::string();
  size_t slot = runner.find("{}");
  if (slot != std::string::npos) {
    cmd = runner.substr(0, slot) + quote(exe) + runner.substr(slot + 2);
  } else {
    if (!runner.empty())
      cmd = runner + " ";
    cmd += quote(exe);
  }
  if (!args.empty())
    cmd += " " + args;
  return cmd;
}

/// True when a built executable can be started here.
bool canRunHere(const Options &o) {
  return !o.Target.Active || !o.Target.Runner.empty();
}

/// Where a package's artifacts go. A cross build gets a directory of its own,
/// so host and cross output never overwrite each other and neither has to be
/// rebuilt after switching.
fs::path buildDir(const Manifest &m, const Options &o) {
  fs::path base = fs::path(m.Root) / "target";
  if (o.Target.Active)
    base /= o.Target.Name;
  return base / profileName(o);
}

/// Adds the flags a manifest asks for to a `runec` command line.
void appendBuildFlags(std::string &cmd, const Manifest &m, const Options &o,
                      const std::map<std::string, std::string> &config) {
  cmd += " --safety " + m.Safety;
  cmd += " --memory " + o.Memory;
  if (o.Freestanding)
    cmd += " --runtime none";
  if (m.NoEntry)
    cmd += " --entry none";
  unsigned opt = o.Release ? std::max(2u, m.OptLevel) : m.OptLevel;
  cmd += " -O" + std::to_string(opt);
  if (m.Debug && !o.Release)
    cmd += " -g";
  // Integer overflow traps in a debug build and wraps in a release one; the
  // compiler reads that off `-O`. The manifest may pin it either way.
  if (m.OverflowChecks == 1)
    cmd += " --overflow-checks";
  else if (m.OverflowChecks == 0)
    cmd += " --no-overflow-checks";
  if (m.WarningsAsErrors)
    cmd += " -Werror";
  if (m.NoStdlib)
    cmd += " --no-stdlib";
  if (o.Target.Active) {
    cmd += " --target " + quote(o.Target.Triple);
    if (!o.Target.Cc.empty())
      cmd += " --cc " + quote(o.Target.Cc);
    // What links, and how it takes its flags. A build script as the linker
    // is only known per package; `buildTarget` adds it.
    if (!o.Target.Linker.empty() && o.Target.Linker != "build-script")
      cmd += " --linker " + quote(o.Target.Linker);
    cmd += " --linker-kind " + linkerKindOf(o.Target);
    if (!o.Target.DefaultFlags)
      cmd += " --no-default-link-args";
    if (!o.Target.Sysroot.empty())
      cmd += " --sysroot " + quote(o.Target.Sysroot);
    if (!o.Target.RuntimeDir.empty())
      cmd += " --runtime-dir " + quote(o.Target.RuntimeDir);
    for (const std::string &a : o.Target.LinkArgs)
      cmd += " --link-arg " + quote(a);
  }
  // `@Config` decides which declarations exist, so these change what is
  // produced and belong here — unlike `-v` and `--color` below.
  //
  // A dependency's name is set too, so a package can ask whether it has one:
  // `@Config(json)` is true exactly when `json` is in `[dependencies]`.
  {
    std::vector<std::string> names = m.ConfigFlags;
    for (const Dependency &d : m.Dependencies)
      names.push_back(d.Name);
    for (const std::string &n : o.ConfigFlags)
      names.push_back(n);
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    for (const std::string &n : names)
      cmd += " --cfg " + quote(n);
    // Keys with values: the package's own `[config]`, as its dependents
    // chose them, and anything `--cfg key=value` added on the command line.
    for (const auto &kv : config)
      cmd += " --cfg " + quote(kv.first + "=" + kv.second);
    for (const auto &kv : o.ConfigValues)
      cmd += " --cfg " + quote(kv.first + "=" + kv.second);
  }
  // Native link flags are not added here: they come from DependencyInputs,
  // which already merges this package's own with its dependencies' and
  // removes duplicates.
  //
  // Nor `-v` or `--color`. Everything above changes what the compiler
  // produces and so belongs in the digest that decides whether to run it at
  // all; those two only change how it narrates itself, and putting them here
  // would mean `rune build -v` rebuilt the world, and `rune build` after it
  // rebuilt the world again. `runStep` adds them when it runs the command.
}

//===----------------------------------------------------------------------===//
// Building
//===----------------------------------------------------------------------===//

/// Everything about a compile that could change what it produces, gathered so
/// the step can be skipped when none of it has.
///
/// The command line covers the flags, the module name and which files go in;
/// the sources cover what those files say; the toolchain covers the compiler
/// and the standard library, which is compiled from source into every artefact
/// and so is an input to all of them.
pm::Fingerprint stepFingerprint(const std::string &cmd,
                                const std::vector<std::string> &inputs) {
  pm::Fingerprint fp;
  fp.add(cmd);
  fp.add(pm::toolchainFingerprint(findCompiler(), RUNE_DEFAULT_STDLIB_DIR).hex());
  for (const std::string &in : inputs)
    fp.addFile(in);
  return fp;
}

/// Runs one compile, printing what it is doing and, if it fails, why.
///
/// Steps run alongside one another, so the child's output is captured and
/// written out in one piece rather than a line at a time — two interleaved
/// diagnostics are worse than either on its own. The child cannot see a
/// terminal through a pipe, so it is told whether to colour.
/// `--diagnostic-format json`: every compile reports one JSON object per
/// diagnostic, for an editor to read. Like colour, it changes only how the
/// compiler narrates, so it stays out of the digest.
static bool gJsonDiagnostics = false;
/// `--diagnostic-format short`: `file:line:col: error: message` lines, which
/// Vim's quickfix list and other `make`-minded tools read.
static bool gShortDiagnostics = false;
/// `--source <path>=<file>` pairs, handed to every compile: an editor's
/// unsaved buffers, checked in place of what is on disk.
static std::vector<std::string> gSourceOverrides;

bool runStep(const std::string &verb, const std::string &what,
             const std::string &cmd, const Options &opts) {
  std::string line = std::string(c("\x1b[32m")) + "○" + c("\x1b[0m") + " " +
                     c("\x1b[1m") + verb + c("\x1b[0m") + " " + what + "\n";
  if (opts.Verbose)
    line += std::string(c("\x1b[2m")) + "  " + cmd + c("\x1b[0m") + "\n";

  // Added here rather than to the command the digest was taken over: see
  // `appendBuildFlags`. A child writing into a pipe cannot see the terminal
  // this output is bound for, so it has to be told.
  std::string full = cmd;
  if (gColor)
    full += " --color";
  if (gJsonDiagnostics)
    full += " --diagnostic-format json";
  else if (gShortDiagnostics)
    full += " --diagnostic-format short";
  // Only a check: a build's output is cached against what is on disk, and
  // must not be made from text that was never saved.
  if (full.find(" --check") != std::string::npos)
    for (const std::string &o : gSourceOverrides)
      full += " --source " + quote(o);
  if (opts.Verbose)
    full += " -v";

  // The compiler spreads its own front end over the cores it can see, and
  // several compilers are running here. Left alone they would each ask for
  // the whole machine and spend the difference fighting over it, so the
  // machine is divided between them instead.
  full = "RUNE_JOBS=" + std::to_string(pm::sharePerJob()) + " " + full;

  std::string output;
  int rc = pm::runCaptured(full, output);
  pm::writeSerialized(line + output);
  return rc == 0;
}

struct BuildResult {
  bool Ok = false;
  std::string LibraryPath;   ///< .rul produced, when the package has a lib
  std::string ExecutablePath;  ///< the one `rune run` means by default
  /// Every executable the package produced, in build order, as name -> path.
  std::vector<std::pair<std::string, std::string>> Executables;
  /// What the build script said `rune run` should start in an executable's
  /// place — a disk image made from a kernel, say — by executable path.
  std::map<std::string, std::string> RunWith;
};

/// What a package's dependencies contribute to the build that consumes them:
/// the libraries themselves, plus any native libraries they need at link time.
struct DependencyInputs {
  std::vector<std::string> Libraries;   ///< staged .rul paths
  std::vector<std::string> LinkLibs;    ///< -l names, in dependency order
  std::vector<std::string> LinkPaths;   ///< -L directories
  /// Objects compiled from `c-sources` and `cxx-sources`, this package's and
  /// its dependencies'.
  std::vector<std::string> Objects;
  std::vector<std::string> LinkArgs;    ///< [build] link-args, verbatim
  /// True when anything in the graph has a C++ half. The C++ runtime is then
  /// linked even if no Rune file declares an `extern "C++"` block — the
  /// objects themselves need it.
  bool NeedsCxx = false;

  void merge(const DependencyInputs &other) {
    auto append = [](std::vector<std::string> &into,
                     const std::vector<std::string> &from) {
      for (const std::string &v : from)
        if (std::find(into.begin(), into.end(), v) == into.end())
          into.push_back(v);
    };
    append(Libraries, other.Libraries);
    append(LinkLibs, other.LinkLibs);
    append(LinkPaths, other.LinkPaths);
    append(Objects, other.Objects);
    append(LinkArgs, other.LinkArgs);
    NeedsCxx = NeedsCxx || other.NeedsCxx;
  }
};

/// Compiles a package's C and C++ sources with the same toolchain the rest of
/// the build uses, and returns the objects to hand the linker.
///
/// This is what lets a package have a C or C++ half at all: the compiler links
/// one object, and nothing else knows how to produce one from a `.c` or a
/// `.cpp`. Using the build's own `cc` is what makes such a package
/// cross-compile; the C++ driver is the one `cxxDriverFor` pairs with it.
bool buildNativeSources(const Manifest &m, const Options &opts,
                        const fs::path &dir, const pm::FingerprintStore &stamps,
                        std::vector<std::string> &objects) {
  if (m.CSources.empty() && m.CxxSources.empty())
    return true;
  std::error_code ec;
  fs::create_directories(dir, ec);
  const std::string cc = opts.Target.Cc.empty() ? std::string("cc") : opts.Target.Cc;
  const std::string cxx = cxxDriverFor(opts.Target);

  struct Half {
    const std::vector<std::string> &Sources;
    const std::vector<std::string> &Flags;
    const std::string &Driver;
    bool IsCxx;
  };
  const Half halves[] = {
      {m.CSources, m.CFlags, cc, false},
      {m.CxxSources, m.CxxFlags, cxx, true},
  };

  for (const Half &half : halves) {
    for (const std::string &src : half.Sources) {
      if (!fs::exists(src, ec)) {
        failLine(std::string("cannot find the ") + (half.IsCxx ? "C++" : "C") +
                 " source '" + src + "'");
        note(std::string("paths in `") + (half.IsCxx ? "cxx-sources" : "c-sources") +
             "` are relative to Rune.toml");
        return false;
      }
      // Two sources with the same stem, one C and one C++, would otherwise
      // write the same object.
      fs::path obj = dir / (fs::path(src).stem().string() +
                            (half.IsCxx ? ".cxx.o" : ".o"));
      objects.push_back(obj.string());

      // A raw target (no default flags) gets `-c`, `-o` and what it and the
      // package said: no position independence, optimisation, debug info or
      // language standard of the build's choosing.
      const bool raw = opts.Target.Active && !opts.Target.DefaultFlags;
      std::string cmd = quote(half.Driver) + " -c";
      if (!raw && wantsPic(opts.Target))
        cmd += " -fPIC";
      if (half.IsCxx && (!raw || !m.CxxStandard.empty()))
        cmd += " -std=" + quote(m.CxxStandard.empty() ? std::string("c++17")
                                                      : m.CxxStandard);
      if (!raw)
        cmd += opts.Release ? " -O2" : " -O0 -g";
      if (opts.Target.Active && opts.Target.Cc.empty() &&
          opts.Target.DefaultFlags)
        cmd += " --target=" + quote(opts.Target.Triple);
      if (!opts.Target.Sysroot.empty())
        cmd += " --sysroot=" + quote(opts.Target.Sysroot);
      for (const std::string &f : opts.Target.CFlags)
        cmd += " " + quote(f);
      for (const std::string &f : half.Flags)
        cmd += " " + quote(f);
      cmd += " -o " + quote(obj.string()) + " " + quote(src);

      pm::Fingerprint fp;
      fp.add(cmd);
      fp.addStamp(half.Driver);
      fp.addFile(src);
      fp.addFile((fs::path(m.Root) / "Rune.toml").string());
      if (stamps.isFresh(obj, fp))
        continue;

      // A C or C++ compiler's own diagnostics, not the Rune compiler's:
      // `--color` is not a flag it would take.
      std::string output;
      std::string line = std::string(c("\x1b[32m")) + "○" + c("\x1b[0m") + " " +
                         c("\x1b[1m") + "Compiling" + c("\x1b[0m") + " " +
                         fs::path(src).filename().string() + "\n";
      if (opts.Verbose)
        line += std::string(c("\x1b[2m")) + "  " + cmd + c("\x1b[0m") + "\n";
      int rc = pm::runCaptured(cmd, output);
      pm::writeSerialized(line + output);
      if (rc != 0) {
        failLine("could not compile '" + src + "'");
        return false;
      }
      stamps.record(obj, fp);
    }
  }
  return true;
}

//===----------------------------------------------------------------------===//
// The package graph
//===----------------------------------------------------------------------===//

/// One package in the build, with the edges that say what has to come first.
///
/// Resolving the whole graph before building any of it is what makes the
/// build concurrent: a recursive build discovers a dependency only once it
/// needs it, and can do nothing but wait for it, whereas a graph known in
/// advance says which packages have nothing to do with each other and can
/// therefore be compiled at the same time.
//===----------------------------------------------------------------------===//
// Build scripts
//
// A package's `build.rune` is an ordinary Rune program, compiled for the
// machine doing the building and run twice: `prepare`, before the package is
// compiled, and `finish`, after each of its executables is linked. It is told
// where it is through `RUNE_*` variables and answers with `rune:key=value`
// lines on its output; `std::build` wraps both sides.
//===----------------------------------------------------------------------===//

/// What a build script answered.
struct ScriptAnswers {
  std::vector<std::string> Cfgs;       ///< `name`, or `key=value`
  std::vector<std::string> LinkArgs;
  std::vector<std::string> LinkLibs;
  std::vector<std::string> LinkPaths;  ///< made absolute
  std::string RunWith;                 ///< finish only; absolute
};

/// Adds a script's `@Config` answers to a `runec` command line.
void appendScriptConfig(std::string &cmd, const ScriptAnswers &a) {
  for (const std::string &c : a.Cfgs)
    cmd += " --cfg " + quote(c);
}

/// Adds its link answers.
void appendScriptLink(std::string &cmd, const ScriptAnswers &a) {
  for (const std::string &dir : a.LinkPaths)
    cmd += " -L" + quote(dir);
  for (const std::string &lib : a.LinkLibs)
    cmd += " -l" + quote(lib);
  for (const std::string &arg : a.LinkArgs)
    cmd += " --link-arg " + quote(arg);
}

struct PackageNode {
  std::string Dir;              ///< canonical path
  Manifest M;
  std::vector<size_t> Deps;     ///< indices of direct dependencies
  bool IsRoot = false;
  /// What this package is compiled with: its own `[config]`, with whatever
  /// its dependents chose for those keys written over it. A package is built
  /// once, so two dependents that disagree is an error rather than a race.
  std::map<std::string, std::string> Config;
  /// Which dependent chose each overridden key, so a clash can name both.
  std::map<std::string, std::string> ConfigChosenBy;

  // Filled in as the build runs. Each is written by this package's own
  // library step and read by its dependents' — which the graph guarantees
  // run afterwards.
  BuildResult Result;
  DependencyInputs Inputs;
  /// The package's `build.rune`, compiled for this machine, and what its
  /// prepare phase answered. Empty when it has none.
  std::string ScriptExe;
  ScriptAnswers Script;
};

/// Loads every manifest reachable from `rootDir`, depth first, so that a
/// package appears in `out` only after everything it depends on.
///
/// Returns false, having said why, for a missing dependency, a malformed
/// manifest, or a cycle.
bool resolvePackages(const std::string &rootDir, std::vector<PackageNode> &out,
                     std::map<std::string, size_t> &index,
                     std::vector<std::string> &onPath, size_t &result) {
  std::string canonical = fs::absolute(rootDir).lexically_normal().string();

  auto already = index.find(canonical);
  if (already != index.end()) {
    result = already->second;
    return true;
  }
  if (std::find(onPath.begin(), onPath.end(), canonical) != onPath.end()) {
    failLine("dependency cycle involving '" + canonical + "'");
    note("packages cannot depend on each other in a loop");
    return false;
  }

  Manifest m;
  std::string error;
  if (!loadManifest(canonical, m, error)) {
    failLine(error);
    return false;
  }

  onPath.push_back(canonical);
  std::vector<size_t> deps;
  for (const Dependency &d : m.Dependencies) {
    fs::path depDir;
    if (d.Path.empty()) {
      // A version names a package from a registry: installed once under
      // `~/.rune/registry/`, pinned by the root project's Rune.lock, and fetched
      // now if it is not there yet.
      depDir = pm::resolveRegistryDependency(m, d, gVerboseBuild);
      if (depDir.empty())
        return false;
    } else {
      depDir = fs::absolute(fs::path(m.Root) / d.Path).lexically_normal();
    }
    std::error_code ec;
    if (!fs::exists(depDir / "Rune.toml", ec)) {
      failLine("dependency '" + d.Name + "' has no package at " +
               depDir.string());
      note("check the `path` in " + (fs::path(m.Root) / "Rune.toml").string());
      return false;
    }
    size_t child = 0;
    if (!resolvePackages(depDir.string(), out, index, onPath, child))
      return false;
    deps.push_back(child);
  }
  onPath.pop_back();

  PackageNode node;
  node.Dir = canonical;
  node.M = std::move(m);
  node.Deps = std::move(deps);
  node.Config = node.M.Config;
  out.push_back(std::move(node));
  result = out.size() - 1;
  index[canonical] = result;
  return true;
}

/// Writes each package's dependents' `config = { ... }` choices over its own
/// `[config]` defaults.
///
/// A package is built once for the whole graph, so two dependents choosing
/// differently for one key have asked for two different builds of it — which
/// is a question only they can settle.
bool applyDependencyConfig(std::vector<PackageNode> &nodes) {
  bool ok = true;
  for (PackageNode &node : nodes) {
    for (size_t depIndex : node.Deps) {
      PackageNode &dep = nodes[depIndex];
      for (const Dependency &d : node.M.Dependencies) {
        if (d.Name != dep.M.Name || d.Config.empty())
          continue;
        for (const auto &kv : d.Config) {
          if (!dep.M.Config.count(kv.first)) {
            failLine("'" + dep.M.Name + "' has no config key '" + kv.first +
                     "'");
            note("'" + node.M.Name + "' chooses it in its `[dependencies]` "
                 "entry; the keys a package offers are the ones in its own "
                 "`[config]` table");
            ok = false;
            continue;
          }
          auto had = dep.Config.find(kv.first);
          auto by = dep.ConfigChosenBy.find(kv.first);
          if (by != dep.ConfigChosenBy.end() && had != dep.Config.end() &&
              had->second != kv.second) {
            failLine("'" + dep.M.Name + "' is asked for two builds at once");
            note("'" + by->second + "' chose " + kv.first + " = " +
                 had->second + ", and '" + node.M.Name + "' chose " +
                 kv.first + " = " + kv.second);
            note("a package is built once for the whole graph; settle on one "
                 "value, or depend on it through one of them");
            ok = false;
            continue;
          }
          dep.Config[kv.first] = kv.second;
          dep.ConfigChosenBy[kv.first] = node.M.Name;
        }
      }
    }
  }
  return ok;
}

/// Stages a package's dependencies' libraries where its compiles can find
/// them, and works out what it will have to link against.
///
/// Every library in the subtree is staged, not just the direct ones: the
/// compiler resolves imports from a single search path, and a public API may
/// well mention types from a dependency's own dependency.
bool prepareInputs(PackageNode &node, std::vector<PackageNode> &nodes,
                   const fs::path &depsDir, const Options &opts) {
  std::error_code ec;
  fs::create_directories(depsDir, ec);

  for (size_t depIndex : node.Deps) {
    PackageNode &dep = nodes[depIndex];
    if (dep.Result.LibraryPath.empty()) {
      failLine("dependency '" + dep.M.Name + "' does not build a library");
      note("give it a src/lib.rune, or depend on it only for its binaries");
      return false;
    }

    std::vector<std::string> toStage = dep.Inputs.Libraries;
    toStage.push_back(dep.Result.LibraryPath);

    DependencyInputs direct;
    for (const std::string &src : toStage) {
      fs::path staged = depsDir / fs::path(src).filename();
      if (fs::equivalent(fs::path(src), staged, ec)) {
        direct.Libraries.push_back(staged.string());
        continue;
      }
      // Copying is skipped when the staged copy is already current, which is
      // what keeps a rebuild with no changes quiet.
      bool needsCopy = true;
      if (fs::exists(staged, ec))
        needsCopy = fs::last_write_time(src, ec) > fs::last_write_time(staged, ec);
      if (needsCopy) {
        fs::copy_file(src, staged, fs::copy_options::overwrite_existing, ec);
        if (ec) {
          failLine("cannot stage '" + src + "': " + ec.message());
          return false;
        }
      }
      direct.Libraries.push_back(staged.string());
    }
    direct.LinkLibs = dep.Inputs.LinkLibs;
    direct.LinkPaths = dep.Inputs.LinkPaths;
    node.Inputs.merge(direct);
  }

  // Whatever this package needs natively, its dependents need too. A cross
  // target's own libraries go in alongside them: `ws2_32` is needed by the
  // build, not by the package.
  // A package's C or C++ half is compiled before anything links, and travels
  // with the package: a dependent linking against this library needs those objects
  // too, because the symbols they define are the ones it calls.
  std::vector<std::string> cObjects;
  const fs::path target = buildDir(node.M, opts);
  if (!buildNativeSources(node.M, opts, target / "c",
                          pm::FingerprintStore(target), cObjects))
    return false;

  DependencyInputs own;
  own.LinkLibs = node.M.LinkLibraries;
  own.LinkPaths = node.M.LinkPaths;
  own.Objects = cObjects;
  own.NeedsCxx = !node.M.CxxSources.empty();
  for (const std::string &a : node.M.LinkArgs)
    own.LinkArgs.push_back(a);
  // Spelled for whatever links: `-T script` to a linker, `-Wl,-T,script` to
  // a compiler driver.
  if (!node.M.LinkerScript.empty())
    for (const std::string &a :
         linkerScriptArgs(opts.Target, node.M.LinkerScript))
      own.LinkArgs.push_back(a);
  for (const std::string &l : opts.Target.LinkLibraries)
    own.LinkLibs.push_back(l);
  for (const std::string &p : opts.Target.LinkPaths)
    own.LinkPaths.push_back(p);
  node.Inputs.merge(own);
  return true;
}

/// The first half of a package's build: what its dependents need from it.
///
/// A dependent imports this package's `.rul` and nothing else, so its own
/// compiles can start as soon as this step is done — its binaries need not
/// wait, and neither need anything else in the graph.
/// Compiles the package's `build.rune`, if it has one, for this machine.
/// Whatever the package is built for, the script runs here, so it gets none
/// of the target's settings — only the host's defaults.
bool compileBuildScript(PackageNode &node, const Options &opts) {
  const Manifest &m = node.M;
  const fs::path script = fs::path(m.Root) / "build.rune";
  std::error_code ec;
  if (!fs::exists(script, ec))
    return true;
  const fs::path dir = fs::path(m.Root) / "target" / "build-script";
  fs::create_directories(dir, ec);
  const fs::path exe = dir / "build";
  pm::FingerprintStore stamps(dir);
  std::string cmd = quote(findCompiler()) + " --module build_script -o " +
                    quote(exe.string()) + " " + quote(script.string());
  pm::Fingerprint fp = stepFingerprint(cmd, {script.string()});
  if (!stamps.isFresh(exe, fp)) {
    if (!runStep("Compiling", m.Name + " v" + m.Version + " (build script)",
                 cmd, opts))
      return false;
    stamps.record(exe, fp);
  }
  node.ScriptExe = exe.string();
  return true;
}

/// What a build script is told, as `NAME=value ` assignments for a shell.
std::string buildScriptEnv(const PackageNode &node, const Options &opts,
                           const std::string &phase, const std::string &artifact,
                           const std::string &artifactName) {
  const Manifest &m = node.M;
  const fs::path root = fs::absolute(m.Root).lexically_normal();
  std::string env;
  auto set = [&](const char *name, const std::string &value) {
    env += std::string(name) + "=" + quote(value) + " ";
  };
  set("RUNE_BUILD_PHASE", phase);
  set("RUNE_PACKAGE_NAME", m.Name);
  set("RUNE_PACKAGE_VERSION", m.Version);
  set("RUNE_PACKAGE_DIR", root.string());
  set("RUNE_OUT_DIR", fs::absolute(buildDir(m, opts)).lexically_normal().string());
  set("RUNE_PROFILE", profileName(opts));
  set("RUNE_TARGET", opts.Target.Active ? opts.Target.Name : "host");
  set("RUNE_TARGET_TRIPLE", opts.Target.Active ? opts.Target.Triple : "");
  set("RUNE_FREESTANDING", opts.Freestanding ? "1" : "0");
  set("RUNE_CC", opts.Target.Active && !opts.Target.Cc.empty()
                     ? opts.Target.Cc
                     : std::string("cc"));
  set("RUNE_LINKER", !opts.Target.Linker.empty() &&
                         opts.Target.Linker != "build-script"
                     ? opts.Target.Linker
                     : (opts.Target.Active && !opts.Target.Cc.empty()
                            ? opts.Target.Cc
                            : std::string("cc")));
  set("RUNE_LINKER_KIND", linkerKindOf(opts.Target));
  if (!artifact.empty()) {
    set("RUNE_ARTIFACT", artifact);
    set("RUNE_ARTIFACT_NAME", artifactName);
  }
  return env;
}

/// Runs the build script in one of its phases, and reads its answers.
bool runBuildScript(const PackageNode &node, const Options &opts,
                    const std::string &phase, const std::string &artifact,
                    const std::string &artifactName, ScriptAnswers &out) {
  const Manifest &m = node.M;
  const fs::path root = fs::absolute(m.Root).lexically_normal();
  const std::string env = buildScriptEnv(node, opts, phase, artifact, artifactName);
  std::string cmd = "cd " + quote(root.string()) + " && " + env +
                    quote(node.ScriptExe);
  const std::string what = m.Name + " v" + m.Version + " (build script, " +
                           phase + (artifactName.empty() ? "" : " " + artifactName) + ")";
  if (opts.Verbose)
    pm::writeSerialized(std::string("  ") + cmd + "\n");

  std::string output;
  int rc = pm::runCaptured(cmd, output);

  // Its answers, and whatever else it had to say.
  std::string said, error;
  std::vector<std::string> warnings;
  std::istringstream lines(output);
  for (std::string line; std::getline(lines, line);) {
    // `rune:key=value`, the key in lower case: anything else is the
    // script's own output — including the runtime's `rune: warning: ...`.
    const size_t eq = line.find('=');
    bool answer = line.rfind("rune:", 0) == 0 && eq != std::string::npos &&
                  eq > 5;
    for (size_t i = 5; answer && i < eq; ++i)
      answer = (line[i] >= 'a' && line[i] <= 'z') || line[i] == '-';
    if (!answer) {
      said += line + "\n";
      continue;
    }
    const std::string key = line.substr(5, eq - 5);
    const std::string value = line.substr(eq + 1);
    auto anchored = [&](const std::string &p) {
      fs::path path(p);
      return (path.is_relative() ? root / path : path).lexically_normal().string();
    };
    if (key == "cfg")
      out.Cfgs.push_back(value);
    else if (key == "link-arg")
      out.LinkArgs.push_back(value);
    else if (key == "link-lib")
      out.LinkLibs.push_back(value);
    else if (key == "link-path")
      out.LinkPaths.push_back(anchored(value));
    else if (key == "run")
      out.RunWith = anchored(value);
    else if (key == "warning")
      warnings.push_back(value);
    else if (key == "error")
      error = value;
    else
      warnings.push_back("unknown build script answer `" + line + "`");
  }
  for (const std::string &w : warnings)
    pm::writeSerialized(std::string(c("\x1b[33m")) + "warning" + c("\x1b[0m") +
                        ": " + m.Name + ": " + w + "\n");
  if (rc != 0) {
    std::string text = std::string(c("\x1b[31m")) + "●" + c("\x1b[0m") +
                       " the build script of " + m.Name + " failed in its " +
                       phase + " phase (exit " + std::to_string(rc) + ")" +
                       (error.empty() ? "" : ": " + error) + "\n" + said;
    pm::writeSerialized(text);
    return false;
  }
  if (opts.Verbose && !said.empty())
    pm::writeSerialized(what + ":\n" + said);
  return true;
}

bool buildPackageLibrary(PackageNode &node, std::vector<PackageNode> &nodes,
                         const Options &opts) {
  const Manifest &m = node.M;
  fs::path target = buildDir(m, opts);
  fs::path depsDir = target / "deps";
  std::error_code ec;
  fs::create_directories(depsDir, ec);
  pm::FingerprintStore stamps(target);

  if (!prepareInputs(node, nodes, depsDir, opts))
    return false;

  // The build script first: what it answers is part of how everything in
  // the package is compiled.
  if (!compileBuildScript(node, opts))
    return false;
  if (!node.ScriptExe.empty() &&
      !runBuildScript(node, opts, "prepare", "", "", node.Script))
    return false;

  if (m.producesLibrary()) {
    // The library is its own root plus the package's components — the files
    // that declare no output of their own. A file that declares one is a
    // target in its own right and is not folded in here.
    std::vector<std::string> sources = m.componentSources();
    sources.push_back(m.LibraryRoot);

    fs::path libOut = target / (m.Name + outputKindSuffix(OutputKind::Library));

    std::string cmd = quote(findCompiler()) +
                      emitFlagFor(OutputKind::Library) + " --module " +
                      quote(m.Name);
    appendBuildFlags(cmd, m, opts, node.Config);
    appendScriptConfig(cmd, node.Script);
    cmd += " -I " + quote(depsDir.string());
    cmd += " -o " + quote(libOut.string());
    for (const std::string &s : sources)
      cmd += " " + quote(s);

    std::vector<std::string> deps = sources;
    deps.push_back((fs::path(m.Root) / "Rune.toml").string());
    for (const std::string &lib : node.Inputs.Libraries)
      deps.push_back(lib);
    pm::Fingerprint fp = stepFingerprint(cmd, deps);

    // Up to date: keep the library that is already there and carry on. The
    // package's executables still have to be looked at — stopping here would
    // leave `rune run` with nothing to run on the second build.
    if (!stamps.isFresh(libOut, fp)) {
      if (!runStep("Compiling", m.Name + " v" + m.Version + " (library)", cmd,
                   opts))
        return false;
      stamps.record(libOut, fp);
    }
    node.Result.LibraryPath = libOut.string();
  }
  return true;
}

/// One thing a package produces after its library: an executable, or a file
/// that declared some other output. These have no bearing on one another, so
/// the graph runs as many of them at once as it has room for.
struct TargetStep {
  std::string Name;
  std::string Path;         ///< the root source file
  OutputKind Kind = OutputKind::Executable;
  bool IsLibraryRoot = false;  ///< only for `--emit`, where the library is
                               ///< re-emitted in the asked-for form
};

/// Everything a package produces once its library exists, in the order
/// `rune run` expects to find them.
///
/// Two entries that would write the same file are one target, not two. A
/// `[[bin]]` whose source also defines `main` is named both by the manifest
/// and by the layout scan, and while a build that compiled them one after the
/// other only wasted the second compile, one that runs them at the same time
/// would have both writing the same executable.
std::vector<TargetStep> targetsOf(const Manifest &m, const Options &opts) {
  std::vector<TargetStep> steps;
  auto add = [&](TargetStep step) {
    for (const TargetStep &have : steps)
      if (have.Name == step.Name && have.Kind == step.Kind)
        return;
    steps.push_back(std::move(step));
  };

  // `--emit <kind>`: every root this package owns is compiled to the asked-for
  // form instead of being linked. The package library is still archived as a
  // `.rul`, because that is what a binary of this package — and anything that
  // depends on it — imports; the emitted file is the same code in the form
  // that was asked for.
  if (opts.EmitSet && opts.Emit != OutputKind::Executable && !opts.CheckOnly) {
    if (m.producesLibrary() && opts.Emit != OutputKind::Library)
      add({m.Name, m.LibraryRoot, opts.Emit, true});
    if (!m.BinaryRoot.empty())
      add({m.producesLibrary() ? m.Name + "-bin" : m.Name, m.BinaryRoot,
           opts.Emit, false});
    for (const BinaryTarget &b : m.Binaries)
      add({b.Name, b.Path, opts.Emit, false});
    return steps;
  }

  // Every file that declares an executable becomes one, named after the file.
  // `src/main.rune` goes first, so `rune run` with no argument means it.
  std::vector<BinaryTarget> binaries = m.Binaries;
  for (const OutputRoot &r : m.Roots)
    if (r.Kind == OutputKind::Executable && r.Path != m.BinaryRoot)
      binaries.push_back(BinaryTarget{r.Name, r.Path});
  if (!m.BinaryRoot.empty())
    binaries.insert(binaries.begin(), BinaryTarget{m.Name, m.BinaryRoot});
  for (const BinaryTarget &b : binaries)
    add({b.Name, b.Path, OutputKind::Executable, false});

  // `@type(Library)` on a file other than lib.rune, `@type(Object)`,
  // `@type(Assembly)`, `@type(LLVM)`: each is compiled on its own, with the
  // package's components, into an output named after the file.
  for (const OutputRoot &r : m.Roots) {
    if (r.Kind == OutputKind::Executable)
      continue;
    if (r.Path == m.LibraryRoot)
      continue;                       // already built as the package library
    add({r.Name, r.Path, r.Kind, false});
  }
  return steps;
}

/// Builds one of them. `producedExe` receives the executable's path when the
/// step made one — a slot of the step's own, because a package's targets are
/// built at the same time and must not write to one list between them.
/// What one target step produced: the executable, and what its build
/// script said to run in its place.
struct Produced {
  std::string Exe;
  std::string RunWith;
};

bool buildTarget(const TargetStep &step, const PackageNode &node,
                 const Options &opts, Produced &produced) {
  std::string &producedExe = produced.Exe;
  const Manifest &m = node.M;
  const fs::path target = buildDir(m, opts);
  const fs::path depsDir = target / "deps";
  pm::FingerprintStore stamps(target);
  const DependencyInputs &inputs = node.Inputs;

  const bool emitting =
      opts.EmitSet && opts.Emit != OutputKind::Executable && !opts.CheckOnly;

  fs::path out;
  std::vector<std::string> sources;
  std::string moduleName;

  if (step.Kind == OutputKind::Executable && !emitting) {
    out = target / (step.Name + opts.Target.exeSuffix());
    // A binary gets a module name of its own, distinct from the package's.
    // The package name then belongs to the library alone, so a binary that
    // says `import <registry>` resolves outward to the real `.rul` instead of to
    // the module it is already inside. Components are re-imported per binary,
    // which is how it already worked.
    moduleName = m.producesLibrary()
                     ? m.Name + "__bin_" + fs::path(step.Path).stem().string()
                     : m.Name;
    sources.push_back(step.Path);
    // Components join the binary only when there is no library target;
    // otherwise they are already compiled into the .rul.
    if (!m.producesLibrary())
      for (const std::string &s : m.componentSources())
        if (s != step.Path)
          sources.push_back(s);
  } else if (emitting) {
    out = target / (step.Name + outputKindSuffix(step.Kind));
    moduleName = step.IsLibraryRoot
                     ? m.Name
                     : (m.producesLibrary()
                            ? m.Name + "__bin_" +
                                  fs::path(step.Path).stem().string()
                            : m.Name);
    // The library root carries the package's components; a binary of a
    // library package imports them through the `.rul` instead, exactly as
    // it does in an ordinary build.
    if (step.IsLibraryRoot || !m.producesLibrary())
      for (const std::string &cs : m.componentSources())
        if (cs != step.Path)
          sources.push_back(cs);
    sources.push_back(step.Path);
  } else {
    out = target / (step.Name + outputKindSuffix(step.Kind));
    moduleName = m.Name;
    sources = m.componentSources();
    sources.push_back(step.Path);
  }

  std::string cmd = quote(findCompiler()) + " --module " + quote(moduleName);
  if (opts.CheckOnly) {
    cmd += " --check";
  } else if (step.Kind != OutputKind::Executable) {
    cmd += emitFlagFor(step.Kind);
  }
  appendBuildFlags(cmd, m, opts, node.Config);
  appendScriptConfig(cmd, node.Script);
  cmd += " -I " + quote(depsDir.string());
  // Staged dependency libraries are already in depsDir; a library built by
  // this package sits one level up, so make that visible too. The library
  // root itself must not see it — it *is* that library, and importing it
  // would give every one of its types a second declaration.
  if (!step.IsLibraryRoot)
    cmd += " -I " + quote(target.string());
  if (step.Kind == OutputKind::Executable && !emitting) {
    // Native libraries every dependency asked for, in dependency order.
    for (const std::string &dir : inputs.LinkPaths)
      cmd += " -L" + quote(dir);
    for (const std::string &lib : inputs.LinkLibs)
      cmd += " -l" + lib;
    for (const std::string &obj : inputs.Objects)
      cmd += " --link-arg " + quote(obj);
    for (const std::string &a : inputs.LinkArgs)
      cmd += " --link-arg " + quote(a);
    if (inputs.NeedsCxx)
      cmd += " --link-cxx";
    appendScriptLink(cmd, node.Script);
    // `linker = "build-script"`: the package's own build.rune links, called
    // as a linker would be — the objects, `-o` and the flags as arguments —
    // in its `link` phase.
    if (opts.Target.Linker == "build-script") {
      if (node.ScriptExe.empty()) {
        failLine("[target." + opts.Target.Name + "] says `linker = "
                 "\"build-script\"`, and '" + m.Name + "' has no build.rune");
        return false;
      }
      cmd = buildScriptEnv(node, opts, "link", out.string(), step.Name) +
            cmd + " --linker " + quote(node.ScriptExe);
    }
  }
  if (!opts.CheckOnly)
    cmd += " -o " + quote(out.string());
  for (const std::string &src : sources)
    cmd += " " + quote(src);

  if (!opts.CheckOnly) {
    std::vector<std::string> deps = sources;
    deps.push_back((fs::path(m.Root) / "Rune.toml").string());
    for (const std::string &lib : inputs.Libraries)
      deps.push_back(lib);
    if (!node.Result.LibraryPath.empty())
      deps.push_back(node.Result.LibraryPath);
    // Read by the link or compiled in, though no source names them.
    if (!m.LinkerScript.empty())
      deps.push_back(m.LinkerScript);
    if (opts.Freestanding)
      deps.push_back((fs::path(RUNE_TOOLCHAIN_ROOT) / "runetime" /
                      "freestanding.rune").string());
    pm::Fingerprint fp = stepFingerprint(cmd, deps);
    const bool linked = step.Kind == OutputKind::Executable && !emitting;
    // After the link, the build script's finish phase — every build, since
    // what it makes is its own business and it may have been edited.
    auto finish = [&]() {
      if (!linked)
        return true;
      producedExe = out.string();
      if (node.ScriptExe.empty())
        return true;
      ScriptAnswers after;
      if (!runBuildScript(node, opts, "finish", out.string(), step.Name, after))
        return false;
      produced.RunWith = after.RunWith;
      return true;
    };
    if (stamps.isFresh(out, fp))
      return finish();
    std::string verb = "Compiling";
    std::string what = step.Name + " v" + m.Version;
    if (step.Kind != OutputKind::Executable)
      what += " (" + std::string(outputKindName(step.Kind)) + ")";
    if (!runStep(verb, what, cmd, opts))
      return false;
    stamps.record(out, fp);
    return finish();
  }

  std::string what = step.Name + " v" + m.Version;
  if (step.Kind != OutputKind::Executable)
    what += " (" + std::string(outputKindName(step.Kind)) + ")";
  return runStep("Checking", what, cmd, opts);
}

/// What a whole-workspace build produced, from the root package's point of
/// view: the same thing the recursive builder used to hand back, plus what
/// the root package links against, which `rune test` needs in order to build
/// a test program the same way.
struct WorkspaceResult {
  bool Ok = false;
  BuildResult Root;
  DependencyInputs RootInputs;
};

/// Builds the package at `opts.PackageDir` and everything under it.
///
/// The whole graph is turned into steps first and then run at once, rather
/// than walked: a package's library, its dependents' libraries and every
/// binary in the tree are ordered only by what actually needs what, so
/// anything that could be compiled at the same time is.
WorkspaceResult buildWorkspace(const Options &opts) {
  // The root's lock is what pins every registry dependency in the graph.
  pm::setLockProject(fs::absolute(opts.PackageDir).lexically_normal());
  gVerboseBuild = opts.Verbose;
  WorkspaceResult result;

  std::vector<PackageNode> nodes;
  std::map<std::string, size_t> index;
  std::vector<std::string> onPath;
  size_t root = 0;
  if (!resolvePackages(opts.PackageDir, nodes, index, onPath, root))
    return result;
  nodes[root].IsRoot = true;
  // What each package's dependents chose for its `[config]` keys, settled
  // before anything is compiled: the answer decides which declarations the
  // package even has.
  if (!applyDependencyConfig(nodes))
    return result;

  // A dependency is always built as a library, whatever `--emit` asked of the
  // root package: its `.rul` is what makes its dependents compile at all, and
  // an `.ll` in its place would leave nothing to import.
  Options depOpts = opts;
  depOpts.EmitSet = false;

  // One library step per package, then one step per thing that package
  // produces afterwards. `libraryJob[i]` is where a dependent's step points.
  std::vector<pm::Job> jobs;
  std::vector<size_t> libraryJob(nodes.size());
  std::vector<std::vector<TargetStep>> targets(nodes.size());

  for (size_t i = 0; i < nodes.size(); ++i) {
    const Options *use = nodes[i].IsRoot ? &opts : &depOpts;
    pm::Job job;
    for (size_t d : nodes[i].Deps)
      job.DependsOn.push_back(libraryJob[d]);
    job.Run = [i, use, &nodes] {
      return buildPackageLibrary(nodes[i], nodes, *use);
    };
    libraryJob[i] = jobs.size();
    jobs.push_back(std::move(job));
  }

  // A dependency's binaries are of no use to the package that depends on it,
  // but `rune build` on a workspace is expected to build everything in it,
  // and the recursive builder did.
  std::vector<std::vector<Produced>> producedExes(nodes.size());
  for (size_t i = 0; i < nodes.size(); ++i) {
    const Options *use = nodes[i].IsRoot ? &opts : &depOpts;
    targets[i] = targetsOf(nodes[i].M, *use);
    if (targets[i].empty() && use->EmitSet &&
        use->Emit != OutputKind::Executable && !use->CheckOnly &&
        !nodes[i].M.producesLibrary()) {
      failLine("nothing to emit: '" + nodes[i].M.Name +
               "' has no src/lib.rune, src/main.rune or [[bin]]");
      return result;
    }
    producedExes[i].resize(targets[i].size());
    for (size_t t = 0; t < targets[i].size(); ++t) {
      pm::Job job;
      job.DependsOn.push_back(libraryJob[i]);
      job.Run = [i, t, use, &nodes, &targets, &producedExes] {
        return buildTarget(targets[i][t], nodes[i], *use, producedExes[i][t]);
      };
      jobs.push_back(std::move(job));
    }
  }

  if (!pm::runGraph(jobs))
    return result;

  // Steps finish in whatever order the pool got to them; `rune run` with no
  // argument means the first executable the manifest names, so the list is
  // rebuilt here in the order the targets were worked out.
  BuildResult &rootResult = nodes[root].Result;
  for (size_t t = 0; t < targets[root].size(); ++t) {
    const Produced &p = producedExes[root][t];
    if (p.Exe.empty())
      continue;
    rootResult.Executables.push_back({targets[root][t].Name, p.Exe});
    if (!p.RunWith.empty())
      rootResult.RunWith[p.Exe] = p.RunWith;
  }
  if (!rootResult.Executables.empty())
    rootResult.ExecutablePath = rootResult.Executables.front().second;
  rootResult.Ok = true;

  result.Ok = true;
  result.Root = rootResult;
  result.RootInputs = nodes[root].Inputs;
  return result;
}

//===----------------------------------------------------------------------===//
// Scaffolding
//===----------------------------------------------------------------------===//

bool writeFile(const fs::path &path, const std::string &text) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream out(path);
  if (!out) {
    failLine("cannot write '" + path.string() + "'");
    return false;
  }
  out << text;
  return true;
}

int scaffold(const std::string &dir, const std::string &name, bool isLibrary) {
  std::error_code ec;
  fs::path root(dir);
  if (fs::exists(root / "Rune.toml", ec)) {
    failLine("'" + root.string() + "' already contains a Rune.toml");
    return 1;
  }

  if (!writeFile(root / "Rune.toml", defaultManifestText(name, isLibrary)))
    return 1;

  // `docs/` is part of the layout, so a package has an obvious place to put
  // its documentation. It starts empty on purpose — what goes in it is yours.
  fs::create_directories(root / "docs", ec);

  if (isLibrary) {
    std::string lib =
        "// " + name + " — library root.\n"
        "//\n"
        "// Only `pub` items are visible to packages that import this one.\n"
        "\n"
        "pub fn greeting() -> String {\n"
        "    \"hello from " + name + "\"\n"
        "}\n"
        "\n"
        "pub fn add(a: i64, b: i64) -> i64 {\n"
        "    a + b\n"
        "}\n";
    if (!writeFile(root / "src" / "lib.rune", lib))
      return 1;
    std::string test =
        "// A test file is an ordinary program: it makes checks and hands the\n"
        "// tally back from `main`. `rune test` builds and runs each one.\n"
        "import std::testing\n"
        "import " + name + "\n"
        "\n"
        "fn main() -> i64 {\n"
        "    testing::equal(\"add sums its arguments\", " + name + "::add(2, 2), 4)\n"
        "    testing::equal(\"greeting names the package\",\n"
        "                   " + name + "::greeting(), \"hello from " + name + "\")\n"
        "    testing::summary()\n"
        "}\n";
    if (!writeFile(root / "tests" / "basics.rune", test))
      return 1;
  } else {
    std::string main =
        "import std::io\n"
        "\n"
        "fn main() -> i64 {\n"
        "    io::println(\"Hello from " + name + "!\")\n"
        "    0\n"
        "}\n";
    if (!writeFile(root / "src" / "main.rune", main))
      return 1;
  }

  writeFile(root / ".gitignore", "target/\n");

  okLine("Created " + std::string(isLibrary ? "library" : "binary") +
         " package '" + name + "'");
  note("build it with `cd " + root.string() + " && rune build`");
  return 0;
}

//===----------------------------------------------------------------------===//
// Commands
//===----------------------------------------------------------------------===//

int commandBuild(const Options &opts) {
  WorkspaceResult w = buildWorkspace(opts);
  if (!w.Ok) {
    failLine("Build failed.");
    return 1;
  }
  okLine(std::string(opts.CheckOnly ? "Checked" : "Finished") + " " +
         profileName(opts) + " profile");
  return 0;
}

int commandRun(const Options &opts) {
  WorkspaceResult w = buildWorkspace(opts);
  if (!w.Ok) {
    failLine("Build failed.");
    return 1;
  }
  const BuildResult &r = w.Root;
  if (r.Executables.empty()) {
    failLine("this package has nothing to run");
    note("add src/main.rune, give a file a `main`, or mark one "
         "`@type(Executable)`");
    return 1;
  }

  if (!canRunHere(opts)) {
    failLine("built for " + opts.Target.Triple + ", which this machine "
             "cannot run");
    if (!opts.Target.WantedRunner.empty()) {
      const std::string &want = opts.Target.WantedRunner;
      note("install " + want.substr(0, want.find(' ')) +
           " to run it here, or copy it to a machine that can");
    } else {
      note("copy the executable to the target machine, or give [target." +
           opts.Target.Name + "] a `runner` that can start it here "
           "(wine, qemu-aarch64, wasmtime, `arch -x86_64`, ...)");
    }
    return 1;
  }

  auto runOne = [&](const std::string &exe) {
    // A build script may have made what is really run: a disk image, say.
    auto made = r.RunWith.find(exe);
    const std::string path = made != r.RunWith.end() ? made->second : exe;
    status("Running", path);
    std::string args;
    for (const std::string &a : opts.ProgramArgs)
      args += (args.empty() ? "" : " ") + quote(a);
    return runCommand(launchCommand(opts, path, args), opts.Verbose);
  };

  // `--all`: every executable the package builds, in build order, stopping at
  // the first that fails so the failure is the one you see.
  if (opts.RunAll) {
    for (const auto &e : r.Executables) {
      int rc = runOne(e.second);
      if (rc != 0) {
        failLine(e.first + " exited with status " + std::to_string(rc));
        return rc;
      }
    }
    return 0;
  }

  // A named target, or the package's own `main.rune` when none is named.
  if (!opts.RunTarget.empty()) {
    for (const auto &e : r.Executables)
      if (e.first == opts.RunTarget)
        return runOne(e.second);
    failLine("this package has no executable named '" + opts.RunTarget + "'");
    std::string names;
    for (const auto &e : r.Executables)
      names += (names.empty() ? "" : ", ") + e.first;
    note("it builds: " + names);
    return 1;
  }
  return runOne(r.ExecutablePath);
}

int commandTest(const Options &opts) {
  Manifest m;
  std::string error;
  if (!loadManifest(opts.PackageDir, m, error)) {
    failLine(error);
    return 1;
  }
  if (m.TestFiles.empty()) {
    okLine("No tests found in tests/");
    return 0;
  }

  WorkspaceResult w = buildWorkspace(opts);
  if (!w.Ok) {
    failLine("Build failed.");
    return 1;
  }
  const DependencyInputs &inputs = w.RootInputs;

  fs::path target = buildDir(m, opts);
  fs::path testDir = target / "tests";
  std::error_code ec;
  fs::create_directories(testDir, ec);
  std::string compiler = findCompiler();

  // Every test file is a program of its own that imports the package and
  // nothing else, so they have no bearing on one another: they are built
  // together, and one that has not changed since it last built is not built
  // again.
  pm::FingerprintStore stamps(target);
  std::vector<pm::Job> jobs;
  // Not `vector<bool>`: its elements share bytes, so two steps finishing at
  // once would be writing the same one.
  std::vector<char> buildable(m.TestFiles.size(), 0);
  std::vector<fs::path> exes(m.TestFiles.size());

  for (size_t i = 0; i < m.TestFiles.size(); ++i) {
    const std::string &testFile = m.TestFiles[i];
    std::string name = fs::path(testFile).stem().string();
    exes[i] = testDir / (name + opts.Target.exeSuffix());
    std::string cmd = quote(compiler) + " --module " + quote(name);
    appendBuildFlags(cmd, m, opts, m.Config);
    cmd += " -I " + quote((target / "deps").string());
    cmd += " -I " + quote(target.string());
    for (const std::string &dir2 : inputs.LinkPaths)
      cmd += " -L" + quote(dir2);
    for (const std::string &lib : inputs.LinkLibs)
      cmd += " -l" + lib;
    for (const std::string &obj : inputs.Objects)
      cmd += " --link-arg " + quote(obj);
    for (const std::string &a : inputs.LinkArgs)
      cmd += " --link-arg " + quote(a);
    if (inputs.NeedsCxx)
      cmd += " --link-cxx";
    cmd += " -o " + quote(exes[i].string()) + " " + quote(testFile);

    std::vector<std::string> deps{testFile,
                                  (fs::path(m.Root) / "Rune.toml").string()};
    for (const std::string &lib : inputs.Libraries)
      deps.push_back(lib);
    if (!w.Root.LibraryPath.empty())
      deps.push_back(w.Root.LibraryPath);
    pm::Fingerprint fp = stepFingerprint(cmd, deps);

    if (stamps.isFresh(exes[i], fp)) {
      buildable[i] = 1;
      continue;
    }
    pm::Job job;
    job.Run = [&, i, name, cmd, fp] {
      if (!runStep("Compiling", "test " + name, cmd, opts))
        return false;
      stamps.record(exes[i], fp);
      buildable[i] = 1;
      return true;
    };
    jobs.push_back(std::move(job));
  }

  // A test that will not compile is a failure to report, not a reason to stop
  // building the rest: `runGraph` would give up at the first one.
  for (pm::Job &job : jobs) {
    auto run = std::move(job.Run);
    job.Run = [run] { run(); return true; };
  }
  pm::runGraph(jobs);

  // Running them is sequential on purpose. A test prints as it goes, and the
  // point of the output is to be read in order; several at once would also
  // contend for whatever a test touches outside itself.
  unsigned passed = 0, failed = 0;
  for (size_t i = 0; i < m.TestFiles.size(); ++i) {
    std::string name = fs::path(m.TestFiles[i]).stem().string();
    if (!buildable[i]) {
      ++failed;
      failLine("test " + name + " failed to build");
      continue;
    }
    if (!canRunHere(opts)) {
      // Built, but for another machine. Reporting it as passed would be a
      // lie, and as failed would be worse.
      status("Built", "test " + name + " (not run: built for " +
                          opts.Target.Triple + ")");
      continue;
    }
    status("Running", "test " + name);
    int rc = runCommand(launchCommand(opts, exes[i].string()), opts.Verbose);
    if (rc == 0) {
      ++passed;
      okLine("test " + name + " ... ok");
    } else {
      ++failed;
      failLine("test " + name + " ... FAILED (exit " + std::to_string(rc) + ")");
    }
  }

  // Files, not checks: each file prints its own per-check tally as it runs.
  std::string summary = std::to_string(passed) + " file(s) passed, " +
                        std::to_string(failed) + " failed";
  if (failed) {
    failLine("Test result: " + summary);
    return 1;
  }
  okLine("Test result: " + summary);
  return 0;
}

/// `rune doc` — a documentation sidecar per target, then the generator.
///
/// The generator is `tools/rune-doc.rune`, a Rune program: the toolchain
/// documents itself with itself rather than with a script in another language.
/// Where the toolchain keeps what it needs across invocations: the doc
/// generator it builds, the stylesheet and script the generated pages use, and
/// a copy of the standard library. Everything here is derived from the
/// installation, so a package can be built from anywhere.
/// The newest thing in a tree, which is what "has this changed" means for a
/// directory: a directory's own timestamp only moves when its immediate
/// contents are added or removed, so editing a file two levels down leaves it
/// untouched — and a cache keyed on it would never notice.
fs::file_time_type newestUnder(const fs::path &root) {
  std::error_code ec;
  fs::file_time_type newest = fs::last_write_time(root, ec);
  if (!fs::is_directory(root, ec))
    return newest;
  for (fs::recursive_directory_iterator it(root, ec), end; it != end;
       it.increment(ec)) {
    if (ec)
      break;
    std::error_code fec;
    auto when = fs::last_write_time(it->path(), fec);
    if (!fec && when > newest)
      newest = when;
  }
  return newest;
}

/// Copies `from` to `to` when the destination is missing or older. Returns
/// false only when the source is not there to copy.
bool refresh(const fs::path &from, const fs::path &to, bool directory) {
  std::error_code ec;
  if (!fs::exists(from, ec))
    return false;
  if (fs::exists(to, ec)) {
    auto a = directory ? newestUnder(from) : fs::last_write_time(from, ec);
    auto b = directory ? newestUnder(to) : fs::last_write_time(to, ec);
    if (!ec && b >= a)
      return true;
  }
  fs::create_directories(to.parent_path(), ec);
  if (directory) {
    fs::remove_all(to, ec);
    fs::copy(from, to, fs::copy_options::recursive, ec);
  } else {
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
  }
  return !ec;
}

/// Fills the cache from the installation, building the doc generator if it is
/// not there yet. Called before anything that needs those files.
void primeToolchainCache(const Options &opts) {
  fs::path home = runeHome();
  fs::path src(RUNE_TOOLCHAIN_ROOT);
  std::error_code ec;
  fs::create_directories(home / "bin", ec);

  // The documentation interface: one stylesheet and one script, kept beside
  // the generator so a package built from anywhere gets the same pages.
  refresh(src / "tools" / "docui" / "style.css", home / "share" / "style.css", false);
  refresh(src / "tools" / "docui" / "app.js", home / "share" / "app.js", false);
  refresh(fs::path(RUNE_DEFAULT_STDLIB_DIR), home / "stdlib", true);

  // The generator is a Rune program, so it is compiled once and kept.
  fs::path gen = home / "bin" / "rune-doc";
  fs::path genSrc = src / "tools" / "rune-doc.rune";
  bool stale = !fs::exists(gen, ec);
  if (!stale && fs::exists(genSrc, ec)) {
    auto a = fs::last_write_time(genSrc, ec);
    auto b = fs::last_write_time(gen, ec);
    stale = !ec && a > b;
  }
  if (stale && fs::exists(genSrc, ec)) {
    status("Preparing", "rune-doc");
    std::string cmd = quote(findCompiler()) + " --stdlib " +
                      quote((home / "stdlib").string()) + " -o " +
                      quote(gen.string()) + " " + quote(genSrc.string());
    runCommand(cmd, opts.Verbose);
  }
}

/// Builds the Rune runtime for a cross target and caches it under
/// `~/.rune/runtime/<triple>/`.
///
/// The runtime is half C and half Rune, and both halves are target-specific:
/// the host's `libruneruntime.a` is the wrong architecture and the wrong
/// object format. Building it here means a cross build needs no more setup
/// than a working cross toolchain.
///
/// A target with no flags of the build's own (`ResolvedTarget::OwnRuntime`)
/// is different: its runtime is built in the project, in
/// `target/<name>/runtime/`, with the command lines it would be compiled with
/// and nothing else — `cc -c <c-flags> -I<runtime headers> -o <obj> <src>` —
/// and built again whenever those lines, the runtime's sources or the
/// toolchain change. A runtime cached for the same triple is never used for
/// it: that one was compiled with flags this target asked not to have.
///
/// Returns the directory holding the archive, or "" when it could not be
/// built.
std::string ensureRuntimeFor(const ResolvedTarget &t, const Options &opts,
                             const fs::path &projectRoot) {
  const bool own = t.OwnRuntime;
  fs::path dir = own ? projectRoot / "target" / t.Name / "runtime"
                     : runeHome() / "runtime" / t.Triple;
  fs::path archive = dir / "libruneruntime.a";
  fs::path root(RUNE_TOOLCHAIN_ROOT);
  fs::path cRoot = root / "runtime";
  fs::path coreSrc = root / "runetime" / "core.rune";

  std::vector<std::string> sources = {
      (cRoot / "src" / "rune_runtime.c").string(),
      (cRoot / "src" / "rune_task.c").string(),
      (cRoot / "src" / "rune_unicode_data.c").string()};

  std::error_code ec;
  // The command that compiles one C source of the runtime for this target.
  const std::string cc = t.Cc.empty() ? std::string("cc") : t.Cc;
  auto compileCommand = [&](const std::string &src, const fs::path &obj) {
    std::string cmd = quote(cc) + " -c";
    if (!own) {
      cmd += " -O2";
      if (wantsPic(t))
        cmd += " -fPIC";
      // A driver chosen for the target already knows its target; a generic
      // one has to be told, exactly as at link time.
      if (t.Cc.empty() && t.DefaultFlags)
        cmd += " --target=" + quote(t.Triple);
    }
    // Asked for by the table, in so many words.
    if (!t.Sysroot.empty())
      cmd += " --sysroot=" + quote(t.Sysroot);
    for (const std::string &f : t.CFlags)
      cmd += " " + quote(f);
    cmd += " -I" + quote((cRoot / "include").string());
    cmd += " -o " + quote(obj.string()) + " " + quote(src);
    return cmd;
  };

  // A runtime of the target's own is rebuilt when anything that went into
  // it changes, the command lines included: a new `c-flags` is a new runtime.
  pm::FingerprintStore stamps(dir);
  pm::Fingerprint ownFp;
  if (own) {
    for (const std::string &src : sources) {
      ownFp.add(compileCommand(src, dir / (fs::path(src).stem().string() + ".o")));
      ownFp.addFile(src);
    }
    ownFp.add(archiverFor(t));
    ownFp.addFile((cRoot / "include" / "rune_runtime.h").string());
    ownFp.addFile((cRoot / "src" / "rune_single_threaded.h").string());
    ownFp.addFile(coreSrc.string());
    ownFp.add(pm::toolchainFingerprint(findCompiler(), RUNE_DEFAULT_STDLIB_DIR).hex());
    if (stamps.isFresh(archive, ownFp))
      return dir.string();
  }

  bool stale = own || !fs::exists(archive, ec);
  if (!stale) {
    auto out = fs::last_write_time(archive, ec);
    for (const std::string &src : sources) {
      if (!fs::exists(src, ec)) continue;
      if (fs::last_write_time(src, ec) > out) { stale = true; break; }
    }
    if (!stale && fs::exists(coreSrc, ec) &&
        fs::last_write_time(coreSrc, ec) > out)
      stale = true;
    // The headers the C half is compiled with count too.
    for (const fs::path &h : {cRoot / "include" / "rune_runtime.h",
                              cRoot / "src" / "rune_single_threaded.h"})
      if (!stale && fs::exists(h, ec) && fs::last_write_time(h, ec) > out)
        stale = true;
  }
  if (!stale)
    return dir.string();

  for (const std::string &src : sources)
    if (!fs::exists(src, ec)) {
      failLine("cannot find the runtime source at '" + src + "'");
      note("point [target." + t.Name + "] `runtime-dir` at a prebuilt "
           "libruneruntime.a for " + t.Triple);
      return "";
    }

  status("Preparing", "runtime for " + t.Triple +
                         (own ? " (target/" + t.Name + "/runtime)" : ""));
  fs::create_directories(dir, ec);
  fs::remove(archive, ec);   // `ar rcs` adds to an archive that is there

  std::vector<std::string> objects;
  for (const std::string &src : sources) {
    fs::path obj = dir / (fs::path(src).stem().string() + ".o");
    std::string cmd = compileCommand(src, obj);
    if (runCommand(cmd, opts.Verbose) != 0) {
      failLine("could not compile the runtime for " + t.Triple);
      if (t.Cc.empty())
        note("'" + cc + "' is this machine's compiler and has no headers for "
             "that target; name a cross toolchain with `cc = \"...\"` in "
             "[target." + t.Name + "]");
      else
        note("check that '" + cc + "' is installed and can compile for " +
             t.Triple);
      note("the command was: " + cmd);
      return "";
    }
    objects.push_back(obj.string());
  }

  // The Rune half goes through our own compiler, which already cross-compiles.
  if (fs::exists(coreSrc, ec)) {
    fs::path obj = dir / "runetime_core.o";
    std::string cmd = quote(findCompiler()) + " --no-stdlib -O2 --target " +
                      quote(t.Triple) + " -c -o " + quote(obj.string()) + " " +
                      quote(coreSrc.string());
    if (runCommand(cmd, opts.Verbose) != 0) {
      failLine("could not compile the Rune half of the runtime for " +
               t.Triple);
      return "";
    }
    objects.push_back(obj.string());
  }

  std::string ar = archiverFor(t);
  std::string cmd = quote(ar) + " rcs " + quote(archive.string());
  for (const std::string &o : objects)
    cmd += " " + quote(o);
  if (runCommand(cmd, opts.Verbose) != 0) {
    failLine("could not archive the runtime with '" + ar + "'");
    note("name the archiver with `ar = \"...\"` in [target." + t.Name + "]");
    return "";
  }
  if (own)
    stamps.record(archive, ownFp);
  return dir.string();
}

/// `rune-lint`, `rune-fmt` or `rune-lsp`: the editor tools, which are Rune
/// programs.
///
/// A build tree has them beside the compiler, built by CMake from the same
/// sources as it; that one is used when it is there. Otherwise they are
/// compiled from `tools/` into `~/.rune/bin` and kept, and compiled again
/// whenever a source is newer than what is kept — the same arrangement as
/// the documentation generator. Returns "" when neither can be had.
std::string ensureEditorTool(const std::string &name, const Options &opts) {
  std::error_code ec;
  fs::path here = fs::path(findCompiler()).parent_path();
  for (const std::string &file : {name, name + ".exe"}) {
    fs::path p = here / file;
    if (fs::is_regular_file(p, ec))
      return p.string();
  }

  fs::path src(RUNE_TOOLCHAIN_ROOT);
  fs::path root = src / "tools" / (name + ".rune");
  if (!fs::exists(root, ec))
    return "";
  std::vector<fs::path> sources = {root};
  for (fs::directory_iterator it(src / "tools" / "runetools", ec), end; it != end;
       it.increment(ec)) {
    if (!ec && it->path().extension() == ".rune")
      sources.push_back(it->path());
  }
  std::sort(sources.begin() + 1, sources.end());

  fs::path bin = runeHome() / "bin" / name;
  bool stale = !fs::exists(bin, ec);
  if (!stale) {
    auto built = fs::last_write_time(bin, ec);
    for (const fs::path &p : sources)
      if (fs::last_write_time(p, ec) > built)
        stale = true;
  }
  if (stale) {
    fs::create_directories(bin.parent_path(), ec);
    status("Preparing", name);
    // Every file after the first is a module under `runetools`, which is
    // what the tools import: `runetools::syntax`, `runetools::lint`.
    std::string cmd = quote(findCompiler()) + " --stdlib " +
                      quote(RUNE_DEFAULT_STDLIB_DIR) +
                      " --module runetools -O2 -o " + quote(bin.string());
    for (const fs::path &p : sources)
      cmd += " " + quote(p.string());
    if (runCommand(cmd, opts.Verbose) != 0) {
      failLine("could not build " + name + " from " + root.string());
      return "";
    }
  }
  return bin.string();
}

/// `rune lint [args]` and `rune lsp [args]`: the editor tools, run with the
/// toolchain's own standard library and compilers, and handed whatever else
/// was on the command line. Their exit status is the command's.
int commandEditorTool(const std::string &command, const std::vector<std::string> &rest,
                      const Options &opts) {
  std::string name = command == "lsp" ? "rune-lsp" : command == "fmt" ? "rune-fmt" : "rune-lint";
  std::string tool = ensureEditorTool(name, opts);
  if (tool.empty()) {
    failLine("could not find or build " + name);
    note("build the toolchain with CMake, which puts it beside `runec`");
    return 2;
  }
  std::error_code ec;
  std::string cmd = quote(tool) + " --stdlib " + quote(RUNE_DEFAULT_STDLIB_DIR);
  if (command == "fmt")
    cmd += " --runec " + quote(findCompiler());   // it asks the compiler for types
  if (command == "lsp") {
    // The server runs the compiler itself: this one, and this `rune`.
    fs::path self = fs::path(gExecutableDir) / "rune";
    cmd += " --runec " + quote(findCompiler());
    if (fs::is_regular_file(self, ec))
      cmd += " --rune " + quote(fs::absolute(self, ec).lexically_normal().string());
  }
  for (const std::string &a : rest)
    cmd += " " + quote(a);
  if (!opts.PackageDir.empty() && opts.PackageDir != ".")
    cmd = "cd " + quote(opts.PackageDir) + " && " + cmd;
  // Not through `runStep`: the server talks over this process's own
  // standard input and output, and the linter's report is for the terminal.
  return runCommand(cmd, false);
}

/// The cached generator, falling back to one sitting beside the compiler.
std::string findDocGenerator() {
  std::error_code ec;
  fs::path cached = runeHome() / "bin" / "rune-doc";
  if (fs::exists(cached, ec))
    return cached.string();
  fs::path here = fs::path(findCompiler()).parent_path();
  for (const char *name : {"rune-doc", "rune-doc.exe"}) {
    fs::path p = here / name;
    if (fs::exists(p, ec))
      return p.string();
  }
  return "";
}

/// Shows `url` in whatever the desktop opens pages with. Nothing waits on
/// it: the browser is the user's from here.
void openInBrowser(const std::string &url, bool verbose) {
  if (gNoBrowser) {
    okLine("Documentation at " + url);
    return;
  }
  // `open`, `start` and most `xdg-open` handlers treat a `file://` URL as a
  // file to open, and the `#section` after it is not part of a file name:
  // it is dropped, and the page opens at the top. A one-line page beside
  // the target that jumps to the section keeps it — the browser follows
  // the refresh, fragment and all — whichever browser is the default.
  std::string target = url;
  size_t hash = url.find('#');
  if (hash != std::string::npos && url.rfind("file://", 0) == 0) {
    fs::path page(url.substr(7, hash - 7));
    std::string fragment = url.substr(hash);
    fs::path jump = page.parent_path() / "jump.html";
    auto attr = [](const std::string &text) {
      std::string out;
      for (char ch : text) {
        if (ch == '&') out += "&amp;";
        else if (ch == '"') out += "&quot;";
        else if (ch == '<') out += "&lt;";
        else out += ch;
      }
      return out;
    };
    std::string to = attr(page.filename().string() + fragment);
    std::ofstream out(jump);
    if (out) {
      // `replace`, so the jump leaves nothing behind in the history; the
      // refresh is for a browser with scripts off.
      out << "<!doctype html>\n<meta charset=\"utf-8\">\n"
          << "<title>Opening…</title>\n"
          << "<script>location.replace(\"" << to << "\")</script>\n"
          << "<noscript><meta http-equiv=\"refresh\" content=\"0; url=" << to
          << "\"></noscript>\n"
          << "<p>Opening <a href=\"" << to << "\">" << to << "</a>…</p>\n";
      out.close();
      target = "file://" + fs::absolute(jump).lexically_normal().string();
    }
  }
#if defined(__APPLE__)
  runCommand("open " + quote(target), verbose);
#elif defined(_WIN32)
  runCommand("cmd /c start \"\" " + quote(target), verbose);
#else
  runCommand("xdg-open " + quote(target) + " >/dev/null 2>&1", verbose);
#endif
}

/// `file://` for a path, with a fragment naming the section to land on.
std::string fileUrl(const fs::path &page, const std::string &fragment) {
  std::string url = "file://" + fs::absolute(page).lexically_normal().string();
  if (!fragment.empty())
    url += "#" + fragment;
  return url;
}

/// The section id `rune-doc` gives a module: its name, lower-cased, with
/// every run of anything else turned into one dash.
std::string moduleSectionId(const std::string &module) {
  std::string out = "m-";
  bool dash = true;
  for (char c : module) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      dash = false;
    } else if (!dash) {
      out += '-';
      dash = true;
    }
  }
  while (!out.empty() && out.back() == '-')
    out.pop_back();
  return out;
}

/// The standard library's reference, built once into the cache and rebuilt
/// when the library — its sources or the guide pages under `stdlib/docs/` —
/// or the generator has changed since. Returns the page, or empty.
///
/// The sources come from the cache's own copy of the library, which
/// `primeToolchainCache` keeps current, so this works from any directory and
/// does not need the toolchain's source tree once it has been primed.
fs::path ensureStdlibDocs(const Options &opts) {
  primeToolchainCache(opts);
  fs::path home = runeHome();
  fs::path stdlib = home / "stdlib";
  fs::path out = home / "docs" / "std";
  fs::path page = out / "index.html";
  std::error_code ec;
  if (!fs::exists(stdlib, ec)) {
    failLine("the standard library is not in the cache at " + stdlib.string());
    note("build the toolchain, or set RUNE_HOME to where it was installed");
    return {};
  }
  std::string gen = findDocGenerator();
  if (gen.empty()) {
    failLine("the documentation generator is not built");
    note("build it with `runec -o rune-doc tools/rune-doc.rune`");
    return {};
  }

  bool stale = !fs::exists(page, ec);
  if (!stale) {
    auto built = fs::last_write_time(page, ec);
    if (newestUnder(stdlib) > built)
      stale = true;
    std::error_code gec;
    auto genWhen = fs::last_write_time(gen, gec);
    if (!gec && genWhen > built)
      stale = true;
  }
  if (!stale)
    return page;

  fs::create_directories(out, ec);
  fs::path sidecar = out / "std.rdoc";
  status("Reading", "the standard library");
  std::string read = quote(findCompiler()) + " --stdlib " + quote(stdlib.string()) +
                     " --emit-docs --docs-stdlib -o " + quote(sidecar.string());
  if (runCommand(read, opts.Verbose) != 0) {
    failLine("could not read the standard library");
    return {};
  }
  // The guide: one page per module under `stdlib/docs/std/`, each rendered
  // on its module's page, plus whatever else is written there.
  fs::path guide = stdlib / "docs";
  fs::create_directories(guide, ec);
  status("Writing", page.string());
  std::string run = quote(gen) + " " + quote(sidecar.string()) + " " +
                    quote(guide.string()) + " " + quote(out.string());
  fs::path style = home / "share" / "style.css";
  fs::path app = home / "share" / "app.js";
  if (fs::exists(style, ec)) {
    run += " " + quote(style.string());
    if (fs::exists(app, ec))
      run += " " + quote(app.string());
  }
  if (runCommand(run, opts.Verbose) != 0)
    return {};
  return page;
}

/// Every module the standard library's sidecar documents, for the message
/// that says a name is not one of them.
std::vector<std::string> stdlibModulesIn(const fs::path &sidecar) {
  std::vector<std::string> names;
  std::ifstream in(sidecar);
  std::string line;
  bool inModule = false;
  while (std::getline(in, line)) {
    if (line == "kind module") { inModule = true; continue; }
    if (inModule && line.rfind("name ", 0) == 0)
      names.push_back(line.substr(5));
    if (line.empty())
      inModule = false;
  }
  return names;
}

/// `rune doc std::io`: the standard library's reference, opened at a module.
int commandDocStdlib(const Options &opts) {
  fs::path page = ensureStdlibDocs(opts);
  if (page.empty())
    return 1;
  std::string module = opts.DocModule;
  std::vector<std::string> known = stdlibModulesIn(page.parent_path() / "std.rdoc");
  std::string fragment;
  if (module != "std") {
    bool found = std::find(known.begin(), known.end(), module) != known.end();
    if (!found) {
      failLine("the standard library has no module '" + module + "'");
      std::string list;
      for (const std::string &n : known)
        list += (list.empty() ? "" : ", ") + n;
      note("it has: " + list);
      return 1;
    }
    fragment = moduleSectionId(module);
  }
  std::string url = fileUrl(page, fragment);
  okLine("Opening " + url);
  openInBrowser(url, opts.Verbose);
  return 0;
}

/// The books the toolchain ships about its own tools, by the name
/// `rune doc <name>` takes: the directory under `tools/books/` it is written
/// in. Each is prose only — a folder of Markdown, like a package's `docs/`
/// with no source beside it.
const char *toolBookDir(const std::string &name) {
  if (name == "lint") return "rune-lint";
  if (name == "lsp") return "rune-lsp";
  if (name == "fmt") return "rune-fmt";
  return nullptr;
}

/// `rune doc lint`, `rune doc lsp`: the linter's and the language server's
/// books. Copied into the cache with the rest of the toolchain, so this
/// works from any directory, and rebuilt only when a page has changed or the
/// generator has.
int commandDocBook(const Options &opts, const std::string &dirName) {
  primeToolchainCache(opts);
  fs::path home = runeHome();
  fs::path books = home / "share" / "books";
  refresh(fs::path(RUNE_TOOLCHAIN_ROOT) / "tools" / "books", books, true);
  fs::path src = books / dirName;
  fs::path out = home / "docs" / dirName;
  fs::path page = out / "index.html";
  std::error_code ec;
  if (!fs::is_directory(src, ec)) {
    failLine("the book is not in the cache at " + src.string());
    note("build the toolchain, or set RUNE_HOME to where it was installed");
    return 1;
  }
  std::string gen = findDocGenerator();
  if (gen.empty()) {
    failLine("the documentation generator is not built");
    note("build it with `runec -o rune-doc tools/rune-doc.rune`");
    return 1;
  }
  bool stale = !fs::exists(page, ec);
  if (!stale) {
    auto built = fs::last_write_time(page, ec);
    std::error_code gec;
    auto genWhen = fs::last_write_time(gen, gec);
    stale = newestUnder(src) > built || (!gec && genWhen > built);
  }
  if (stale) {
    fs::create_directories(out, ec);
    status("Writing", page.string());
    // `-` for the sidecar: there is no source to read, only the pages.
    std::string run = quote(gen) + " - " + quote(src.string()) + " " + quote(out.string());
    fs::path style = home / "share" / "style.css";
    fs::path app = home / "share" / "app.js";
    if (fs::exists(style, ec)) {
      run += " " + quote(style.string());
      if (fs::exists(app, ec))
        run += " " + quote(app.string());
    }
    if (runCommand(run, opts.Verbose) != 0)
      return 1;
  }
  std::string url = fileUrl(page, "");
  okLine("Opening " + url);
  openInBrowser(url, opts.Verbose);
  return 0;
}

int commandDoc(const Options &opts);

/// `rune doc geometry`, `rune doc work::geometry`: a package's documentation,
/// built from the copy this project uses, or the newest one installed, or —
/// when there is none — the newest release a registry has, fetched now.
int commandDocPackage(const Options &opts) {
  std::string registry, name;
  pm::splitQualified(opts.DocModule, registry, name);
  if (name.empty() || name.find('/') != std::string::npos ||
      name.find('@') != std::string::npos) {
    failLine("'" + opts.DocModule + "' is not a package name");
    note("`rune doc <package>` or `rune doc <registry>::<package>`; a version "
         "is not part of it — the one this project uses is read, or the newest");
    return 2;
  }
  std::string error;
  fs::path dir = pm::locatePackage(opts.PackageDir, name, registry, opts.Verbose, error);
  if (dir.empty()) {
    failLine(error);
    if (!registry.empty() || error.find("no registry") != std::string::npos)
      note("`rune search " + name + "` looks for it; `rune registry list` "
           "shows the registries");
    return 1;
  }
  Manifest m;
  if (!loadManifest(dir.string(), m, error, /*requireSources=*/false)) {
    failLine(error);
    return 1;
  }
  status("Documenting", m.Name + " v" + m.Version + " from " + dir.string());
  // Exactly `rune doc --open` run inside the package: it is built there,
  // its dependencies resolved through a lock of its own. What that pulls in
  // is referenced by nobody — the store is not a project — so it is
  // `rune remove`'s to reclaim, like the package itself when nothing uses it.
  pm::setRecordReferences(false);
  Options inner = opts;
  inner.PackageDir = dir.string();
  inner.DocModule.clear();
  inner.OpenDocs = true;
  return commandDoc(inner);
}

int commandDoc(const Options &opts) {
  if (opts.DocModule == "std" || opts.DocModule.rfind("std::", 0) == 0)
    return commandDocStdlib(opts);
  // The toolchain's own books come before packages: a package that happens
  // to be called `lint` is still `rune doc <registry>::lint`.
  if (const char *book = toolBookDir(opts.DocModule))
    return commandDocBook(opts, book);
  if (!opts.DocModule.empty())
    return commandDocPackage(opts);
  primeToolchainCache(opts);
  Manifest m;
  std::string error;
  // A package with nothing under `src/` still has documentation: `docs/` is
  // written by hand and is read exactly as it is.
  if (!loadManifest(opts.PackageDir, m, error, /*requireSources=*/false)) {
    failLine(error);
    return 1;
  }
  fs::path target = buildDir(m, opts);
  fs::path docsDir = fs::path(m.Root) / "docs";
  // Read from `docs/`, written to `target/<profile>/docs/`. Nothing generated
  // goes back into the directory the author writes, so `docs/` is only ever
  // read and `rune clean` takes the built page with everything else.
  fs::path docsOut = target / "docs";
  std::error_code ec;
  fs::create_directories(docsDir, ec);

  // A package may be nothing but prose. `docs/` is written by hand and read
  // as it is, so documentation for one is exactly the same job minus the
  // half that comes from the compiler — there is nothing to build, nothing
  // to read, and no reason to refuse.
  const bool hasSource = !m.Roots.empty() || !m.componentSources().empty();
  if (!hasSource) {
    std::string genOnly = findDocGenerator();
    if (genOnly.empty()) {
      failLine("the documentation generator is not built");
      note("build it with `runec -o rune-doc tools/rune-doc.rune`");
      return 1;
    }
    fs::create_directories(docsOut, ec);
    status("Writing", (docsOut / "index.html").string());
    std::string only = quote(genOnly) + " - " + quote(docsDir.string()) + " " +
                       quote(docsOut.string());
    std::error_code sec;
    fs::path styleOnly = runeHome() / "share" / "style.css";
    fs::path appOnly = runeHome() / "share" / "app.js";
    if (fs::exists(styleOnly, sec)) {
      only += " " + quote(styleOnly.string());
      if (fs::exists(appOnly, sec))
        only += " " + quote(appOnly.string());
    }
    if (runCommand(only, opts.Verbose) != 0)
      return 1;
    okLine("Documentation written to " + (docsOut / "index.html").string());
    if (opts.OpenDocs)
      openInBrowser(fileUrl(docsOut / "index.html", ""), opts.Verbose);
    return 0;
  }

  fs::create_directories(target, ec);

  // Build first: a file that imports the package's own library needs that
  // library to exist before it can be read for documentation.
  if (!buildWorkspace(opts).Ok) {
    failLine("Build failed.");
    return 1;
  }

  std::string compiler = findCompiler();
  fs::path sidecar = target / (m.Name + ".rdoc");
  std::string sidecarArg = sidecar.string();

  // One sidecar per output root, compiled the same way the build compiles it:
  // each root with the package's components alongside, under the same module
  // name the build would use. Reading every file in one go instead would make
  // a root that imports the package's own library import itself.
  std::vector<std::string> parts;
  std::vector<OutputRoot> roots = m.Roots;
  if (roots.empty()) {
    // Components only: read them together, since nothing declares an output.
    OutputRoot only;
    only.Name = m.Name;
    only.Kind = OutputKind::Library;
    roots.push_back(only);
  }

  status("Reading", m.Name + " v" + m.Version);
  for (const OutputRoot &r : roots) {
    std::string moduleName =
        (r.Kind == OutputKind::Executable && m.producesLibrary())
            ? m.Name + "__bin_" + fs::path(r.Path).stem().string()
            : m.Name;
    // Named after the module rather than the root: a package with both a
    // library and a binary has two roots whose `Name` is the package's, and
    // one part file between them meant the second overwrote the first.
    fs::path part = target / (moduleName + ".rdoc.part");

    // A binary of a package that also builds a library is compiled against
    // that library, exactly as the build compiles it: the components are
    // already inside the `.rul`, so passing them as sources as well would
    // offer every one of them twice. Everything else carries its components
    // and must not see the `.rul`, for the same reason in reverse.
    const bool againstLibrary =
        r.Kind == OutputKind::Executable && m.producesLibrary();

    std::string cmd = quote(compiler) + " --emit-docs --module " +
                      quote(moduleName);
    cmd += " --safety " + m.Safety;
    cmd += " --memory " + opts.Memory;
    cmd += " -I " + quote((target / "deps").string());
    if (againstLibrary)
      cmd += " -I " + quote(target.string());
    cmd += " -o " + quote(part.string());
    // The root goes first: the file the compiler treats as *the* package
    // module is chosen from the sources in order, and the build passes the
    // root first for the same reason.
    if (!r.Path.empty())
      cmd += " " + quote(r.Path);
    if (!againstLibrary)
      for (const std::string &src : m.componentSources())
        cmd += " " + quote(src);
    if (runCommand(cmd, opts.Verbose) != 0) {
      failLine("could not read " + r.Name);
      return 1;
    }
    parts.push_back(part.string());
  }

  // Merge: a record is a block of lines, so joining the parts is enough. A
  // declaration reachable from two roots is written twice and the second
  // write produces the same page, which is harmless.
  {
    std::ofstream all(sidecar);
    if (!all) {
      failLine("cannot write " + sidecar.string());
      return 1;
    }
    std::set<std::string> seen;
    for (const std::string &part : parts) {
      std::ifstream in(part);
      std::string block, line;
      auto flush = [&]() {
        if (block.empty())
          return;
        if (seen.insert(block).second)
          all << block << "\n";
        block.clear();
      };
      while (std::getline(in, line)) {
        if (line.empty()) { flush(); continue; }
        block += line + "\n";
      }
      flush();
      in.close();
      std::error_code rec;
      fs::remove(part, rec);
    }
  }

  std::string gen = findDocGenerator();
  if (gen.empty()) {
    failLine("the documentation generator is not built");
    note("build it with `runec -o rune-doc tools/rune-doc.rune`");
    return 1;
  }
  fs::create_directories(docsOut, ec);
  status("Writing", (docsOut / "index.html").string());
  // Both come from the cache, so where `rune doc` was invoked does not matter.
  std::string run = quote(gen) + " " + quote(sidecarArg) + " " +
                    quote(docsDir.string()) + " " + quote(docsOut.string());
  std::error_code tec;
  fs::path style = runeHome() / "share" / "style.css";
  fs::path app = runeHome() / "share" / "app.js";
  if (fs::exists(style, tec)) {
    run += " " + quote(style.string());
    if (fs::exists(app, tec))
      run += " " + quote(app.string());
  }
  if (runCommand(run, opts.Verbose) != 0)
    return 1;
  okLine("Documentation written to " + (docsOut / "index.html").string());
  if (opts.OpenDocs)
    openInBrowser(fileUrl(docsOut / "index.html", ""), opts.Verbose);
  return 0;
}

int commandClean(const Options &opts) {
  Manifest m;
  std::string error;
  if (!loadManifest(opts.PackageDir, m, error)) {
    failLine(error);
    return 1;
  }
  fs::path target = fs::path(m.Root) / "target";
  std::error_code ec;
  uintmax_t removed = fs::remove_all(target, ec);
  if (ec) {
    failLine("cannot remove '" + target.string() + "': " + ec.message());
    return 1;
  }
  okLine("Removed " + std::to_string(removed) + " file(s) from target/");
  return 0;
}

void printUsage(std::ostream &os) {
  os << R"(rune - the Rune package manager

USAGE
    rune <command> [options]

COMMANDS
    new <name> [--lib]   Create a package in a new directory
    init [--lib]         Create a package in the current directory
    build                Build the package and its dependencies
    run [-- args...]     Build, then run the package's executable
    test                 Build and run every program under tests/
    doc [--open]         Read docs/ and the source; write target/<profile>/docs
    doc std::<module>    Open the standard library's reference at a module
    doc lint | doc lsp | doc fmt
                         Open the book about the linter, the language server
                         or the formatter
    doc <package>        Open a package's documentation; <registry>::<package>
                         reads the copy from that registry (--no-open: only say
                         where the page is)
    check                Type-check without producing output
    lint [path...]       Look for likely mistakes and style problems; --fix applies fixes
    fmt [path...]        Lay source out and write in types and argument labels;
                         --check only reports
    lsp                  Run the language server, for an editor, over stdin and stdout
    clean                Delete the target/ directory
    targets              List what this machine can build for: the foreign
                         targets (wasm, windows, linux-arm64, ...) and the
                         package's own [target.<name>] tables

PACKAGES
    search <regex>       Find packages in the configured registries
    desc <name>          Describe a package: versions, authors, dependencies
    add <name>[@req]...  Depend on a package; install it and pin it in Rune.lock
                         `--config key=value` chooses one of its `[config]` keys
    remove [<name>...]   Drop a dependency; with no names, uninstall unused packages
    update [<name>...]   Move dependencies to the newest versions their requirements allow
    deps                 Print the dependency tree
    installed            List what is installed under ~/.rune/registry and who uses it
    registry init [dir] [--name N]
                         Make a package registry (static files, ready to serve)
    registry --serve [--port N] [--dir D]
    registry --addPackage <project> [--dir D]
    registry add <url> [--name N]
                         Use a registry from this machine, under its own name
                         or the alias --name gives it
    registry list      The registries this machine uses
    registry remove <name>
                         Stop using one

    A package is <name>, or <registry>::<name> to take it from one registry;
    search, desc, add, update, deps and installed take --registry <name> too.

OPTIONS
    --release            Optimise (-O2) and omit debug information
    --memory <mode>      zombie | arc: single ownership proven by the Zombie
                         borrow checker, or reference counting (default: the
                         manifest's [build] memory, else zombie)
    -j, --jobs <n>       Compile at most <n> things at once (default: cores)
    --cfg <name>         Set <name> for `@Config(...)`, on top of [build] cfg
    --cfg <key>=<value>  Give <key> a value, over what [config] says
    --target <name>      Build for another machine: a foreign target
                         (`--target wasm`), a [target.<name>] table, or a triple
    --emit <kind>        llvm-ir | asm | obj | lib | exe — what to produce
                         instead of linking (default exe)
    -C, --directory <d>  Operate on the package in <d> instead of .
    -v, --verbose        Print each command as it runs
    --no-color           Disable coloured output
    -h, --help           Print this message
    --version            Print the version

LAYOUT
    Rune.toml            Package manifest
    src/main.rune        Binary root      -> target/<profile>/<name>
    src/lib.rune         Library root     -> target/<profile>/<name>.rul
    [target.<name>]      A cross toolchain -> target/<name>/<profile>/
    tests/*.rune         One test program per file
    docs/                Hand-written prose; only ever read
    target/              Build output, the generated documentation included
)";
}

} // namespace

int main(int argc, char **argv) {
  gExecutableDir = executableDirOf(argv[0]);
  gColor = isatty(2);

  if (argc < 2) {
    printUsage(std::cout);
    return 2;
  }

  std::string command = argv[1];
  Options opts;
  std::string newName;
  bool wantLib = false;
  bool afterSeparator = false;
  bool memoryFromFlag = false;

  // The editor tools take flags of their own, which they are handed as
  // written; only `-C` is `rune`'s.
  if (command == "lint" || command == "lsp" || command == "fmt") {
    std::vector<std::string> rest;
    for (int i = 2; i < argc; ++i) {
      std::string a = argv[i];
      if ((a == "-C" || a == "--directory") && i + 1 < argc) { opts.PackageDir = argv[++i]; continue; }
      if (a == "--no-color") { gColor = false; continue; }
      if (a == "-v" || a == "--verbose") { opts.Verbose = true; continue; }
      rest.push_back(a);
    }
    return commandEditorTool(command, rest, opts);
  }

  // The package commands take their own flags — `--serve`, `--port`,
  // `--refresh` — so they see the arguments as written, less the few every
  // command shares.
  if (command == "registry" || command == "search" || command == "desc" ||
      command == "installed" || command == "add" || command == "remove" ||
      command == "update" || command == "deps") {
    std::vector<std::string> rest;
    for (int i = 2; i < argc; ++i) {
      std::string a = argv[i];
      if (a == "-v" || a == "--verbose") { opts.Verbose = true; continue; }
      if (a == "--no-color") { gColor = false; continue; }
      if ((a == "-C" || a == "--directory") && i + 1 < argc) { opts.PackageDir = argv[++i]; continue; }
      if ((a == "-j" || a == "--jobs") && i + 1 < argc) {
        int n = atoi(argv[++i]);
        if (n > 0) pm::setJobLimit(static_cast<unsigned>(n));
        continue;
      }
      if (a == "-h" || a == "--help") { printUsage(std::cout); return 0; }
      rest.push_back(a);
    }
    if (command == "registry") return pm::commandRegistry(rest, opts.Verbose);
    if (command == "search") return pm::commandSearch(rest, opts.Verbose);
    if (command == "desc") return pm::commandDesc(rest, opts.Verbose);
    if (command == "installed") return pm::commandInstalled(rest, opts.Verbose);
    if (command == "add") return pm::commandAdd(opts.PackageDir, rest, opts.Verbose);
    if (command == "remove") return pm::commandRemove(opts.PackageDir, rest, opts.Verbose);
    if (command == "update") return pm::commandUpdate(opts.PackageDir, rest, opts.Verbose);
    return pm::commandDeps(opts.PackageDir, rest, opts.Verbose);
  }

  for (int i = 2; i < argc; ++i) {
    std::string a = argv[i];
    if (afterSeparator) {
      opts.ProgramArgs.push_back(a);
      continue;
    }
    if (a == "--") { afterSeparator = true; continue; }
    if (a == "--release") { opts.Release = true; continue; }
    if (a == "--memory" && i + 1 < argc) {
      opts.Memory = argv[++i];
      memoryFromFlag = true;
      continue;
    }
    if (a == "-v" || a == "--verbose") { opts.Verbose = true; continue; }
    if (a == "--no-color") { gColor = false; continue; }
    if (a == "--diagnostic-format" && i + 1 < argc) {
      std::string v = argv[++i];
      if (v != "json" && v != "human" && v != "short") {
        std::cerr << "rune: --diagnostic-format takes 'human', 'json' or 'short'\n";
        return 2;
      }
      gJsonDiagnostics = v == "json";
      gShortDiagnostics = v == "short";
      if (v != "human") gColor = false;
      continue;
    }
    if (a == "--source" && i + 1 < argc) {
      gSourceOverrides.push_back(argv[++i]);
      continue;
    }
    if (a == "--lib") { wantLib = true; continue; }
    if (a == "--bin") { wantLib = false; continue; }
    if (a == "--all") { opts.RunAll = true; continue; }
    if (a == "--open") { opts.OpenDocs = true; continue; }
    if (a == "--no-open") { gNoBrowser = true; continue; }
    if (a == "--cfg" && i + 1 < argc) {
      {
        std::string v = argv[++i];
        size_t eq = v.find('=');
        if (eq == std::string::npos)
          opts.ConfigFlags.push_back(v);
        else
          opts.ConfigValues.push_back({v.substr(0, eq), v.substr(eq + 1)});
      }
      continue;
    }
    if ((a == "-j" || a == "--jobs") && i + 1 < argc) {
      int n = atoi(argv[++i]);
      if (n <= 0) {
        failLine("--jobs takes a count of 1 or more");
        return 2;
      }
      pm::setJobLimit(static_cast<unsigned>(n));
      continue;
    }
    if ((a == "-C" || a == "--directory") && i + 1 < argc) {
      opts.PackageDir = argv[++i];
      continue;
    }
    if (a == "--target" && i + 1 < argc) {
      opts.TargetName = argv[++i];
      continue;
    }
    if (a == "--emit" && i + 1 < argc) {
      std::string kind = argv[++i];
      if (!parseEmitKind(kind, opts.Emit)) {
        failLine("unknown output kind '" + kind + "'");
        note("one of llvm-ir, asm, obj, lib or exe");
        return 2;
      }
      opts.EmitSet = true;
      continue;
    }
    if (a == "-h" || a == "--help") { printUsage(std::cout); return 0; }
    if (a == "--version") {
      std::cout << "rune " << RUNE_VERSION_STRING << "\n";
      return 0;
    }
    if (!a.empty() && a[0] == '-') {
      failLine("unknown option '" + a + "'");
      note("run `rune --help` for the list of options");
      return 2;
    }
    // For `run`, the first bare word names which executable to run.
    if (command == "run" && opts.RunTarget.empty()) {
      opts.RunTarget = a;
      continue;
    }
    // For `doc`, a bare word asks for something other than this package:
    // `std` or `std::name` for the library's own reference, anything else
    // for a package — `geometry`, or `work::geometry` from one registry.
    if (command == "doc" && opts.DocModule.empty()) {
      opts.DocModule = a;
      continue;
    }
    if (newName.empty())
      newName = a;
  }

  if (command == "-h" || command == "--help") {
    printUsage(std::cout);
    return 0;
  }
  if (command == "--version") {
    std::cout << "rune " << RUNE_VERSION_STRING << "\n";
    return 0;
  }
  if (command == "new") {
    if (newName.empty()) {
      failLine("`rune new` needs a package name");
      note("for example: rune new hello");
      return 2;
    }
    return scaffold(newName, fs::path(newName).filename().string(), wantLib);
  }
  if (command == "init") {
    std::string name = newName.empty()
                           ? fs::absolute(opts.PackageDir).filename().string()
                           : newName;
    return scaffold(opts.PackageDir, name, wantLib);
  }
  // Run from inside a package — its `src/`, say — `rune` works on the package
  // around it: the nearest directory above with a Rune.toml, as Cargo does.
  if (opts.PackageDir == "." && !fs::exists("Rune.toml")) {
    std::error_code ec;
    for (fs::path dir = fs::current_path(ec).parent_path();
         !ec && !dir.empty(); dir = dir.parent_path()) {
      if (fs::exists(dir / "Rune.toml", ec)) {
        opts.PackageDir = dir.string();
        break;
      }
      if (dir == dir.root_path())
        break;
    }
  }
  // Resolve the target once, from the root package's manifest. A dependency
  // does not get to pick the toolchain: everything in one build is built for
  // one machine.
  if (command != "new" && command != "init" &&
      !(command == "doc" && !opts.DocModule.empty())) {
    Manifest root;
    std::string error;
    if (loadManifest(opts.PackageDir, root, error)) {
      if (command == "targets")
        return listTargets(&root);
      // `[build] target` is what this package builds for when nothing on the
      // command line says otherwise. `[build] emit` works the same way.
      if (opts.TargetName.empty())
        opts.TargetName = root.DefaultTarget;
      if (!memoryFromFlag)
        opts.Memory = root.Memory;
      if (opts.Memory != "arc" && opts.Memory != "zombie") {
        failLine("unknown `memory` mode: '" + opts.Memory + "'");
        note("one of arc or zombie");
        return 1;
      }
      if (!opts.EmitSet && !root.Emit.empty()) {
        if (!parseEmitKind(root.Emit, opts.Emit)) {
          failLine("unknown `emit` in Rune.toml: '" + root.Emit + "'");
          note("one of llvm-ir, asm, obj, lib or exe");
          return 1;
        }
        opts.EmitSet = true;
      }
      opts.Freestanding = root.Freestanding;
      if (!opts.TargetName.empty()) {
        TargetProblem problem;
        if (!resolveTarget(root, opts.TargetName, opts.Target, problem)) {
          failLine(problem.Message);
          for (const std::string &n : problem.Notes)
            note(n);
          return 1;
        }
        // Bare metal has no hosted runtime to build, whatever the package
        // said; a freestanding program has no use for one either.
        if (opts.Target.Freestanding)
          opts.Freestanding = true;
        // A cross build needs a runtime built for its target. One named in
        // the manifest is taken as given; otherwise it is built and cached.
        if (opts.Target.RuntimeDir.empty() && !opts.Freestanding) {
          opts.Target.RuntimeDir = ensureRuntimeFor(
              opts.Target, opts, fs::absolute(root.Root).lexically_normal());
          if (opts.Target.RuntimeDir.empty())
            return 1;
        }
      }
    } else if (command == "targets" &&
               !fs::exists(fs::path(opts.PackageDir) / "Rune.toml")) {
      // Outside a package there are still the foreign targets to show.
      return listTargets(nullptr);
    } else if (command == "targets" || !opts.TargetName.empty()) {
      failLine(error);
      return 1;
    }
  }

  if (command == "build") return commandBuild(opts);
  if (command == "check") { opts.CheckOnly = true; return commandBuild(opts); }
  if (command == "run") return commandRun(opts);
  if (command == "test") return commandTest(opts);
  if (command == "doc") return commandDoc(opts);
  if (command == "clean") return commandClean(opts);
  failLine("unknown command '" + command + "'");
  note("run `rune --help` for the list of commands");
  return 2;
}
