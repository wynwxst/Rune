//===- Targets.cpp - What a build is for ----------------------------------===//
#include "Targets.h"

#include "Console.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace rune {

using pm::c;
using pm::runeHome;

//===----------------------------------------------------------------------===//
// The foreign targets
//===----------------------------------------------------------------------===//

const std::vector<ForeignTarget> &foreignTargets() {
  static const std::vector<ForeignTarget> targets = [] {
    std::vector<ForeignTarget> all;

    ForeignTarget wasm;
    wasm.Name = "wasm";
    wasm.Aliases = {"wasi", "wasm32", "wasm32-wasi", "wasm32-wasip1"};
    wasm.Summary = "WebAssembly with WASI, built with the WASI SDK";
    wasm.Triple = "wasm32-wasip1";
    wasm.Toolchain = ToolchainKind::WasiSdk;
    // A program run here sees this directory and this environment, as a
    // native one would; WASI grants neither unless asked.
    wasm.Runners = {"wasmtime run -S inherit-env=y --dir=.",
                    "wasmer run --dir=.", "wasm3"};
    wasm.InstallHint = "download the WASI SDK from "
                       "https://github.com/WebAssembly/wasi-sdk/releases and "
                       "set WASI_SDK_PATH to where it was unpacked";
    all.push_back(wasm);

    // The same, with real threads: shared memory, atomics, and wasi-threads
    // to start them. Fewer runtimes can run it, so it is a target of its own
    // rather than the default.
    ForeignTarget threads = wasm;
    threads.Name = "wasm-threads";
    threads.Aliases = {"wasi-threads", "wasm32-wasi-threads",
                       "wasm32-wasip1-threads"};
    threads.Summary = "WebAssembly with WASI and threads, built with the WASI SDK";
    threads.Triple = "wasm32-wasip1-threads";
    threads.Runners = {
        "wasmtime run -W threads=y -S threads=y -S inherit-env=y --dir=."};
    threads.CFlags = {"-pthread"};
    all.push_back(threads);

    ForeignTarget windows;
    windows.Name = "windows";
    windows.Aliases = {"mingw", "win64", "x86_64-w64-mingw32",
                       "x86_64-pc-windows-gnu", "x86_64-w64-windows-gnu"};
    windows.Summary = "64-bit Windows, built with mingw-w64";
    windows.Triple = "x86_64-w64-mingw32";
    windows.ToolPrefix = "x86_64-w64-mingw32";
    windows.Runners = {"wine"};
    // The runtime speaks sockets; Windows keeps them in a library of their
    // own, which no Rune source ever names.
    windows.LinkLibraries = {"ws2_32"};
    windows.InstallHint = "install mingw-w64: `apt install gcc-mingw-w64-x86-64` "
                          "or `brew install mingw-w64`";
    all.push_back(windows);

    // A Linux target runs under qemu's user-mode emulation, which needs the
    // target's own libc — the one the cross toolchain installed.
    auto linuxTarget = [](const std::string &name, const std::string &alias,
                          const std::string &arch, const std::string &summary,
                          const std::string &package) {
      const std::string prefix = arch + "-linux-gnu";
      ForeignTarget t;
      t.Name = name;
      t.Aliases = {alias, prefix, arch + "-unknown-linux-gnu"};
      t.Summary = summary;
      t.Triple = prefix;
      t.ToolPrefix = prefix;
      t.Runners = {"qemu-" + arch + " -L /usr/" + prefix};
      t.InstallHint = "install the cross compiler: `apt install " + package +
                      "`, and `qemu-user` to run what it builds";
      return t;
    };
    all.push_back(linuxTarget("linux-arm64", "linux-aarch64", "aarch64",
                              "64-bit ARM Linux, built with GCC",
                              "gcc-aarch64-linux-gnu"));
    all.push_back(linuxTarget("linux-x64", "linux-x86_64", "x86_64",
                              "64-bit x86 Linux, built with GCC",
                              "gcc-x86-64-linux-gnu"));
    all.push_back(linuxTarget("linux-riscv64", "linux-riscv", "riscv64",
                              "64-bit RISC-V Linux, built with GCC",
                              "gcc-riscv64-linux-gnu"));
    return all;
  }();
  return targets;
}

const ForeignTarget *findForeignTarget(const std::string &name) {
  for (const ForeignTarget &t : foreignTargets()) {
    if (t.Name == name)
      return &t;
    for (const std::string &alias : t.Aliases)
      if (alias == name)
        return &t;
  }
  return nullptr;
}

//===----------------------------------------------------------------------===//
// Finding a toolchain
//===----------------------------------------------------------------------===//

std::string findOnPath(const std::string &program) {
  std::error_code ec;
  if (program.find('/') != std::string::npos)
    return fs::is_regular_file(program, ec) ? program : "";
  const char *path = getenv("PATH");
  if (!path)
    return "";
#ifdef _WIN32
  const char separator = ';';
  const std::vector<std::string> names = {program, program + ".exe"};
#else
  const char separator = ':';
  const std::vector<std::string> names = {program};
#endif
  std::string entries = path;
  size_t start = 0;
  while (start <= entries.size()) {
    size_t end = entries.find(separator, start);
    if (end == std::string::npos)
      end = entries.size();
    const fs::path dir = entries.substr(start, end - start);
    for (const std::string &name : names) {
      fs::path candidate = dir / name;
      if (!dir.empty() && fs::is_regular_file(candidate, ec))
        return candidate.string();
    }
    start = end + 1;
  }
  return "";
}

/// A runner is a command line; what has to exist is its first word.
static bool runnerAvailable(const std::string &runner) {
  return !findOnPath(runner.substr(0, runner.find(' '))).empty();
}

/// True when `dir` looks like an unpacked WASI SDK.
static bool isWasiSdk(const fs::path &dir) {
  std::error_code ec;
  return fs::is_regular_file(dir / "bin" / "clang", ec) &&
         fs::is_directory(dir / "share" / "wasi-sysroot", ec);
}

std::string findWasiSdk(const std::string &configured) {
  if (!configured.empty())
    return isWasiSdk(configured) ? configured : "";
  for (const char *env : {"WASI_SDK_PATH", "WASI_SDK"})
    if (const char *v = getenv(env))
      if (*v && isWasiSdk(v))
        return v;
  // Where its own instructions, and package managers, put it. A versioned
  // directory — `/opt/wasi-sdk-25.0-x86_64-linux` is what the tarball
  // unpacks to — counts too; the newest wins.
  std::vector<fs::path> places = {runeHome() / "toolchains" / "wasi-sdk",
                                  "/opt/wasi-sdk", "/usr/local/wasi-sdk",
                                  "/usr/local/opt/wasi-sdk/share/wasi-sdk",
                                  "/opt/homebrew/opt/wasi-sdk/share/wasi-sdk"};
  if (const char *home = getenv("HOME"))
    places.push_back(fs::path(home) / "wasi-sdk");
  for (const fs::path &p : places)
    if (isWasiSdk(p))
      return p.string();
  std::vector<std::string> versioned;
  std::error_code ec;
  for (const fs::path &parent : {fs::path("/opt"), runeHome() / "toolchains"}) {
    for (fs::directory_iterator it(parent, ec), end; !ec && it != end;
         it.increment(ec))
      if (it->path().filename().string().rfind("wasi-sdk-", 0) == 0 &&
          isWasiSdk(it->path()))
        versioned.push_back(it->path().string());
  }
  if (versioned.empty())
    return "";
  std::sort(versioned.begin(), versioned.end());
  return versioned.back();
}

std::string archiverFor(const ResolvedTarget &t) {
  if (!t.Ar.empty())
    return t.Ar;
  if (t.Cc.empty())
    return "ar";
  // A cross toolchain names its tools after its triple, so `<triple>-gcc`
  // sits beside `<triple>-ar`; deriving one from the other saves configuring
  // what is almost always implied.
  for (const char *suffix : {"-gcc", "-clang", "-cc"}) {
    size_t n = std::strlen(suffix);
    if (t.Cc.size() > n && t.Cc.compare(t.Cc.size() - n, n, suffix) == 0)
      return t.Cc.substr(0, t.Cc.size() - n) + "-ar";
  }
  return "ar";
}

std::string cxxDriverFor(const ResolvedTarget &t) {
  if (!t.Cxx.empty())
    return t.Cxx;
  if (t.Cc.empty())
    return "c++";
  // A cross toolchain ships the two together, and naming one is naming both:
  // `…-gcc` becomes `…-g++`, `clang` becomes `clang++`.
  static const std::pair<const char *, const char *> kPairs[] = {
      {"-gcc", "-g++"}, {"gcc", "g++"}, {"clang", "clang++"}, {"cc", "c++"}};
  for (const auto &pair : kPairs) {
    const size_t n = std::strlen(pair.first);
    if (t.Cc.size() >= n && t.Cc.compare(t.Cc.size() - n, n, pair.first) == 0)
      return t.Cc.substr(0, t.Cc.size() - n) + pair.second;
  }
  return t.Cc;
}

bool wantsPic(const ResolvedTarget &t) { return !t.isWasm(); }

//===----------------------------------------------------------------------===//
// Resolving a name
//===----------------------------------------------------------------------===//

/// How many single-character edits turn `a` into `b`, for "did you mean".
static size_t editDistance(const std::string &a, const std::string &b) {
  std::vector<size_t> row(b.size() + 1);
  for (size_t j = 0; j <= b.size(); ++j)
    row[j] = j;
  for (size_t i = 1; i <= a.size(); ++i) {
    size_t diagonal = row[0];
    row[0] = i;
    for (size_t j = 1; j <= b.size(); ++j) {
      size_t above = row[j];
      row[j] = std::min({row[j] + 1, row[j - 1] + 1,
                         diagonal + (a[i - 1] == b[j - 1] ? 0 : 1)});
      diagonal = above;
    }
  }
  return row[b.size()];
}

/// Every name `--target` would accept here.
static std::vector<std::string> knownNames(const Manifest &m) {
  std::vector<std::string> names;
  for (const TargetSpec &s : m.Targets)
    names.push_back(s.Name);
  for (const ForeignTarget &t : foreignTargets())
    names.push_back(t.Name);
  return names;
}

/// Fills `out` with what a foreign target knows, finding its toolchain.
/// `spec` is the package's table for it, when there is one: what it names
/// is taken as found, so a toolchain installed somewhere odd only has to be
/// pointed at.
static bool fillFromForeign(const ForeignTarget &f, const TargetSpec *spec,
                            ResolvedTarget &out, TargetProblem &problem) {
  out.Triple = f.Triple;
  out.CFlags = f.CFlags;
  out.LinkLibraries = f.LinkLibraries;
  const bool ccGiven = spec && !spec->Cc.empty();

  switch (f.Toolchain) {
  case ToolchainKind::WasiSdk: {
    const std::string configured = spec ? spec->Sdk : "";
    const std::string sdk = findWasiSdk(configured);
    if (sdk.empty() && !ccGiven) {
      if (!configured.empty()) {
        problem.Message = "no WASI SDK at '" + configured + "'";
        problem.Notes.push_back("`sdk` should name the directory the SDK "
                                "unpacked to, the one holding `bin/clang` and "
                                "`share/wasi-sysroot`");
      } else {
        problem.Message = "cannot find the WASI SDK, which " + f.Name +
                          " builds with";
        problem.Notes.push_back(f.InstallHint);
        problem.Notes.push_back("or say where it is: `sdk = \"...\"` in "
                                "[target." + f.Name + "]");
      }
      return false;
    }
    if (!sdk.empty()) {
      // The SDK's `<triple>-clang` is its clang with the target already
      // chosen, so nothing downstream has to be told the triple again.
      out.Cc = (fs::path(sdk) / "bin" / (f.Triple + "-clang")).string();
      out.Cxx = out.Cc + "++";
      out.Ar = (fs::path(sdk) / "bin" / "llvm-ar").string();
      out.Sysroot = (fs::path(sdk) / "share" / "wasi-sysroot").string();
    }
    break;
  }
  case ToolchainKind::GnuPrefix: {
    const std::string cc = f.ToolPrefix + "-gcc";
    if (!ccGiven && findOnPath(cc).empty()) {
      problem.Message = "cannot find '" + cc + "', which " + f.Name +
                        " builds with";
      problem.Notes.push_back(f.InstallHint);
      problem.Notes.push_back("or name another compiler: `cc = \"...\"` in "
                              "[target." + f.Name + "]");
      return false;
    }
    out.Cc = cc;
    break;
  }
  }

  if (!f.Runners.empty())
    out.WantedRunner = f.Runners.front();
  for (const std::string &runner : f.Runners)
    if (runnerAvailable(runner)) {
      out.Runner = runner;
      break;
    }
  return true;
}

/// Lays the package's own settings over whatever `out` already holds: a key
/// that is given wins, a key that is not leaves the foreign default alone.
static void overlay(const TargetSpec &spec, ResolvedTarget &out) {
  auto take = [](std::string &into, const std::string &from) {
    if (!from.empty())
      into = from;
  };
  take(out.Triple, spec.Triple);
  take(out.Cc, spec.Cc);
  take(out.Cxx, spec.Cxx);
  take(out.Ar, spec.Ar);
  take(out.Sysroot, spec.Sysroot);
  take(out.RuntimeDir, spec.RuntimeDir);
  take(out.Runner, spec.Runner);
  auto append = [](std::vector<std::string> &into,
                   const std::vector<std::string> &from) {
    into.insert(into.end(), from.begin(), from.end());
  };
  append(out.CFlags, spec.CFlags);
  append(out.LinkLibraries, spec.LinkLibraries);
  append(out.LinkPaths, spec.LinkPaths);
  append(out.LinkArgs, spec.LinkArgs);
}

bool resolveTarget(const Manifest &m, const std::string &name,
                   ResolvedTarget &out, TargetProblem &problem) {
  out = ResolvedTarget();
  out.Active = true;
  out.Name = name;

  const TargetSpec *spec = m.findTarget(name);
  // A table may stand on a foreign target: named for one, or saying which.
  const ForeignTarget *base = nullptr;
  if (spec && !spec->Base.empty()) {
    base = findForeignTarget(spec->Base);
    if (!base) {
      problem.Message = "[target." + name + "] has `base = \"" + spec->Base +
                        "\"`, which is not a foreign target";
      std::string list;
      for (const ForeignTarget &t : foreignTargets())
        list += (list.empty() ? "" : ", ") + t.Name;
      problem.Notes.push_back("the foreign targets are " + list);
      return false;
    }
  } else if (!spec || spec->Triple.empty()) {
    base = findForeignTarget(name);
    // `--target wasi` builds into `target/wasm/`, whichever spelling asked.
    if (base && !spec)
      out.Name = base->Name;
  }

  if (base && !fillFromForeign(*base, spec, out, problem))
    return false;
  if (spec) {
    overlay(*spec, out);
    return true;
  }
  if (base)
    return true;

  // A bare triple has at least two dashes; a mistyped name usually has none,
  // and saying which is which is more useful than either alone.
  if (name.find('-') != std::string::npos) {
    out.Triple = name;
    return true;
  }
  problem.Message = "no target named '" + name + "'";
  std::string best;
  size_t bestDistance = 3;
  for (const std::string &known : knownNames(m)) {
    size_t d = editDistance(name, known);
    if (d < bestDistance) {
      bestDistance = d;
      best = known;
    }
  }
  if (!best.empty())
    problem.Notes.push_back("did you mean '" + best + "'?");
  problem.Notes.push_back("`rune targets` lists every target this package "
                          "can build for; a target triple works too");
  return false;
}

//===----------------------------------------------------------------------===//
// `rune targets`
//===----------------------------------------------------------------------===//

/// One target's entry: its name and triple, what it is, and whether this
/// machine can build it and run the result.
static void printEntry(const std::string &name, const std::string &triple,
                       const std::string &summary, bool ok,
                       const ResolvedTarget &t, const TargetProblem &problem) {
  std::cout << "  " << c("\x1b[1m") << name << c("\x1b[0m");
  for (size_t i = name.size(); i < 16; ++i)
    std::cout << ' ';
  std::cout << c("\x1b[2m") << triple << c("\x1b[0m") << "\n";
  if (!summary.empty())
    std::cout << "      " << summary << "\n";
  if (!ok) {
    std::cout << "      " << c("\x1b[31m") << "✗ " << c("\x1b[0m")
              << problem.Message << "\n";
    for (const std::string &n : problem.Notes)
      std::cout << "        " << c("\x1b[2m") << n << c("\x1b[0m") << "\n";
    return;
  }
  std::cout << "      " << c("\x1b[32m") << "✓ " << c("\x1b[0m")
            << "builds with " << (t.Cc.empty() ? "cc" : t.Cc) << "\n";
  if (!t.Runner.empty())
    std::cout << "      " << c("\x1b[32m") << "✓ " << c("\x1b[0m")
              << "runs with " << t.Runner << "\n";
  else if (!t.WantedRunner.empty())
    std::cout << "      " << c("\x1b[33m") << "· " << c("\x1b[0m")
              << "cannot run here without "
              << t.WantedRunner.substr(0, t.WantedRunner.find(' ')) << "\n";
  else
    std::cout << "      " << c("\x1b[33m") << "· " << c("\x1b[0m")
              << "cannot run here; give it a `runner`\n";
}

int listTargets(const Manifest *m) {
  Manifest none;
  const Manifest &manifest = m ? *m : none;

  if (m && !m->Targets.empty()) {
    std::cout << c("\x1b[1m") << "In Rune.toml" << c("\x1b[0m") << "\n";
    for (const TargetSpec &s : m->Targets) {
      ResolvedTarget t;
      TargetProblem problem;
      bool ok = resolveTarget(manifest, s.Name, t, problem);
      std::string summary;
      if (!s.Base.empty())
        summary = "the foreign target " + s.Base + ", adjusted";
      else if (s.Triple.empty() && findForeignTarget(s.Name))
        summary = "the foreign target " + findForeignTarget(s.Name)->Name +
                  ", adjusted";
      printEntry(s.Name, ok ? t.Triple : s.Triple, summary, ok, t, problem);
    }
    std::cout << "\n";
  }

  std::cout << c("\x1b[1m") << "Foreign targets" << c("\x1b[0m")
            << c("\x1b[2m") << "  (built in; --target <name>)" << c("\x1b[0m")
            << "\n";
  for (const ForeignTarget &f : foreignTargets()) {
    // A package's own table by the same name is listed above, and is what
    // `--target` would use.
    if (manifest.findTarget(f.Name))
      continue;
    ResolvedTarget t;
    TargetProblem problem;
    bool ok = fillFromForeign(f, nullptr, t, problem);
    printEntry(f.Name, f.Triple, f.Summary, ok, t, problem);
  }
  return 0;
}

} // namespace rune
