//===- Compilation.cpp - Pipeline orchestration ----------------*- C++ -*-===//

#include "rune/Driver.h"

#include <fstream>

#include "rune/AST.h"
#include "rune/ASTWalk.h"
#include "rune/CodeGen.h"
#include "rune/Config.h"
#include "rune/Diagnostics.h"
#include "rune/Library.h"
#include "rune/Lexer.h"
#include "rune/Macro.h"
#include "rune/Parallel.h"
#include "rune/Parser.h"
#include "rune/Sema.h"
#include "rune/Zombie.h"
#include "rune/Source.h"
#include "rune/Type.h"

#include <llvm/IR/DiagnosticHandler.h>
#include <llvm/IR/DiagnosticInfo.h>
#include <llvm/IR/DiagnosticPrinter.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Host.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iomanip>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <cstdlib>
#include <cctype>
#include <filesystem>
#include <iostream>
#if defined(_WIN32)
#include <process.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern "C" char **environ;
#endif

namespace rune {

namespace {

void dumpTokens(const SourceManager &sm, const std::vector<Token> &toks) {
  for (const Token &t : toks) {
    PresumedLoc pl = sm.decode(t.loc());
    std::cout << (pl.isValid() ? pl.File->Name : "?") << ":" << pl.Line << ":"
              << pl.Column << "  " << tokenName(t.Kind);
    switch (t.Kind) {
    case Tok::Identifier:
      std::cout << " '" << t.Text << "'";
      break;
    case Tok::StringLiteral:
      std::cout << " \"" << t.Text << "\"";
      break;
    case Tok::CharLiteral:
      std::cout << " '" << t.Text << "' (U+" << std::hex << t.IntValue << std::dec
                << ")";
      break;
    case Tok::IntLiteral:
      std::cout << " " << t.IntValue;
      if (!t.Suffix.empty()) std::cout << " suffix=" << t.Suffix;
      break;
    case Tok::FloatLiteral:
      std::cout << " " << t.FloatValue;
      if (!t.Suffix.empty()) std::cout << " suffix=" << t.Suffix;
      break;
    default:
      break;
    }
    std::cout << "\n";
  }
}

/// Where the time went, for `--time`.
///
/// Wall clock rather than CPU time: a pass that spread itself over the cores
/// should read as the time it actually took, not as the sum of what every
/// thread spent. The report says how many threads were available so the two
/// are not confused.
class PhaseTimer {
public:
  explicit PhaseTimer(bool enabled) : Enabled(enabled) {}

  /// Runs `body`, recording how long it took under `name`. A name used twice
  /// accumulates, so a phase entered from more than one place reads as one
  /// line.
  template <typename Body> void phase(const char *name, Body body) {
    if (!Enabled) {
      body();
      return;
    }
    const auto start = Clock::now();
    body();
    const double ms = std::chrono::duration<double, std::milli>(
                          Clock::now() - start).count();
    for (auto &e : Entries)
      if (e.first == name) {
        e.second += ms;
        return;
      }
    Entries.push_back({name, ms});
  }

  /// Records a part of a phase already timed under another name, shown
  /// indented beneath it and left out of the total.
  void within(const char *name, double ms) {
    if (Enabled)
      Sub.push_back({name, ms});
  }

  void report(std::ostream &os) const {
    if (!Enabled)
      return;
    double total = 0;
    size_t width = 5;
    for (const auto &e : Entries) {
      total += e.second;
      width = std::max(width, e.first.size());
    }
    for (const auto &e : Sub)
      width = std::max(width, e.first.size() + 2);
    os << "\n  time report (" << parallelism() << " thread"
       << (parallelism() == 1 ? "" : "s") << " available)\n";
    auto line = [&](const std::string &name, double ms) {
      os << "    " << std::left << std::setw(static_cast<int>(width + 2)) << name
         << std::right << std::setw(9) << std::fixed << std::setprecision(1)
         << ms << " ms";
      if (total > 0)
        os << std::setw(7) << std::setprecision(1) << (100.0 * ms / total)
           << " %";
      os << "\n";
    };
    for (const auto &e : Entries) {
      line(e.first, e.second);
      if (e.first == "check")
        for (const auto &sub : Sub)
          line("  " + sub.first, sub.second);
    }
    os << "    " << std::string(width + 2 + 20, '-') << "\n";
    line("total", total);
    os.unsetf(std::ios::floatfield);
  }

private:
  using Clock = std::chrono::steady_clock;
  bool Enabled;
  std::vector<std::pair<std::string, double>> Entries;
  std::vector<std::pair<std::string, double>> Sub;
};

std::string stemOf(const std::string &path) {
  size_t slash = path.find_last_of("/\\");
  std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
  size_t dot = base.find_last_of('.');
  return dot == std::string::npos ? base : base.substr(0, dot);
}

/// Quotes a path for the shell so directories with spaces survive.
std::string shellQuote(const std::string &s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'')
      out += "'\\''";
    else
      out += c;
  }
  return out + "'";
}

/// The triple a build is for: the one asked for, or the host's.
llvm::Triple targetTripleOf(const CompilerOptions &opts) {
  return llvm::Triple(llvm::Triple::normalize(
      opts.TargetTriple.empty() ? llvm::sys::getDefaultTargetTriple()
                                : opts.TargetTriple));
}

/// The target's pointer width in bits — what `usize` and `isize` are.
///
/// The data layout is the authority: it is the same one the code generator
/// installs on the module, so the type system and the emitted code cannot
/// disagree about how wide a pointer-sized integer is. When no backend is
/// registered for the triple the compile is going to fail later anyway; the
/// triple's own idea of the width keeps type checking sensible until then.
unsigned targetPointerBits(const CompilerOptions &opts) {
  const llvm::Triple triple = targetTripleOf(opts);
  std::string err;
  if (std::unique_ptr<llvm::TargetMachine> tm = createTargetMachine(triple, err))
    return tm->createDataLayout().getPointerSizeInBits();
  return llvm::Triple::getArchPointerBitWidth(triple.getArch());
}

std::string defaultOutputName(const CompilerOptions &opts) {
  if (!opts.OutputPath.empty())
    return opts.OutputPath;
  std::string stem = stemOf(opts.Inputs.front());
  switch (opts.Output) {
  case OutputKind::Object: return stem + ".o";
  case OutputKind::LLVMIR: return stem + ".ll";
  case OutputKind::Assembly: return stem + ".s";
  case OutputKind::Library: return stem + ".rul";
  case OutputKind::Shared: {
    const llvm::Triple triple = targetTripleOf(opts);
    if (triple.isOSWindows())
      return stem + ".dll";
    if (triple.isOSDarwin())
      return "lib" + stem + ".dylib";
    return "lib" + stem + ".so";
  }
  case OutputKind::Docs: return stem + ".rdoc";
  // A PE image is only executable with the suffix, and a cross build is
  // usually copied to the machine it runs on rather than run in place. A
  // WebAssembly module is not executable at all without a runtime to load
  // it, and every one of them expects the suffix.
  default: {
    const llvm::Triple triple = targetTripleOf(opts);
    if (triple.isOSWindows())
      return stem + ".exe";
    if (triple.isWasm())
      return stem + ".wasm";
    return stem;
  }
  }
}

/// What the back end should spend on the machine code itself. Distinct from
/// the IR-level pipeline in `optimizeModule`: this one decides instruction
/// selection, scheduling and register allocation.
llvm::CodeGenOptLevel codeGenOptLevel(unsigned optLevel) {
  switch (optLevel) {
  case 0: return llvm::CodeGenOptLevel::None;
  case 1: return llvm::CodeGenOptLevel::Less;
  case 2: return llvm::CodeGenOptLevel::Default;
  default: return llvm::CodeGenOptLevel::Aggressive;
  }
}

/// Reports what the back end has to say at the Rune source it came from.
/// An `asm::` call carries its source location as its `!srcloc` cookie.
struct BackendDiagnostics : llvm::DiagnosticHandler {
  explicit BackendDiagnostics(DiagnosticEngine &diags) : Diags(diags) {}
  DiagnosticEngine &Diags;
  bool Failed = false;

  bool handleDiagnostics(const llvm::DiagnosticInfo &di) override {
    const llvm::DiagnosticSeverity severity = di.getSeverity();
    if (severity == llvm::DS_Remark || severity == llvm::DS_Note)
      return true;
    const bool error = severity == llvm::DS_Error;
    uint64_t cookie = 0;
    std::string message, line;
    if (auto *ia = llvm::dyn_cast<llvm::DiagnosticInfoInlineAsm>(&di)) {
      cookie = ia->getLocCookie();
      message = ia->getMsgStr().str();
    } else if (auto *sm = llvm::dyn_cast<llvm::DiagnosticInfoSrcMgr>(&di)) {
      cookie = sm->getLocCookie();
      message = sm->getSMDiag().getMessage().str();
      line = sm->getSMDiag().getLineContents().str();
    } else {
      std::string text;
      llvm::raw_string_ostream os(text);
      llvm::DiagnosticPrinterRawOStream printer(os);
      di.print(printer);
      message = os.str();
    }
    SourceRange where;
    if (cookie != 0 && cookie <= 0xFFFFFFFEull)
      where = SourceRange(SourceLoc(static_cast<uint32_t>(cookie)));
    if (error) {
      Failed = true;
      auto d = where.isValid()
                   ? Diags.error(where, "inline assembly: {}", message.c_str())
                   : Diags.error(SourceRange(), "the back end: {}",
                                 message.c_str());
      if (!line.empty())
        d.note("in the assembly line `{}`", line.c_str());
      if (message.find("constraint") != std::string::npos)
        d.note("registers are written LLVM's way, `{eax}` or `{st}`; GCC's "
               "letters `a`, `t` and `u` are read too");
      d.code(509);
    } else {
      Diags.warn(where, "{}{}", where.isValid() ? "inline assembly: " : "",
                 message.c_str());
    }
    return true;
  }
};

/// Lowers the module to a native object or assembly file.
bool writeMachineCode(llvm::Module &m, const std::string &path,
                      const CompilerOptions &opts, DiagnosticEngine &diags,
                      bool assembly) {
  std::string tripleStr = opts.TargetTriple.empty()
                              ? llvm::sys::getDefaultTargetTriple()
                              : opts.TargetTriple;
  // See CodeGen: an un-normalised alias parses as an unknown OS.
  llvm::Triple triple(llvm::Triple::normalize(tripleStr));
  // The back end runs its own optimisation pipeline, and left to itself it
  // picks the `-O2` one whatever `-O` asked for. A debug build then pays for
  // scheduling and register-allocation work it did not want, which is most of
  // what an unoptimised compile spends its time on.
  std::string err;
  std::unique_ptr<llvm::TargetMachine> tm =
      createTargetMachine(triple, err, codeGenOptLevel(opts.OptLevel));
  if (!tm) {
    diags.fatal("no backend for target '{}'", tripleStr).note(err.c_str());
    return false;
  }
  m.setDataLayout(tm->createDataLayout());
  m.setTargetTriple(triple);

  std::error_code ec;
  auto out = std::make_unique<llvm::raw_fd_ostream>(path, ec,
                                                    llvm::sys::fs::OF_None);
  if (ec) {
    diags.fatal("cannot write '{}'", path).note(ec.message().c_str());
    return false;
  }
  llvm::legacy::PassManager pm;
  auto kind = assembly ? llvm::CodeGenFileType::AssemblyFile
                       : llvm::CodeGenFileType::ObjectFile;
  if (tm->addPassesToEmitFile(pm, *out, nullptr, kind)) {
    diags.fatal("this target cannot emit {} files", assembly ? "assembly" : "object");
    return false;
  }
  // The back end's own errors — almost always inline assembly it cannot
  // assemble or registers it cannot find — come here rather than to stderr
  // bare, and are reported at the `asm::` call they came from.
  auto handler = std::make_unique<BackendDiagnostics>(diags);
  BackendDiagnostics *seen = handler.get();
  llvm::LLVMContext &ctx = m.getContext();
  std::unique_ptr<llvm::DiagnosticHandler> previous =
      ctx.getDiagnosticHandler();
  ctx.setDiagnosticHandler(std::move(handler));
  pm.run(m);
  // Read before the handler goes: putting the previous one back destroys it.
  const bool failed = seen->Failed;
  ctx.setDiagnosticHandler(std::move(previous));
  out->close();
  if (failed) {
    // Whatever was written is not an object anyone should link.
    std::filesystem::remove(path, ec);
    return false;
  }
  return true;
}

/// Links the object file into an executable using the system C toolchain,
/// which already knows how to find libc and the platform's startup files.
/// Runs `argv` and waits for it, without a shell.
///
/// `system()` would start `/bin/sh` to read a command line that this then has
/// to quote correctly — two processes and a quoting problem, for a program
/// whose arguments are already a list. Handing the list to the kernel skips
/// both: nothing re-parses the arguments, so a path with a space, a quote or a
/// dollar in it is simply an argument.
#if defined(_WIN32)
/// One argument as the Microsoft C runtime reads a command line back into
/// `argv`: in quotes when it has a space, a tab or a quote, with the
/// backslashes before a quote doubled. `_spawnvp` joins its arguments with
/// spaces and nothing more, so without this a path with a space in it would
/// arrive as two.
std::string windowsArgument(const std::string &a) {
  if (!a.empty() && a.find_first_of(" \t\"") == std::string::npos)
    return a;
  std::string out = "\"";
  size_t slashes = 0;
  for (char ch : a) {
    if (ch == '\\') {
      ++slashes;
      continue;
    }
    if (ch == '"') {
      out.append(slashes * 2 + 1, '\\');
      out += '"';
    } else {
      out.append(slashes, '\\');
      out += ch;
    }
    slashes = 0;
  }
  out.append(slashes * 2, '\\');
  return out + "\"";
}

int runProgram(const std::vector<std::string> &argv) {
  std::vector<std::string> quoted;
  for (const std::string &a : argv)
    quoted.push_back(windowsArgument(a));
  std::vector<const char *> raw;
  for (const std::string &a : quoted)
    raw.push_back(a.c_str());
  raw.push_back(nullptr);
  // `p` for the PATH search, as below.
  intptr_t rc = _spawnvp(_P_WAIT, argv[0].c_str(), raw.data());
  return rc < 0 ? 127 : static_cast<int>(rc);
}
#else
int runProgram(const std::vector<std::string> &argv) {
  std::vector<char *> raw;
  raw.reserve(argv.size() + 1);
  for (const std::string &a : argv)
    raw.push_back(const_cast<char *>(a.c_str()));
  raw.push_back(nullptr);

  pid_t pid = 0;
  // `p` for the PATH search: `cc` is a name to look up, not a path.
  int rc = posix_spawnp(&pid, raw[0], nullptr, nullptr, raw.data(), environ);
  if (rc != 0)
    return 127;
  int status = 0;
  if (waitpid(pid, &status, 0) < 0)
    return 127;
  if (WIFSIGNALED(status))
    return 128 + WTERMSIG(status);
  return WIFEXITED(status) ? WEXITSTATUS(status) : 127;
}
#endif

/// The command, as it would have been typed, for `-v` and for a failure note.
std::string spellCommand(const std::vector<std::string> &argv) {
  std::string out;
  for (const std::string &a : argv) {
    if (!out.empty())
      out += ' ';
    out += shellQuote(a);
  }
  return out;
}

/// Links the object file into an executable using the system C toolchain,
/// which already knows how to find libc and the platform's startup files.
/// Links the object into a program, or — with `shared` — into a native
/// shared library the system's loader can open: a `.dylib`, a `.so` or a
/// `.dll`. The command is the same one either way, which is the point: the
/// C toolchain already knows what its platform's shared libraries look like,
/// and there is nothing here to teach it.
bool linkExecutable(const std::string &objPath,
                    const std::vector<std::string> &extraObjects,
                    const std::string &outPath, const CompilerOptions &opts,
                    DiagnosticEngine &diags, bool shared = false) {
  // A cross build names its own toolchain driver; that driver already knows
  // its target's libc, startup files and linker, so nothing else has to be
  // taught the platform. Falling back: $RUNE_CC, then the host's `cc`.
  std::string cc = opts.LinkDriver;
  if (cc.empty()) {
    if (const char *env = getenv("RUNE_CC"))
      cc = env;
    else
#if defined(_WIN32)
      // MinGW-w64 installs `gcc`; a `cc` is not something Windows has.
      cc = "gcc";
#else
      cc = "cc";
#endif
  }

  // The link program may carry arguments of its own:
  // `--linker "clang --target=i686-elf"`.
  std::vector<std::string> argv;
  {
    std::istringstream words(cc);
    for (std::string w; words >> w;)
      argv.push_back(w);
  }

  // Everything below that the compiler adds of its own accord goes through
  // `own`, never straight onto the line. `--no-default-link-args` drops all
  // of it, so the link is exactly what the build said; and a linker run
  // directly (`--linker-kind ld`) gets it in a linker's spelling, with what
  // only a compiler driver understands left out. Nothing is forced on a
  // toolchain it was not written for.
  auto own = [&](const std::string &a) {
    if (opts.NoDefaultLinkArgs)
      return;
    if (!opts.LinkerIsLd) {
      argv.push_back(a);
      return;
    }
    if (a.rfind("-Wl,", 0) == 0) {
      std::stringstream parts(a.substr(4));
      for (std::string part; std::getline(parts, part, ',');)
        argv.push_back(part);
      return;
    }
    if (a.rfind("--target=", 0) == 0 || a.rfind("-fuse-ld", 0) == 0 ||
        a == "-nostdlib" || a == "-static" || a == "-pthread")
      return;
    if (a == "-rdynamic") {
      argv.push_back("--export-dynamic");
      return;
    }
    argv.push_back(a);
  };

  argv.push_back(objPath);
  // Only a driver that was not chosen for the target needs telling. A
  // `<triple>-gcc` is already the right compiler and rejects `--target`;
  // clang is one binary for every target and needs it.
  if (!opts.TargetTriple.empty() && opts.LinkDriver.empty())
    own("--target=" + opts.TargetTriple);
  if (!opts.Sysroot.empty())
    own("--sysroot=" + opts.Sysroot);
  for (const std::string &extra : extraObjects)
    argv.push_back(extra);
  argv.push_back("-o");
  argv.push_back(outPath);
  // A freestanding program carries its runtime compiled in, and links with
  // nothing the platform would otherwise supply: no C library, no start
  // files. What it needs beyond its own object — a linker script, a boot
  // stub — comes through `--link-arg`.
  if (opts.Freestanding) {
    own("-nostdlib");
    own("-static");
  } else {
    std::filesystem::path runtimeLib =
        std::filesystem::path(opts.RuntimeLibDir) / "libruneruntime.a";
    if (std::filesystem::exists(runtimeLib))
      argv.push_back(runtimeLib.string());
    else
      diags.fatal("cannot find the Rune runtime at '{}'", runtimeLib.string())
          .note("build the `runeruntime` target, or pass --runtime-dir");
  }
  const llvm::Triple triple = targetTripleOf(opts);
  const bool isWindows = triple.isOSWindows();
  const bool isWasm = triple.isWasm();

  if (shared && isWasm) {
    diags.fatal("WebAssembly has no shared libraries")
        .note("build a `.rul` library or an executable module instead");
    return false;
  }

  if (shared) {
    own("-shared");
    // Apple wants the library to know the name it will be found under, so a
    // program linked against it records that rather than the build path.
    if (triple.isOSDarwin()) {
      own("-Wl,-install_name,@rpath/" +
                     std::filesystem::path(outPath).filename().string());
    } else if (!isWindows) {
      own("-Wl,-soname," +
                     std::filesystem::path(outPath).filename().string());
    }
  }

  // A debug build exports its symbols so a traceback can name the frames it
  // walks; without this the dynamic linker can only resolve the exported ones.
  // A PE image exports through a different mechanism and has no such flag.
  // WebAssembly has no dynamic linker to ask, and its traps carry their own
  // backtrace.
  if (opts.DebugInfo && !isWindows && !isWasm && !opts.Freestanding)
    own("-rdynamic");

  // With the `-threads` flavour the threads are real: every thread is an
  // instance of its own, so the memory has to come from outside — imported,
  // shared, and with a ceiling it can grow to (the whole of wasm32's 4 GB).
  // Without it the runtime answers the thread calls itself
  // (rune_single_threaded.h), so no emulation library is linked: not every
  // WASI SDK has one.
  if (isWasm && !opts.Freestanding) {
    if (triple.str().find("threads") != std::string::npos) {
      own("-pthread");
      own("-Wl,--import-memory,--export-memory,"
          "--max-memory=4294967296");
    }
    // wasm-ld reserves 64 KB of stack unless told otherwise; a native main
    // thread has 8 MB, and Rune code is written expecting about that.
    own("-Wl,-z,stack-size=8388608");
  }

  for (const std::string &dir : opts.LinkPaths)
    argv.push_back("-L" + dir);
  for (const std::string &arg : opts.LinkArgs)
    argv.push_back(arg);
  bool wantsMath = false;
  bool addedFrameworkPaths = false;
  for (const std::string &spec : opts.LinkLibraries) {
    // `static:name` and `dynamic:name` — `#link("name", type: static)` — are
    // the one file of that kind, found here and named in full: the linkers
    // have no portable way to insist (ld64 has no `-Bstatic`, and GNU ld's
    // `-Bdynamic` still settles for an archive).
    std::string kind, lib = spec;
    size_t colon = spec.find(':');
    if (colon != std::string::npos &&
        (spec.compare(0, colon, "static") == 0 || spec.compare(0, colon, "dynamic") == 0 ||
         spec.compare(0, colon, "framework") == 0)) {
      kind = spec.substr(0, colon);
      lib = spec.substr(colon + 1);
    }
    if (lib == "m")
      wantsMath = true;
    if (kind.empty()) {
      argv.push_back("-l" + lib);
      continue;
    }
    // `#link("Cocoa", type: framework)`: an Apple framework, found on the
    // framework path — which the `#linkpath`s join.
    if (kind == "framework") {
      if (!triple.isOSDarwin()) {
        diags.fatal("`#link(\"{}\", type: framework)` is for Apple's platforms, "
                    "and this build is for {}", lib, triple.str())
            .note("put `#Config(os == \"macos\")` before it, so other "
                  "targets leave it out")
            .code(546);
        return false;
      }
      if (!addedFrameworkPaths) {
        for (const std::string &dir : opts.LinkPaths)
          argv.push_back("-F" + dir);
        addedFrameworkPaths = true;
      }
      argv.push_back("-framework");
      argv.push_back(lib);
      continue;
    }
    std::vector<std::string> files;
    if (kind == "static") {
      files = {"lib" + lib + ".a"};
      if (isWindows)
        files.push_back(lib + ".lib");
    } else if (triple.isOSDarwin()) {
      files = {"lib" + lib + ".dylib", "lib" + lib + ".tbd"};
    } else if (isWindows) {
      files = {"lib" + lib + ".dll.a", lib + ".dll.a", "lib" + lib + ".dll", lib + ".dll"};
    } else {
      files = {"lib" + lib + ".so"};
    }
    std::vector<std::string> dirs = opts.LinkPaths;
    std::string root = opts.Sysroot;
    std::string multiarch = triple.getArchName().str() + "-linux-gnu";
    for (const char *d : {"/usr/local/lib", "/opt/homebrew/lib", "/usr/lib", "/usr/lib64", "/lib"})
      dirs.push_back(root + d);
    if (triple.isOSLinux())
      dirs.push_back(root + "/usr/lib/" + multiarch);
    if (triple.isOSDarwin() && !root.empty())
      dirs.push_back(root + "/usr/lib");
    std::string found;
    for (const std::string &d : dirs) {
      for (const std::string &f : files) {
        std::error_code fec;
        std::filesystem::path p = std::filesystem::path(d) / f;
        if (std::filesystem::exists(p, fec)) {
          found = p.string();
          break;
        }
      }
      if (!found.empty())
        break;
    }
    if (found.empty()) {
      std::string tried;
      for (const std::string &f : files)
        tried += (tried.empty() ? "`" : ", `") + f + "`";
      diags.fatal("`#link(\"{}\", type: {})` found no {}", lib, kind, tried)
          .note("looked in the `#linkpath`s and `-L` directories, then {}",
                "/usr/local/lib, /opt/homebrew/lib and /usr/lib")
          .note("add the directory with `#linkpath(\"...\")`, or drop `type:` to let "
                "the linker choose")
          .code(546);
      return false;
    }
    argv.push_back(found);
  }
  // A program that calls C++ needs the C++ runtime — `operator new`, the
  // standard library's own objects, static initialisers. The driver is a C
  // one, so it is asked for by name: libc++ where Apple ships it, libstdc++
  // everywhere GCC does. A MinGW build links it statically so the executable
  // does not go looking for `libstdc++-6.dll`.
  if (opts.LinkCxx) {
    if (opts.CxxStdlib == "libc++" && !isWasm && !triple.isOSDarwin()) {
      // Asked for by name: C++ built with `-stdlib=libc++` where the
      // platform's own is libstdc++. Its ABI half is a library of its own
      // everywhere but Apple.
      own("-lc++");
      own("-lc++abi");
    } else if (opts.CxxStdlib == "libstdc++" && !isWindows) {
      own("-lstdc++");
    } else if (triple.isOSDarwin() || triple.isOSFreeBSD()) {
      own("-lc++");
    } else if (isWasm) {
      // The WASI SDK ships libc++, split in two as LLVM builds it.
      own("-lc++");
      own("-lc++abi");
    } else if (isWindows) {
      // `-static-libstdc++` is a `g++`-driver spelling and this is `gcc`, so
      // the linker is told directly. `-lgcc_eh` is what libstdc++'s unwinder
      // hooks resolve against once it is static; `-lgcc` and the C runtime
      // are what the driver adds after these anyway.
      own("-Wl,-Bstatic");
      own("-lstdc++");
      own("-lgcc_eh");
      own("-lgcc");
      own("-lwinpthread"); // libstdc++'s threads, static too
      own("-Wl,-Bdynamic");
    } else {
      own("-lstdc++");
    }
  }
  // The standard library calls into libm; only add it if nobody else did, and
  // only where there is one. Windows keeps the math functions in the C
  // runtime and Apple's platforms keep them in libSystem, so on both, asking
  // for `-lm` means the driver goes looking for a library that is not there.
  if (!wantsMath && !isWindows && !triple.isOSDarwin() && !opts.Freestanding)
    own("-lm");
  // The runtime's threads and locks use pthreads; glibc before 2.34 (the
  // manylinux_2_28 build) keeps them in a separate library.
  if (triple.isOSLinux() && !opts.Freestanding)
    own("-lpthread");

  if (opts.Verbose)
    diags.status("link: " + spellCommand(argv));
  int rc = runProgram(argv);
  if (rc != 0) {
    diags.fatal("linking failed (exit status {})", rc)
        .note("the command was: {}", spellCommand(argv));
    return false;
  }
  return true;
}

/// One module of a macro package: a `#type(Macros)` file, or the `#macro fn`s
/// lifted out of an ordinary one.
struct MacroSource {
  /// Where the nested build reads it from.
  std::string Path;
  /// Written to `Path` before the build; empty when `Path` is the author's
  /// own file, read as it stands.
  std::string Text;
  /// What it is called inside the package — `rune_macros::<stem of Path>`.
  std::string Module;
  /// The file or library module it came from, which is what tells one
  /// package's cache entry from another's.
  std::string Origin;
};

/// A module-path-safe stem for a generated macro source.
std::string macroStem(const std::string &module, const char *suffix) {
  std::string out;
  for (char c : module)
    out += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
  return out + suffix;
}

/// The module a file at `path` becomes inside the macro package — named the
/// way any compilation names its files.
std::string macroModuleFor(const std::string &path) {
  const std::string stem = stemOf(path);
  return stem == "main" || stem == "lib" ? "rune_macros" : "rune_macros::" + stem;
}

/// Builds a macro package out of `sources`, and records what it answers to.
///
/// It is an ordinary Rune compilation — the same function, called again —
/// with three differences: it is built for the machine doing the compiling
/// rather than for the target, the compiler writes it a `main`, and its
/// `#macro fn`s are the package rather than something to take out.
///
/// `stamp` already holds what the sources say, token by token. The built
/// program is cached under it, beside the stdlib and the compiler that made
/// it, so a change to any macro — and only to a macro — builds it again.
bool buildMacroPackage(DiagnosticEngine &diags, const CompilerOptions &opts,
                       std::vector<MacroSource> &sources, uint64_t stamp,
                       MacroPackage &out) {
  if (out.Macros.empty())
    return true;

  std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "rune-macros";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);

  // Which package this is, whatever its macros currently say: the files it
  // came from. Old builds of the same package are cleared away once a new
  // one is in place, rather than accumulating one per edit.
  std::vector<std::string> origins;
  for (const MacroSource &src : sources)
    origins.push_back(src.Origin + "=" + src.Module);
  std::sort(origins.begin(), origins.end());
  uint64_t identity = 1469598103934665603ull;
  for (const std::string &o : origins)
    stampText(o + "\n", identity);
  // The module names are what the dispatcher is written against.
  for (const std::string &o : origins)
    stampText(o + "\n", stamp);

  // The library and the compiler by size and time rather than by content:
  // a few dozen `stat`s, not reading a megabyte on every build. Keyed on the
  // sources alone, a package built before a fix to either went on being run
  // after it — with the bug the fix removed.
  auto mixFile = [&](const std::filesystem::path &p) {
    std::error_code fec;
    auto size = std::filesystem::file_size(p, fec);
    auto when = std::filesystem::last_write_time(p, fec);
    stampText(p.string() + ":" +
                  std::to_string(fec ? 0ull
                                     : static_cast<unsigned long long>(size)) +
                  ":" +
                  std::to_string(static_cast<long long>(
                      when.time_since_epoch().count())),
              stamp);
  };
  if (!opts.StdlibDir.empty()) {
    std::error_code wec;
    std::vector<std::filesystem::path> lib;
    for (std::filesystem::recursive_directory_iterator
             it(opts.StdlibDir, wec), end;
         it != end; it.increment(wec)) {
      if (wec)
        break;
      if (it->path().extension() == ".rune")
        lib.push_back(it->path());
    }
    std::sort(lib.begin(), lib.end());
    for (const auto &p : lib)
      mixFile(p);
  }
  static int anchor = 0;
  std::string self = llvm::sys::fs::getMainExecutable(nullptr, &anchor);
  if (!self.empty())
    mixFile(self);

  char identityText[32], stampText16[32];
  std::snprintf(identityText, sizeof identityText, "%016llx",
                static_cast<unsigned long long>(identity));
  std::snprintf(stampText16, sizeof stampText16, "%016llx",
                static_cast<unsigned long long>(stamp));
  const std::string prefix = "macros-" + std::string(identityText) + "-";
  std::filesystem::path program = dir / (prefix + stampText16);
#if defined(_WIN32)
  program += ".exe";
#endif

  // Already built from exactly these macros. Its time is brought up to date,
  // which is what says the entry is still in use.
  if (std::filesystem::exists(program)) {
    std::filesystem::last_write_time(
        program, std::filesystem::file_time_type::clock::now(), ec);
    out.Program = program.string();
    return true;
  }

  // Generated sources — lifted functions, a library's macro files — and the
  // dispatcher go in a directory of this build's own, so two compilers
  // building the same package at once do not write over each other.
  const std::string scratchName =
      "src-" + std::string(stampText16) + "-" + std::to_string(getpid());
  const std::filesystem::path scratch = dir / scratchName;
  std::filesystem::create_directories(scratch, ec);
  auto cleanup = [&] { std::filesystem::remove_all(scratch, ec); };

  std::vector<std::string> modules;
  for (MacroSource &src : sources) {
    modules.push_back(src.Module);
    if (src.Text.empty())
      continue;
    src.Path = (scratch / src.Path).string();
    std::filesystem::create_directories(
        std::filesystem::path(src.Path).parent_path(), ec);
    std::ofstream f(src.Path, std::ios::binary);
    if (!f) {
      diags.fatal("cannot write a macro package source to '{}'", src.Path);
      cleanup();
      return false;
    }
    f << src.Text;
  }
  const std::filesystem::path mainPath =
      scratch / ("main-" + std::string(stampText16) + ".rune");
  {
    std::ofstream f(mainPath, std::ios::binary);
    if (!f) {
      diags.fatal("cannot write the macro package's entry point to '{}'",
                  mainPath.string());
      cleanup();
      return false;
    }
    f << macroDispatcherSource(out.Macros, modules);
  }

  // Linked under a name of its own and renamed into place, so a build that
  // fails halfway — or one killed while linking — never leaves a file that a
  // later build takes for a finished package.
  std::filesystem::path partial = program;
  partial += "." + std::to_string(getpid()) + ".partial";

  CompilerOptions nested;
  nested.MacroPackage = true;
  nested.ModuleName = "rune_macros";
  nested.Output = OutputKind::Executable;
  nested.OutputKindFromFlag = true;
  nested.OutputPath = partial.string();
  nested.StdlibDir = opts.StdlibDir;
  // A cross build's `--runtime-dir` is the target's runtime; the macro
  // program runs here, and links the one built for here.
  nested.RuntimeLibDir =
      opts.TargetTriple.empty() ? opts.RuntimeLibDir : hostRuntimeLibDir();
  // No import paths: the libraries on them were built for the program —
  // for its target and its memory mode — and could not be linked into a
  // program that runs here. A library's own macros come in as sources.
  nested.NoColor = opts.NoColor;
  nested.ForceColor = opts.ForceColor;
  nested.JsonDiagnostics = opts.JsonDiagnostics;
  nested.ShortDiagnostics = opts.ShortDiagnostics;
  nested.Verbose = opts.Verbose;
  // Built to run here, now, whatever the program is being built for — and
  // with counting rather than single ownership, because a macro is a short
  // program whose speed nobody measures and whose borrows nobody wants to
  // argue with.
  nested.Memory = MemoryMode::Arc;
  nested.Safety = SafetyLevel::Minimal;
  nested.OptLevel = 0;
  nested.Inputs.push_back(mainPath.string());
  for (const MacroSource &src : sources)
    nested.Inputs.push_back(src.Path);

  if (opts.Verbose)
    diags.status("building the macro package at " + program.string());
  const int rc = compileWithOptions(nested);
  cleanup();
  if (rc != 0 || !std::filesystem::exists(partial)) {
    std::filesystem::remove(partial, ec);
    diags.fatal("the macro package did not build")
        .note("its macros cannot run, so nothing that uses one can be "
              "expanded");
    return false;
  }
  std::filesystem::rename(partial, program, ec);
  if (ec && !std::filesystem::exists(program)) {
    diags.fatal("cannot put the macro package in place at '{}'",
                program.string());
    return false;
  }
  std::filesystem::remove(partial, ec);

  // Earlier builds of this package are out of date now, and any package no
  // build has asked for in a fortnight — a project deleted, or moved — is
  // only taking up room.
  const auto stale = std::filesystem::file_time_type::clock::now() -
                     std::chrono::hours(24 * 14);
  std::vector<std::filesystem::path> unwanted;
  for (auto it = std::filesystem::directory_iterator(dir, ec);
       !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
    const std::string name = it->path().filename().string();
    if (it->path() == program)
      continue;
    // Only a finished package — the prefix and a stamp, nothing after — is
    // an earlier build. Anything longer belongs to a build still running:
    // its `.partial`, and the object file it is about to link.
    std::string finished = name;
#if defined(_WIN32)
    if (finished.size() > 4 &&
        finished.compare(finished.size() - 4, 4, ".exe") == 0)
      finished.resize(finished.size() - 4);
#endif
    const bool earlier = finished.rfind(prefix, 0) == 0 &&
                         finished.size() == prefix.size() + 16;
    std::error_code tec;
    const auto when = std::filesystem::last_write_time(it->path(), tec);
    if (earlier || (!tec && when < stale))
      unwanted.push_back(it->path());
  }
  for (const auto &p : unwanted)
    std::filesystem::remove_all(p, ec);
  out.Program = program.string();
  return true;
}

} // namespace

//===----------------------------------------------------------------------===//
// `--query-members`: what an editor completes after a `.`
//===----------------------------------------------------------------------===//

namespace {

std::string queryJson(const std::string &s) {
  std::string out = "\"";
  for (unsigned char c : s) {
    switch (c) {
    case '"': out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n"; break;
    case '\r': out += "\\r"; break;
    case '\t': out += "\\t"; break;
    default:
      if (c < 0x20) {
        static const char *hex = "0123456789abcdef";
        out += "\\u00";
        out += hex[c >> 4];
        out += hex[c & 15];
      } else {
        out += static_cast<char>(c);
      }
    }
  }
  return out + "\"";
}

/// `fn name(a: T, b: U) -> R`, with the types as this instantiation has them.
std::string methodSignature(const FunctionDecl *fn) {
  std::vector<const Param *> shown;
  for (const Param &p : fn->Params)
    if (!p.IsSelf)
      shown.push_back(&p);
  std::vector<Type *> types;
  Type *result = nullptr;
  if (fn->Ty && fn->Ty->is(TypeKind::Function)) {
    types = fn->Ty->params();
    result = fn->Ty->result();
  }
  // The function type may or may not count `self`; line the rest up from
  // the end, where the written parameters are.
  size_t skip = types.size() > shown.size() ? types.size() - shown.size() : 0;
  std::string out = "fn " + fn->Name + "(";
  for (size_t i = 0; i < shown.size(); ++i) {
    if (i) out += ", ";
    out += shown[i]->Name;
    if (skip + i < types.size() && types[skip + i])
      out += ": " + types[skip + i]->toString();
  }
  out += ")";
  if (result && !result->isVoid() && result->toString() != "()")
    out += " -> " + result->toString();
  return out;
}

/// The expression in `root` spanning [start, end): the outermost one that
/// ends exactly at `end` and begins no earlier than `start`.
const Expr *exprSpanning(const Node *root, SourceLoc start, SourceLoc end) {
  const Expr *best = nullptr;
  NodeVisitor visit;
  visit = [&](const Node *n) {
    if (!n)
      return;
    if (n->Range.isValid() && (n->Range.end() < start || end < n->Range.begin()))
      return;   // entirely elsewhere
    // Expressions are the first run of node kinds, up to `Error`.
    if (n->Kind <= NodeKind::Error) {
      auto *e = static_cast<const Expr *>(n);
      if (e->Ty && n->Range.end() == end && start <= n->Range.begin() &&
          (!best || n->Range.begin() < best->Range.begin()))
        best = e;
    }
    forEachChild(n, visit);
  };
  visit(root);
  return best;
}

int answerMemberQuery(const SourceManager &sm, const SemaResult &result,
                      const CompilerOptions &opts) {
  // The file, by the path it was given as.
  const SourceFile *file = nullptr;
  for (unsigned i = 0; i < sm.fileCount(); ++i)
    if (sm.file(i).Path == opts.QueryFile)
      file = &sm.file(i);
  if (!file) {
    std::cout << "{\"error\":" << queryJson("no such input: " + opts.QueryFile) << "}\n";
    return 1;
  }
  SourceLoc start = sm.locForFileOffset(file->ID, opts.QueryStart);
  SourceLoc end = sm.locForFileOffset(file->ID, opts.QueryEnd);

  // Every checked body: instantiations of generic code are clones that keep
  // their template's ranges, so this finds a receiver inside those too.
  const Expr *found = nullptr;
  std::string where;      // the module the expression is in
  for (const FunctionDecl *fn : result.Functions) {
    if (!fn->Body || !fn->Body->Range.isValid())
      continue;
    if (fn->Body->Range.end() < start || end < fn->Body->Range.begin())
      continue;
    if ((found = exprSpanning(fn->Body.get(), start, end))) {
      where = fn->ModulePath;
      break;
    }
  }
  if (!found)
    for (const GlobalVarDecl *g : result.Globals)
      if (g->Init && (found = exprSpanning(g->Init.get(), start, end)))
        break;
  if (!found || !found->Ty || found->Ty->isError()) {
    std::cout << "{\"error\":\"no typed expression there\"}\n";
    return 1;
  }

  // Members belong to what a borrow points at.
  Type *t = found->Ty;
  while (t->is(TypeKind::Pointer) && !t->isRawPointer() && t->pointee())
    t = t->pointee();
  if (t->isOpaque() && t->opaqueUnderlying())
    t = t->opaqueUnderlying();

  std::string members;
  std::set<std::string> seen;
  auto add = [&](const std::string &name, const char *kind, const std::string &detail,
                 const std::string &doc, bool isStatic, bool isPublic,
                 const std::string &module) {
    if (name.empty() || !seen.insert(name).second)
      return;
    if (!members.empty())
      members += ",";
    members += "{\"name\":" + queryJson(name) + ",\"kind\":\"" + kind + "\"" +
               ",\"detail\":" + queryJson(detail) + ",\"doc\":" + queryJson(doc) +
               ",\"static\":" + (isStatic ? "true" : "false") +
               ",\"public\":" + (isPublic ? "true" : "false") +
               ",\"module\":" + queryJson(module) + "}";
  };

  // Fields: the type's own, then each superclass's.
  if (NominalDecl *nd = t->nominal()) {
    std::vector<NominalDecl *> chain{nd};
    if (auto *c = dyn_cast<ClassDecl>(static_cast<Decl *>(nd)))
      for (ClassDecl *sc = c->Super; sc; sc = sc->Super)
        chain.push_back(sc);
    for (NominalDecl *n : chain)
      for (const auto &f : n->Fields)
        add(f->Name, "field", f->Name + ": " + (f->Ty ? f->Ty->toString() : std::string("?")),
            f->Doc, false, f->IsPublic, static_cast<Decl *>(n)->ModulePath);
  }

  // Methods: whatever the type's table holds — its own, every `extend`, and
  // every `bind` that applies to this instantiation.
  auto emitTable = [&](Type *key) {
    auto it = result.MethodTables.find(key);
    if (it == result.MethodTables.end())
      return;
    for (const auto &entry : it->second) {
      const FunctionDecl *fn = entry.second;
      if (!fn || fn->WhereUnmet)
        continue;
      if (fn->Flavour == FunctionFlavour::Initialiser ||
          fn->Flavour == FunctionFlavour::Deinitialiser)
        continue;
      bool isStatic = fn->IsStatic || fn->Flavour == FunctionFlavour::Free;
      bool isPublic = fn->IsPublic || fn->Bind || fn->FromMark;
      add(fn->Name, "method", methodSignature(fn), fn->Doc, isStatic, isPublic,
          fn->ModulePath);
    }
  };
  emitTable(t);
  if (auto *c = t->nominal() ? dyn_cast<ClassDecl>(static_cast<Decl *>(t->nominal())) : nullptr)
    for (ClassDecl *sc = c->Super; sc; sc = sc->Super)
      emitTable(sc->DeclaredType);
  // A `dyn Mark`: what the mark requires.
  if (auto *mk = t->nominal() ? dyn_cast<MarkDecl>(static_cast<Decl *>(t->nominal())) : nullptr)
    for (const auto &fn : mk->Methods)
      add(fn->Name, "method", methodSignature(fn.get()), fn->Doc, false, true,
          static_cast<Decl *>(mk)->ModulePath);

  std::cout << "{\"type\":" << queryJson(found->Ty->toString())
            << ",\"module\":" << queryJson(where)
            << ",\"members\":[" << members << "]}\n";
  return 0;
}


//===--- --query-hints ---------------------------------------------------===//

/// Spells `t` as the file whose module is `mod` can write it: a type from
/// another module goes through the name that module is imported under. Sets
/// `ok` false when there is no way to write it there — a module that is not
/// imported, a `some`, a type nested in another.
std::string spellType(const Type *t, const Module &mod, bool &ok) {
  if (!t) { ok = false; return "?"; }
  if (t->isOpaque()) { ok = false; return t->toString(); }
  auto list = [&](const std::vector<Type *> &ts) {
    std::string out;
    for (size_t i = 0; i < ts.size(); ++i) {
      if (i) out += ", ";
      out += spellType(ts[i], mod, ok);
    }
    return out;
  };
  switch (t->kind()) {
  case TypeKind::Error:
  case TypeKind::Opaque:
    ok = false;
    return t->toString();
  case TypeKind::Pointer: {
    if (t->isWeakPointer()) ok = false;
    std::string s = t->isRawPointer() ? "*" : "&";
    if (t->isMutablePointer()) s += "var ";
    return s + spellType(t->pointee(), mod, ok);
  }
  case TypeKind::Array:
    return "[" + std::to_string(t->arraySize()) + ":" + spellType(t->element(), mod, ok) + "]";
  case TypeKind::Slice:
    return "[" + spellType(t->element(), mod, ok) + "]";
  case TypeKind::Tuple:
    return "(" + list(t->params()) + ")";
  case TypeKind::Function:
  case TypeKind::CFunction: {
    std::string s = t->is(TypeKind::CFunction) ? "@cfunction(" : "@function(";
    s += list(t->params()) + ")";
    if (t->result() && !t->result()->isVoid())
      s += " -> " + spellType(t->result(), mod, ok);
    return s;
  }
  case TypeKind::Struct:
  case TypeKind::Enum:
  case TypeKind::Class:
  case TypeKind::Mark:
  case TypeKind::DynMark: {
    const auto *d = static_cast<const Decl *>(t->nominal());
    if (!d) { ok = false; return t->toString(); }
    if (d->Parent) ok = false;
    std::string name = d->Name;
    bool reachable = d->ModulePath.empty() || d->ModulePath == mod.Name;
    // The prelude's types need no import.
    if (!reachable && (name == "Option" || name == "Result") &&
        d->ModulePath.rfind("std", 0) == 0)
      reachable = true;
    std::string prefix;
    for (const DeclPtr &dp : mod.Decls) {
      if (reachable || dp->Kind != NodeKind::Import)
        continue;
      const auto *im = static_cast<const ImportDecl *>(dp.get());
      std::string path;
      for (size_t k = 0; k < im->Path.size(); ++k)
        path += (k ? "::" : "") + im->Path[k];
      bool same = path == d->ModulePath ||
                  (im->ResolvedModule && im->ResolvedModule->Name == d->ModulePath);
      if (!same)
        continue;
      if (im->IsGlob) {
        reachable = true;
      } else if (!im->Names.empty()) {
        for (const std::string &n : im->Names)
          if (n == name)
            reachable = true;
      } else {
        prefix = (im->Alias.empty() ? im->Path.back() : im->Alias) + "::";
        reachable = true;
      }
    }
    if (!reachable) ok = false;
    std::string s = prefix + name;
    if (t->is(TypeKind::DynMark))
      return "dyn " + s;
    if (!t->params().empty())
      s += "<" + list(t->params()) + ">";
    if (t->isUniq()) ok = false;
    return s;
  }
  default:
    return t->toString();
  }
}

struct Hint {
  uint32_t Offset;
  std::string Kind;    // "type" or "parameter"
  std::string Label;
  std::string Insert;  // empty when it cannot be written there
};

/// Every hint in `root`'s subtree that falls in `file`.
void collectHints(const Node *root, const SourceFile &file, const Module &mod,
                  const std::vector<SourceRange> &generic,
                  std::map<uint32_t, Hint> &out) {
  auto offsetOf = [&](SourceLoc l) -> int64_t {
    if (!l.isValid() || l.raw() < file.StartOffset ||
        l.raw() > file.StartOffset + file.Buffer.size())
      return -1;
    return static_cast<int64_t>(l.raw() - file.StartOffset);
  };
  auto inGeneric = [&](SourceLoc l) {
    for (const SourceRange &r : generic)
      if (r.begin() <= l && l < r.end())
        return true;
    return false;
  };
  // Some code reaches the checker rewritten — a `bind`'s methods, a
  // closure given its parameter types — and its ranges are not always the
  // file's own. A hint is only kept where the text says it belongs.
  const std::string &text = file.Buffer;
  auto isWord = [](char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' ||
           static_cast<unsigned char>(c) >= 0x80;
  };
  auto nameEndsAt = [&](int64_t off, const std::string &name) {
    if (off < static_cast<int64_t>(name.size()) || off > static_cast<int64_t>(text.size()))
      return false;
    size_t from = static_cast<size_t>(off) - name.size();
    if (text.compare(from, name.size(), name) != 0)
      return false;
    if (from > 0 && isWord(text[from - 1]))
      return false;
    return static_cast<size_t>(off) >= text.size() || !isWord(text[static_cast<size_t>(off)]);
  };
  auto afterOpenOrComma = [&](int64_t off) {
    int64_t k = off - 1;
    while (k >= 0 && (text[k] == ' ' || text[k] == '\t' || text[k] == '\n' || text[k] == '\r'))
      --k;
    return k >= 0 && (text[k] == '(' || text[k] == ',');
  };
  auto typeHint = [&](SourceLoc at, const Type *ty, bool insertable, const std::string &name) {
    int64_t off = offsetOf(at);
    if (off < 0 || !ty || ty->isError() || ty->isVoid() || ty->isNever() ||
        inGeneric(at) || !nameEndsAt(off, name))
      return;
    bool ok = true;
    std::string spelled = spellType(ty, mod, ok);
    Hint h{static_cast<uint32_t>(off), "type", ": " + spelled,
           ok && insertable ? ": " + spelled : ""};
    out.emplace(h.Offset, h);
  };
  NodeVisitor visit;
  visit = [&](const Node *n) {
    if (!n)
      return;
    switch (n->Kind) {
    case NodeKind::VarStmt: {
      auto *v = static_cast<const VarStmtNode *>(n);
      if (!v->TypeAnnotation && v->Init && v->DeclaresNew && !v->IsGlobal &&
          v->Binding && v->Binding->Kind == NodeKind::BindingPat) {
        auto *b = static_cast<const BindingPattern *>(v->Binding.get());
        if (!b->Sub && b->Binding)
          typeHint(b->Range.end(), b->Binding->Ty, true, b->Name);
      }
      break;
    }
    case NodeKind::For: {
      auto *f = static_cast<const ForExpr *>(n);
      if (f->Binding && f->Binding->Kind == NodeKind::BindingPat) {
        auto *b = static_cast<const BindingPattern *>(f->Binding.get());
        if (!b->Sub && b->Binding)
          typeHint(b->Range.end(), b->Binding->Ty, false, b->Name);   // no syntax for it
      }
      break;
    }
    case NodeKind::Closure: {
      auto *c = static_cast<const ClosureExpr *>(n);
      if (c->Ty && c->Ty->is(TypeKind::Function)) {
        const auto &ps = c->Ty->params();
        for (size_t i = 0; i < c->Params.size() && i < ps.size(); ++i) {
          const Param &p = c->Params[i];
          if (p.TypeAnnotation || p.IsSelf || !p.Range.isValid())
            continue;
          typeHint(p.Range.begin().offsetBy(static_cast<int32_t>(p.Name.size())), ps[i], true, p.Name);
        }
      }
      break;
    }
    case NodeKind::Call: {
      auto *c = static_cast<const CallExpr *>(n);
      for (size_t i = 0; i < c->Args.size() && i < c->ParamLabels.size(); ++i) {
        const Argument &a = c->Args[i];
        const std::string &name = c->ParamLabels[i];
        if (name.empty() || !a.Label.empty() || !a.Value)
          continue;
        const Expr *v = a.Value.get();
        // An argument that already says it — `f(width)` for `width` — needs
        // no label to be read, though a formatter may still write one.
        // The argument's slot: just inside the call's `(`, or just after the
        // `,` that ends the one before. Only more `(`s — the argument is in
        // brackets of its own — may stand between it and the expression,
        // whose range starts inside them. A receiver passed as the first
        // argument is outside the brackets altogether.
        int64_t begin = offsetOf(v->Range.begin());
        int64_t paren = offsetOf(c->ParenRange.begin());
        if (begin < 0 || paren < 0 || begin <= paren || text[static_cast<size_t>(paren)] != '(')
          continue;
        int64_t off = paren + 1;
        if (i > 0) {
          const Expr *before = c->Args[i - 1].Value.get();
          int64_t k = before ? offsetOf(before->Range.end()) : -1;
          while (k >= 0 && k < begin && text[static_cast<size_t>(k)] != ',')
            ++k;
          if (k < 0 || k >= begin)
            continue;
          off = k + 1;
        }
        auto blank = [&](char ch) { return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r'; };
        while (off < begin && blank(text[static_cast<size_t>(off)]))
          ++off;
        bool clean = true;
        for (int64_t k = off; k < begin; ++k)
          if (!blank(text[static_cast<size_t>(k)]) && text[static_cast<size_t>(k)] != '(')
            clean = false;
        if (!clean || !afterOpenOrComma(off))
          continue;
        Hint h{static_cast<uint32_t>(off), "parameter", name + ":", name + ": "};
        out.emplace(h.Offset, h);
      }
      break;
    }
    default:
      break;
    }
    forEachChild(n, visit);
  };
  visit(root);
}

/// The ranges of generic declarations in `mod`: their bodies are checked
/// once per instantiation, with concrete types no source could write there.
void genericRanges(const std::vector<DeclPtr> &decls, std::vector<SourceRange> &out) {
  auto fns = [&](const std::vector<std::unique_ptr<FunctionDecl>> &ms) {
    for (const auto &m : ms)
      if (!m->Generics.empty())
        out.push_back(m->Range);
  };
  for (const DeclPtr &d : decls) {
    switch (d->Kind) {
    case NodeKind::Function:
      if (!static_cast<const FunctionDecl *>(d.get())->Generics.empty())
        out.push_back(d->Range);
      break;
    case NodeKind::Struct:
    case NodeKind::Enum:
    case NodeKind::Class: {
      auto *nd = static_cast<const NominalDecl *>(d.get());
      if (!nd->Generics.empty()) out.push_back(d->Range);
      else fns(nd->Methods);
      break;
    }
    case NodeKind::Bind: {
      auto *b = static_cast<const BindDecl *>(d.get());
      if (!b->Generics.empty()) out.push_back(d->Range);
      else fns(b->Methods);
      break;
    }
    case NodeKind::Extend: {
      auto *e = static_cast<const ExtendDecl *>(d.get());
      if (!e->Generics.empty() || e->FoldedIntoTemplate) out.push_back(d->Range);
      else fns(e->Methods);
      break;
    }
    default:
      break;
    }
  }
}

int answerHintQuery(const SourceManager &sm, const SemaResult &result,
                    const std::vector<std::unique_ptr<Module>> &modules,
                    const DiagnosticEngine &diags, const CompilerOptions &opts) {
  const SourceFile *file = nullptr;
  for (unsigned i = 0; i < sm.fileCount(); ++i)
    if (sm.file(i).Path == opts.QueryFile)
      file = &sm.file(i);
  const Module *mod = nullptr;
  for (const auto &m : modules)
    if (file && m->FileID == file->ID)
      mod = m.get();
  if (!file || !mod) {
    std::cout << "{\"error\":" << queryJson("no such input: " + opts.QueryFile) << "}\n";
    return 1;
  }
  // Inside a generic declaration, the types are an instantiation's; inside
  // a macro invocation, the code is the macro's. Neither is written in.
  std::vector<SourceRange> generic;
  genericRanges(mod->Decls, generic);
  std::vector<SourceRange> expanded = diags.expansionRanges();
  SourceLoc first = sm.locForFileOffset(file->ID, 0);
  SourceLoc last = sm.locForFileOffset(file->ID, static_cast<uint32_t>(file->Buffer.size()));
  std::map<uint32_t, Hint> hints;
  for (const FunctionDecl *fn : result.Functions) {
    if (!fn->Body || !fn->Body->Range.isValid() || fn->Body->Range.end() < first ||
        last < fn->Body->Range.begin())
      continue;
    collectHints(fn->Body.get(), *file, *mod, generic, hints);
  }
  for (const GlobalVarDecl *g : result.Globals)
    if (g->Init)
      collectHints(g->Init.get(), *file, *mod, generic, hints);
  std::cout << "{\"hints\":[";
  bool firstOut = true;
  for (const auto &[off, h] : hints) {
    SourceLoc at = sm.locForFileOffset(file->ID, off);
    bool inMacro = false;
    for (const SourceRange &r : expanded)
      if (r.isValid() && r.begin() < at && at < r.end())   // an argument may be one
        inMacro = true;
    if (inMacro)
      continue;
    std::cout << (firstOut ? "" : ",") << "{\"offset\":" << off
              << ",\"kind\":\"" << h.Kind << "\",\"label\":" << queryJson(h.Label)
              << ",\"insert\":" << queryJson(h.Insert) << "}";
    firstOut = false;
  }
  std::cout << "]}\n";
  return 0;
}

} // namespace

/// The value of the program directive `@<name>(<value>)` at the top of
/// `text`, or empty. Only the directives before the first declaration count,
/// as the parser has it; this reads them before anything is parsed, because
/// `#runtime(none)` decides which files the compilation is made of.
static std::string programDirective(const std::string &text,
                                    const std::string &name) {
  size_t i = 0;
  const size_t n = text.size();
  for (;;) {
    while (i < n && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' ||
                     text[i] == '\n' || text[i] == ';'))
      ++i;
    if (i + 1 < n && text[i] == '/' && text[i + 1] == '/') {
      while (i < n && text[i] != '\n')
        ++i;
      continue;
    }
    if (i >= n || (text[i] != '@' && text[i] != '#'))
      return "";
    size_t start = ++i;
    while (i < n && (std::isalnum(static_cast<unsigned char>(text[i])) ||
                     text[i] == '_'))
      ++i;
    const std::string word = text.substr(start, i - start);
    std::string value;
    if (i < n && text[i] == '(') {
      size_t close = text.find(')', i);
      if (close == std::string::npos)
        return "";
      value = text.substr(i + 1, close - i - 1);
      i = close + 1;
    }
    auto trim = [](std::string v) {
      const char *ws = " \t\r\n";
      size_t a = v.find_first_not_of(ws);
      if (a == std::string::npos)
        return std::string();
      return v.substr(a, v.find_last_not_of(ws) - a + 1);
    };
    // Only the directives the parser reads at the top of a file; anything
    // else is a declaration's decorator, and the directives are over.
    if (word != "type" && word != "link" && word != "linkpath" &&
        word != "runtime" && word != "entry" && word != "lint")
      return "";
    if (word == name)
      return trim(value);
  }
}

int compileWithOptions(const CompilerOptions &given) {
  CompilerOptions opts = given;
  // `--tiers` builds the standard library as a freestanding program would
  // see it — the `runtime == "none"` definitions, the freestanding runtime
  // compiled in — and stops after code generation.
  if (opts.TierReport) {
    opts.Freestanding = true;
    opts.NoEntry = true;
    opts.Output = OutputKind::Object;
    if (opts.ModuleName.empty())
      opts.ModuleName = "tiers";
  }
  PhaseTimer timer(opts.TimeReport);
  SourceManager sm;
  DiagnosticEngine diags(sm);
  diags.detectColor();
  if (opts.ForceColor) diags.setColorEnabled(true);
  if (opts.NoColor) diags.setColorEnabled(false);
  diags.setJsonOutput(opts.JsonDiagnostics);
  diags.setShortOutput(opts.ShortDiagnostics);
  diags.setWarningsAsErrors(opts.WarningsAsErrors);
  diags.setQuietWarnings(opts.NoWarnings);
  diags.setErrorLimit(opts.ErrorLimit);

  std::vector<unsigned> fileIDs;
  for (const std::string &path : opts.Inputs) {
    std::optional<unsigned> id;
    // The same file may be spelt two ways — relative, or through a symbolic
    // link such as macOS's `/var` — by an editor and by a build tool.
    auto over = opts.SourceOverrides.find(path);
    if (over == opts.SourceOverrides.end() && !opts.SourceOverrides.empty()) {
      std::error_code ec;
      auto canon = std::filesystem::weakly_canonical(path, ec);
      for (auto it = opts.SourceOverrides.begin();
           !ec && it != opts.SourceOverrides.end(); ++it) {
        std::error_code ec2;
        if (std::filesystem::weakly_canonical(it->first, ec2) == canon && !ec2) {
          over = it;
          break;
        }
      }
    }
    if (over != opts.SourceOverrides.end()) {
      std::ifstream in(over->second, std::ios::binary);
      if (in) {
        std::ostringstream text;
        text << in.rdbuf();
        id = sm.addBuffer(path, text.str());
      }
    } else {
      id = sm.loadFile(path);
    }
    if (!id) {
      diags.fatal("cannot open input file '{}'", path)
          .note("check the path and that the file is readable");
      return 1;
    }
    fileIDs.push_back(*id);
  }

  // `#runtime(none)` and `#entry(none)` in any of the program's own files say
  // the same as the flags. They are read now because a freestanding program
  // is compiled together with the runtime it brings along.
  for (unsigned id : fileIDs) {
    const std::string &text = sm.file(id).Buffer;
    if (programDirective(text, "runtime") == "none")
      opts.Freestanding = true;
    if (programDirective(text, "entry") == "none")
      opts.NoEntry = true;
  }
  // The generated code's runtime needs — panics, the heap, `memcpy` — are
  // answered by a Rune file compiled into the program, for its target, the
  // way Rust builds `core` for the target it is compiling for. A library is
  // compiled without it: the program that links the library brings one.
  for (const auto &kv : opts.ConfigValues)
    if (kv.first == "freestanding_type" && kv.second != "full" &&
        kv.second != "minimal") {
      diags.error(SourceRange(), "`freestanding_type` is '{}'", kv.second)
          .note("it is `full`, the whole freestanding runtime, or `minimal`, "
                "only what a program cannot run without")
          .code(545);
      return 1;
    }
  if (opts.Freestanding && opts.Output != OutputKind::Library &&
      opts.Output != OutputKind::Docs && opts.Output != OutputKind::None) {
    // The core — panics, the heap, memory — and `String`, numbers as text,
    // and printing to the program's `#output`.
    for (const char *part : {"freestanding.rune", "freestanding_text.rune"}) {
      const std::filesystem::path rt =
          std::filesystem::path(opts.RunetimeDir) / part;
      std::optional<unsigned> id = sm.loadFile(rt.string());
      if (!id) {
        diags.fatal("cannot find the freestanding runtime at '{}'", rt.string())
            .note("it ships in the toolchain's `runetime/` directory");
        return 1;
      }
      fileIDs.push_back(*id);
    }
  }

  if (opts.Verbose)
    diags.status(fmt("lexing {} file(s)", fileIDs.size()));

  if (opts.Dump == DumpKind::Tokens) {
    for (unsigned id : fileIDs) {
      Lexer lexer(sm, diags, id);
      dumpTokens(sm, lexer.tokenize());
    }
    return diags.hadError() ? 1 : 0;
  }

  std::vector<std::unique_ptr<Module>> modules;
  // Object files extracted from imported libraries; handed to the linker and
  // removed afterwards.
  std::vector<std::string> libraryObjects;

  // Rune libraries on the module search path. Each `.rul` carries its modules'
  // source, so their declarations are re-parsed here while their compiled code
  // comes from the embedded object file. The source is only read in at this
  // point: parsing it waits until the macro table is built, a few lines down,
  // because a library's source is its whole source — macro calls in it have to
  // expand like anywhere else, and a `pub macro` in it belongs to everyone who
  // imports the library.
  std::vector<std::pair<unsigned, std::string>> libraryUnits;
  /// One entry per library unit: the conditions that library was built with.
  /// A `.rul` carries its own source, so its `#Config`s are answered again
  /// here — and they have to be answered the way they were when its object
  /// code was made, or the interface would describe code that is not in it.
  std::vector<ConfigSet> libraryConfigs;
  bool libraryError = false;
  timer.phase("read", [&] {
  for (const std::string &dir : opts.ImportPaths) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec))
      continue;
    std::vector<std::filesystem::path> libs;
    for (auto it = std::filesystem::directory_iterator(dir, ec);
         !ec && it != std::filesystem::directory_iterator(); ++it)
      if (it->is_regular_file() && it->path().extension() == ".rul")
        libs.push_back(it->path());
    std::sort(libs.begin(), libs.end());
    // The library this compile is about to write is not one of its inputs:
    // its old interface says nothing about the new one, and one left by
    // another compiler would otherwise stop the very build that replaces it.
    std::filesystem::path replacing;
    if (opts.Output == OutputKind::Library)
      replacing = std::filesystem::weakly_canonical(defaultOutputName(opts), ec);
    for (const auto &libPath : libs) {
      if (!replacing.empty() &&
          std::filesystem::weakly_canonical(libPath, ec) == replacing)
        continue;
      LibraryContents lib;
      // Read quietly first: a library on the path that this compile does not
      // import — a sibling output left by another compiler, or one still
      // being written — must not stop it. What is wrong is said as a
      // warning; an import of it fails later as a missing module.
      {
        DiagnosticEngine probe(sm);
        probe.setSilent(true);
        if (!readLibrary(libPath.string(), lib, probe)) {
          diags.warn(SourceRange(), "skipping '{}': it could not be read as "
                                    "a library of this compiler's",
                     libPath.string())
              .note("rebuilding the package that makes it replaces it");
          continue;
        }
      }
      // A library that *is* the module being compiled — the unit or package
      // an editor is checking, whose last build sits on the import path — is
      // that module's old self, not something it imports.
      if (!opts.ModuleName.empty() && lib.ModuleName == opts.ModuleName)
        continue;
      // The object code inside bakes in one memory model — retains and
      // releases, or their absence and the moved-in argument convention —
      // so a library from the other model would link and then miscount.
      if (lib.Memory != opts.Memory) {
        diags.fatal("'{}' was built with `--memory {}`; this build is "
                    "`--memory {}`", libPath.string(),
                    memoryModeName(lib.Memory), memoryModeName(opts.Memory))
            .note("rebuild the dependency with the same memory mode")
            .code(291);
        libraryError = true;
        return;
      }
      ConfigSet libCfg = ConfigSet::forOptions(opts);
      libCfg.Flags.clear();
      for (const auto &kv : opts.ConfigValues)
        libCfg.Values.erase(kv.first);
      if (opts.DebugInfo)
        libCfg.Flags.insert("debug");
      for (const std::string &flag : lib.ConfigFlags)
        libCfg.Flags.insert(flag);
      for (const auto &kv : lib.ConfigValues)
        if (!libCfg.Builtin.count(kv.first))
          libCfg.Values[kv.first] = kv.second;
      for (const auto &unit : lib.Interfaces) {
        unsigned id = sm.addBuffer(libPath.string() + " (" + unit.first + ")",
                                   unit.second);
        libraryUnits.push_back({id, unit.first});
        libraryConfigs.push_back(libCfg);
      }
      // A name no other compile can be using. Builds run several compilers
      // at once, and more than one of them may import this same library; a
      // fixed name in the temp directory means one deleting its extracted
      // object on the way out while another is still linking against it.
      std::filesystem::path objOut =
          std::filesystem::temp_directory_path(ec) /
          (libPath.stem().string() + "." + std::to_string(getpid()) + "." +
           std::to_string(libraryObjects.size()) + ".rul.o");
      if (!extractLibraryObject(libPath.string(), objOut.string(), diags)) {
        libraryError = true;
        return;
      }
      libraryObjects.push_back(objOut.string());
      if (opts.Verbose)
        diags.status(fmt("using library {}", libPath.string()));
    }
  }
  });
  if (libraryError)
    return 1;

  // The standard library is compiled from source alongside the user's inputs,
  // so `import std::io` resolves without any prebuilt artefact.
  // The standard library's files, loaded but not yet parsed: macros are
  // gathered from every file first, so one written here is in scope in user
  // code too.
  std::vector<std::pair<unsigned, std::string>> stdlibUnits;
  if (!opts.NoStdlib && !opts.StdlibDir.empty()) {
    std::error_code ec;
    std::filesystem::path root(opts.StdlibDir);
    if (std::filesystem::is_directory(root, ec)) {
      std::vector<std::filesystem::path> stdFiles;
      for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
           !ec && it != std::filesystem::recursive_directory_iterator(); ++it)
        if (it->is_regular_file() && it->path().extension() == ".rune")
          stdFiles.push_back(it->path());
      std::sort(stdFiles.begin(), stdFiles.end());
      for (const auto &p : stdFiles) {
        auto id = sm.loadFile(p.string());
        if (!id)
          continue;
        stdlibUnits.push_back({*id, std::string()});
        // stdlib/std/io.rune -> module "std::io"
        std::string name = std::filesystem::relative(p, root, ec).string();
        if (name.size() > 5)
          name = name.substr(0, name.size() - 5); // drop ".rune"
        for (size_t i = 0; i + 1 < name.size(); ++i)
          if (name[i] == '/' || name[i] == '\\') {
            name.replace(i, 1, "::");
            ++i;
          }
        stdlibUnits.back().second = name;
      }
    }
  }

  // The package's root file (`main.rune` or `lib.rune`, or one named after the
  // package) *is* the package module; every other file becomes a submodule of
  // it, so two packages can both have a `shapes.rune` without colliding.
  //
  // Everything the compilation will read is listed first, in the order the
  // modules have to end up in: libraries, then the standard library, then
  // this package's own files.
  struct Unit {
    unsigned FileID = 0;
    std::string ModuleName;
    bool FromLibrary = false;
    bool IsStdlib = false;
  };
  std::vector<Unit> units;
  for (const auto &u : libraryUnits)
    units.push_back({u.first, u.second, /*FromLibrary=*/true, false});
  size_t firstStdlibUnit = units.size();
  for (const auto &u : stdlibUnits)
    units.push_back({u.first, u.second, false, /*IsStdlib=*/true});
  size_t firstUserModule = units.size();
  for (unsigned id : fileIDs) {
    std::string stem = stemOf(sm.file(id).Path);
    units.push_back({id,
                     (stem == "main" || stem == "lib" || stem == opts.ModuleName)
                         ? opts.ModuleName
                         : opts.ModuleName + "::" + stem,
                     false, false});
  }

  // Macros are gathered in a different order from the one the modules are in:
  // the standard library first, then libraries, then this package. Two macros
  // of one name are a diagnostic pointing at "the earlier declaration", and
  // which one that is should not change with how the units happen to be
  // listed.
  std::vector<size_t> macroOrder;
  macroOrder.reserve(units.size());
  for (size_t i = 0; i < units.size(); ++i)
    if (units[i].IsStdlib)
      macroOrder.push_back(i);
  for (size_t i = 0; i < units.size(); ++i)
    if (units[i].FromLibrary)
      macroOrder.push_back(i);
  for (size_t i = firstUserModule; i < units.size(); ++i)
    macroOrder.push_back(i);

  // Lexing happens once, here, and the tokens are handed to the parser rather
  // than produced again: macros have to be gathered from every file before any
  // is parsed, so the whole compilation is tokenised whatever else happens.
  //
  // Files do not refer to one another at this stage — a token stream is a
  // function of one buffer — so they are lexed together where there is more
  // than one core to do it on.
  std::vector<std::vector<Token>> tokens(units.size());
  timer.phase("lex", [&] {
    parallelFor(units.size(), [&](size_t i) {
      tokens[i] = Lexer(sm, diags, units[i].FileID).tokenize();
    });
  });

  // Macros are gathered from every file before any is parsed, so one written
  // in the standard library — or in another module of this package — is in
  // scope wherever it is used, and order of compilation does not decide it.
  // The table is one shared thing built in a fixed order, so this part stays
  // sequential: which of two macros of the same name is the duplicate should
  // not depend on which thread got there first.
  //
  // The definitions are taken out of the stream as they are read, which is
  // what the parser would otherwise have to do again for itself; what it gets
  // handed below is already free of them.
  // A `#type(Macros)` file is a package of its own: it is built ahead of this
  // compilation, for the machine doing the compiling, and run to expand each
  // invocation. None of its names reach here, which is the point — what a
  // macro imports and declares is its own business.
  //
  // A `#macro fn` may also be written in any other file. Those are lifted
  // out of it — the program never sees them — and built into the same
  // package, so where a macro is declared is the author's choice. A library
  // carries its macros the same way, as source, so importing one brings its
  // macros along, built here for the machine doing the expanding.
  MacroPackage macroPackage;
  /// This package's own `#type(Macros)` files, kept for a library's
  /// interface so that whoever imports it gets its macros too.
  std::vector<std::pair<std::string, std::string>> exportedMacroFiles;
  if (!opts.MacroPackage) {
    std::vector<MacroSource> macroSources;
    std::vector<size_t> macroFiles;
    uint64_t macroStamp = 1469598103934665603ull;
    const unsigned errorsBefore = diags.errorCount();
    timer.phase("macro package", [&] {
      for (size_t i = 0; i < units.size(); ++i) {
        if (units[i].IsStdlib)
          continue;
        const SourceFile &file = sm.file(units[i].FileID);
        const bool fromLibrary = units[i].FromLibrary;
        if (declaresMacroPackage(tokens[i])) {
          MacroSource src;
          if (fromLibrary) {
            src.Path = macroStem(units[i].ModuleName, "") + ".rune";
            src.Text = file.Buffer;
            while (!src.Text.empty() && src.Text.back() == '\0')
              src.Text.pop_back();
            src.Origin = "library " + units[i].ModuleName;
          } else {
            src.Path = file.Path;
            src.Origin = file.Path;
            exportedMacroFiles.push_back({units[i].ModuleName, file.Buffer});
          }
          src.Module = macroModuleFor(src.Path);
          stampText(src.Module, macroStamp);
          stampTokens(tokens[i], macroStamp);
          collectProcMacros(tokens[i], diags, macroPackage.Macros, src.Module);
          macroSources.push_back(std::move(src));
          macroFiles.push_back(i);
          continue;
        }
        if (!hasProcMacros(tokens[i]))
          continue;
        // Under its own name when it is this package's file, so what the
        // nested build reports reads as the file the macro was written in.
        MacroSource src;
        src.Path = fromLibrary
                       ? macroStem(units[i].ModuleName, "_lib_macros") + ".rune"
                       : "lifted/" +
                             std::filesystem::path(file.Path).filename().string();
        src.Module = macroModuleFor(src.Path);
        src.Origin = fromLibrary ? "library " + units[i].ModuleName
                                 : file.Path;
        collectProcMacros(tokens[i], diags, macroPackage.Macros, src.Module);
        stampText(src.Module, macroStamp);
        src.Text = liftProcMacros(tokens[i], file.Buffer, file.StartOffset,
                                  macroStamp);
        macroSources.push_back(std::move(src));
      }
    });
    // A macro declared wrongly — twice, or not `pub` — leaves a package
    // that cannot be built.
    if (diags.errorCount() != errorsBefore) {
      diags.statusFail("Build failed.");
      return 1;
    }
    if (macroPackage.Macros.empty()) {
      for (size_t i : macroFiles)
        if (!units[i].FromLibrary)
          diags.warn(SourceRange(), "'{}' says it is a macro package but "
                                    "declares no macros",
                     sm.file(units[i].FileID).Name)
              .note("a macro is a `pub fn` marked `#macro`");
    } else {
      bool built = false;
      timer.phase("macro package", [&] {
        built = buildMacroPackage(diags, opts, macroSources, macroStamp,
                                  macroPackage);
      });
      if (!built) {
        diags.statusFail("Build failed.");
        return 1;
      }
    }
    if (!macroFiles.empty()) {
      // Take them out of this compilation, innermost first so the indices
      // that follow stay put.
      for (auto it = macroFiles.rbegin(); it != macroFiles.rend(); ++it) {
        units.erase(units.begin() + static_cast<long>(*it));
        tokens.erase(tokens.begin() + static_cast<long>(*it));
        // A library's file was counted among the libraries, which come
        // before the user's own modules.
        if (*it < firstStdlibUnit) {
          --firstStdlibUnit;
          --firstUserModule;
          libraryConfigs.erase(libraryConfigs.begin() + static_cast<long>(*it));
        }
      }
      macroOrder.clear();
      for (size_t i = 0; i < units.size(); ++i)
        if (units[i].IsStdlib)
          macroOrder.push_back(i);
      for (size_t i = 0; i < units.size(); ++i)
        if (units[i].FromLibrary)
          macroOrder.push_back(i);
      for (size_t i = firstUserModule; i < units.size(); ++i)
        macroOrder.push_back(i);
    }
  }

  MacroTable macros;
  timer.phase("macros", [&] {
    // A `#macro fn` in a file that is part of the program is a mistake: it
    // belongs in a macro package. Inside one, it is the whole point.
    for (size_t i : macroOrder)
      collectMacros(tokens[i], diags, macros, /*record=*/true,
                    units[i].ModuleName);
  });

  // Parsing one file needs nothing from any other — names are resolved later,
  // by Sema, over the finished modules — so the files go out to the pool and
  // come back in the order they were listed.
  std::vector<std::unique_ptr<Module>> parsed(units.size());
  timer.phase("parse", [&] {
    parallelFor(units.size(), [&](size_t i) {
      Parser parser(sm, diags, units[i].FileID, units[i].ModuleName,
                    std::move(tokens[i]), &macros,
                    macroPackage.usable() ? &macroPackage : nullptr);
      parsed[i] = parser.parseModule();
      parsed[i]->FromLibrary = units[i].FromLibrary;
      parsed[i]->IsStdlib = units[i].IsStdlib;
    });
  });
  // `#Config` decides which declarations exist at all, and is answered here:
  // after parsing, because it is written as an expression, and before anything
  // is collected, so what it rules out is never named, never resolved and
  // never checked. A declaration for another platform may mention types and
  // foreign symbols that exist nowhere on this one.
  {
    const ConfigSet cfg = ConfigSet::forOptions(opts);
    timer.phase("config", [&] {
      for (size_t i = 0; i < parsed.size(); ++i)
        applyConfig(*parsed[i],
                    i < libraryConfigs.size() ? libraryConfigs[i] : cfg, diags);
    });
  }

  for (auto &m : parsed)
    modules.push_back(std::move(m));

  if (opts.Dump == DumpKind::AST) {
    for (size_t i = firstUserModule; i < modules.size(); ++i)
      printAST(*modules[i], sm, std::cout);
    return diags.hadError() ? 1 : 0;
  }

  if (diags.hadError()) {
    diags.statusFail("Build failed.");
    return 1;
  }


  if (opts.Verbose)
    diags.status(fmt("checking {} module(s)", modules.size()));

  TypeContext typeCtx(targetPointerBits(opts));
  Sema sema(sm, diags, typeCtx, opts.Safety, opts.Memory, opts.Dump,
            opts.ZombieStdlib);
  // `extern "C++"` declarations are mangled and sized for the machine being
  // built for: `long` is 32 bits on Windows, `int64_t` is `long` on Linux.
  sema.setCxxTarget(
      cxxTargetFor(targetTripleOf(opts).str(), typeCtx.pointerBits()));
  sema.CStringLiterals = opts.Freestanding || opts.NoStdlib;
  for (const auto &m : modules)
    sema.addModule(m.get());
  timer.phase("check", [&] { sema.check(); });
  if (sema.usesCxx())
    const_cast<CompilerOptions &>(opts).LinkCxx = true;
  if (opts.Memory == MemoryMode::Zombie) {
    timer.within("zombie", sema.zombieMillis());
    const zombie::Stats &zs = zombie::lastStats();
    timer.within("  lower (thread ms)", zs.LowerMs);
    timer.within("  summaries", zs.SummariesMs);
    timer.within("  moves", zs.MovesMs);
    timer.within("  loans", zs.LoansMs);
    timer.within("  infer", zs.InferMs);
    timer.within(("  bodies " + std::to_string(zs.Bodies) + ", skipped " +
                  std::to_string(zs.Skipped)).c_str(), 0);
  }

  if (opts.Dump == DumpKind::Symbols) {
    sema.dumpSymbols(std::cout);
    return diags.hadError() ? 1 : 0;
  }

  // Answered whatever else went wrong: an editor asks in the middle of an
  // edit, and one mistake elsewhere in the file should not cost it the
  // answer about this expression.
  if (!opts.QueryFile.empty())
    return opts.QueryHints ? answerHintQuery(sm, sema.result(), modules, diags, opts)
                           : answerMemberQuery(sm, sema.result(), opts);

  if (diags.hadError()) {
    diags.statusFail("Build failed.");
    return 1;
  }


  // `#type(...)` at the top of a file says what it produces. A file that
  // declares nothing and has a `main` is an executable, which is what the
  // driver already assumed; this makes it possible to say otherwise.
  {
    CompilerOptions &mutableOpts = const_cast<CompilerOptions &>(opts);
    for (const auto &m : modules) {
      if (m->IsStdlib || m->FromLibrary || m->DeclaredOutput.empty())
        continue;
      static const std::map<std::string, OutputKind> kinds = {
          {"Executable", OutputKind::Executable},
          {"Exec", OutputKind::Executable},
          {"Library", OutputKind::Library},
          {"Lib", OutputKind::Library},
          {"Shared", OutputKind::Shared},
          {"Dylib", OutputKind::Shared},
          {"Object", OutputKind::Object},
          {"Obj", OutputKind::Object},
          {"Assembly", OutputKind::Assembly},
          {"Asm", OutputKind::Assembly},
          {"LLVM", OutputKind::LLVMIR},
      };
      // `#type(Macros)` says the file is a macro package, which the compiler
      // has already acted on: those files were built and run before this
      // compilation began, and the package's own build treats them as
      // ordinary source. Either way there is nothing left to decide here.
      if (m->DeclaredOutput == "Macros")
        continue;
      auto it = kinds.find(m->DeclaredOutput);
      if (it == kinds.end()) {
        diags.error(m->DeclaredOutputRange, "unknown output type '{}'",
                    m->DeclaredOutput)
            .note("one of Executable, Library, Shared, Object, Assembly, "
                  "LLVM or Macros — or the short forms Exec, Lib, Dylib, "
                  "Obj, Asm")
            .code(109);
        continue;
      }
      // An explicit flag on the command line still wins: a build script has
      // the last word over a file's own preference.
      if (!opts.OutputKindFromFlag)
        mutableOpts.Output = it->second;
    }
  }

  // `#link` / `#linkpath` at the top of a file name what that file needs. They
  // are gathered here, after parsing, and joined with anything -l and -L asked
  // for; duplicates are dropped so a library named twice is passed once.
  {
    CompilerOptions &mutableOpts = const_cast<CompilerOptions &>(opts);
    auto addUnique = [](std::vector<std::string> &into,
                        const std::vector<std::string> &from) {
      for (const std::string &v : from)
        if (std::find(into.begin(), into.end(), v) == into.end())
          into.push_back(v);
    };
    for (const auto &m : modules) {
      addUnique(mutableOpts.LinkLibraries, m->LinkLibraries);
      addUnique(mutableOpts.LinkPaths, m->LinkPaths);
    }
  }

  if (opts.Output == OutputKind::None) {
    timer.report(std::cerr);
    return 0;
  }

  if (opts.Verbose)
    diags.status("generating code");

  opts.LinksRuneLibraries = !libraryObjects.empty();
  CodeGen cg(sm, diags, typeCtx, sema.result(), opts);
  bool generated = false;
  if (opts.TierReport)
    for (auto &m : modules)
      cg.TierModules.push_back(m.get());
  timer.phase("codegen", [&] { generated = cg.run(); });
  if (!generated) {
    diags.statusFail("Build failed.");
    return 1;
  }
  if (opts.TierReport) {
    if (opts.TierOut) {
      *opts.TierOut = cg.Tiers;
    } else {
      for (const auto &[key, need] : cg.Tiers) {
        size_t a = key.find('|'), b = key.find('|', a + 1);
        std::string owner = key.substr(a + 1, b - a - 1);
        std::string name = key.substr(0, a) + "::" +
                           (owner.empty() ? "" : owner + "::") +
                           key.substr(b + 1);
        std::cout << (need == "bare" ? "bare    " : "hosted  ") << name;
        if (need != "bare")
          std::cout << "  (" << need << ")";
        std::cout << "\n";
      }
    }
    return 0;
  }
  std::unique_ptr<llvm::Module> llvmModule = cg.takeModule();

  std::string outPath = defaultOutputName(opts);

  // Documentation needs nothing from code generation, so it is written here
  // and the compile stops. The format is one `key value` line per record field
  // and a blank line between records, so the generator that reads it can be an
  // ordinary Rune program with a line loop and no parser to speak of.
  if (opts.Output == OutputKind::Docs) {
    std::ofstream out(outPath);
    if (!out) {
      diags.fatal("cannot write '{}'", outPath);
      return 1;
    }
    // Values are single-line: a doc's newlines become `\n` markers that the
    // reader turns back into lines.
    auto text = [](const std::string &v) {
      std::string o;
      for (char c : v) {
        if (c == '\n') o += "\\n";
        else if (c == '\\') o += "\\\\";
        else o += c;
      }
      return o;
    };
    // A signature as it would be written, not as the type system spells it:
    // `fn english(&self) -> String` rather than `@function() -> String`.
    auto signature = [](const FunctionDecl *f) {
      // An `async fn` is documented as it was written: the result the body
      // produces, not the `Future` the compiler wrapped around it.
      std::string sig = (f->IsAsync ? "async fn " : "fn ") + f->Name;
      if (!f->Generics.empty()) {
        sig += "<";
        for (size_t g = 0; g < f->Generics.size(); ++g) {
          if (g) sig += ", ";
          sig += f->Generics[g].Name;
        }
        sig += ">";
      }
      sig += "(";
      bool first = true;
      for (const Param &p : f->Params) {
        if (!first) sig += ", ";
        first = false;
        if (p.IsSelf) {
          if (p.SelfByRef) sig += p.SelfMutable ? "&var self" : "&self";
          else sig += "self";
          continue;
        }
        if (p.IsVariadic) { sig += "..."; continue; }
        sig += p.Name;
        if (p.Ty) sig += ": " + p.Ty->toString();
      }
      sig += ")";
      Type *ret = f->Ty ? f->Ty->result() : nullptr;
      if (f->IsAsync && ret && ret->isNominal() &&
          ret->typeArguments().size() == 1)
        ret = ret->typeArguments()[0];
      if (ret && !ret->isVoid())
        sig += " -> " + ret->toString();
      return sig;
    };

    // The declaration as it would be written: an enum's variants, a struct's
    // fields, a mark's requirements, a class's shape. This is what a reader
    // needs and what a signature alone does not carry.
    auto definition = [&](const Decl *d) -> std::string {
      auto typeOf = [](const FieldDecl *f) {
        return f->Ty ? f->Ty->toString() : std::string("?");
      };
      if (const auto *e = dyn_cast<EnumDecl>(d)) {
        std::string out = "enum " + e->Name + " {\n";
        for (const auto &v : e->Variants) {
          out += "    " + v->Name;
          if (v->Shape == VariantShape::Tuple) {
            // A tuple variant keeps its payload types on `TupleTypes`, not as
            // named fields.
            out += "(";
            for (size_t i = 0; i < v->TupleTypes.size(); ++i) {
              if (i) out += ", ";
              out += v->TupleTypes[i]->Resolved
                         ? v->TupleTypes[i]->Resolved->toString()
                         : std::string("?");
            }
            out += ")";
          } else if (v->Shape == VariantShape::Struct) {
            out += " { ";
            for (size_t i = 0; i < v->Fields.size(); ++i) {
              if (i) out += ", ";
              out += v->Fields[i]->Name + ": " + typeOf(v->Fields[i].get());
            }
            out += " }";
          }
          out += "\n";
        }
        return out + "}";
      }
      if (const auto *st = dyn_cast<StructDecl>(d)) {
        std::string out = "struct " + st->Name + " {\n";
        for (const auto &f : st->Fields)
          out += std::string("    ") + (f->IsPublic ? "pub " : "") + f->Name +
                 ": " + typeOf(f.get()) + "\n";
        return out + "}";
      }
      if (const auto *mk = dyn_cast<MarkDecl>(d)) {
        std::string out = "mark " + mk->Name + " {\n";
        for (const auto &at : mk->AssociatedTypes)
          out += "    type " + at->Name + "\n";
        for (const auto &m : mk->Methods)
          out += "    " + signature(m.get()) +
                 (m->Body ? "   // has a default" : "") + "\n";
        return out + "}";
      }
      if (const auto *c = dyn_cast<ClassDecl>(d)) {
        std::string out = "class " + c->Name;
        if (c->Super) out += " : " + c->Super->Name;
        out += " {\n";
        for (const auto &f : c->Fields)
          out += std::string("    ") + (f->IsWeak ? "weak " : "") +
                 (f->IsPublic ? "pub " : "") + f->Name + ": " +
                 typeOf(f.get()) + "\n";
        for (const auto &m : c->Methods)
          if (m->IsPublic)
            out += "    " + signature(m.get()) + "\n";
        return out + "}";
      }
      return "";
    };

    // The standard library's functions, each with whether a bare-metal
    // program has it — worked out by building the library as one would.
    std::map<std::string, std::string> tiers;
    if (opts.DocsStdlib) {
      CompilerOptions t;
      t.TierReport = true;
      t.TierOut = &tiers;
      t.StdlibDir = opts.StdlibDir;
      t.RunetimeDir = opts.RunetimeDir;
      t.NoColor = true;
      compileWithOptions(t);
    }

    auto record = [&](const char *kind, const Decl *d, const std::string &mod,
                      const std::string &owner, const std::string &sig,
                      const std::string &base) {
      out << "kind " << kind << "\n";
      out << "module " << mod << "\n";
      out << "name " << d->Name << "\n";
      if (!owner.empty()) out << "owner " << owner << "\n";
      if (!sig.empty()) out << "sig " << text(sig) << "\n";
      auto tier = tiers.find(mod + "|" + owner + "|" + d->Name);
      if (tier != tiers.end())
        out << "tier " << (tier->second == "bare" ? std::string("bare")
                                                   : "hosted " + tier->second)
            << "\n";
      if (!base.empty()) out << "base " << base << "\n";
      out << "public " << (d->IsPublic ? 1 : 0) << "\n";
      if (!d->Doc.empty()) out << "doc " << text(d->Doc) << "\n";
      std::string def = definition(d);
      if (!def.empty()) out << "decl " << text(def) << "\n";
      out << "\n";
    };

    // A member record: something that belongs to a declaration rather than to
    // the module. `sig` is the member as it would be written.
    auto member = [&](const char *kind, const Decl *d, const std::string &mod,
                      const std::string &owner, const std::string &sig) {
      out << "kind " << kind << "\n";
      out << "module " << mod << "\n";
      out << "name " << d->Name << "\n";
      out << "owner " << owner << "\n";
      if (!sig.empty()) out << "sig " << text(sig) << "\n";
      out << "public " << (d->IsPublic ? 1 : 0) << "\n";
      if (!d->Doc.empty()) out << "doc " << text(d->Doc) << "\n";
      out << "\n";
    };

    // One variant, spelled the way it was declared: a unit name, a tuple of
    // payload types, or a braced shape.
    auto variantSig = [&](const EnumVariantDecl *v) {
      std::string sig = v->Name;
      if (v->Shape == VariantShape::Tuple) {
        sig += "(";
        for (size_t i = 0; i < v->TupleTypes.size(); ++i) {
          if (i) sig += ", ";
          sig += v->TupleTypes[i]->Resolved
                     ? v->TupleTypes[i]->Resolved->toString()
                     : std::string("?");
        }
        sig += ")";
      } else if (v->Shape == VariantShape::Struct) {
        sig += " { ";
        for (size_t i = 0; i < v->Fields.size(); ++i) {
          if (i) sig += ", ";
          sig += v->Fields[i]->Name + ": " +
                 (v->Fields[i]->Ty ? v->Fields[i]->Ty->toString()
                                   : std::string("?"));
        }
        sig += " }";
      }
      return sig;
    };

    auto fieldSig = [&](const FieldDecl *f) {
      std::string sig;
      if (f->IsWeak) sig += "weak ";
      if (f->IsPublic) sig += "pub ";
      sig += f->Name + ": " +
             (f->Ty ? f->Ty->toString() : std::string("?"));
      return sig;
    };

    // Every member a type carries, in the order it was written. Fields and
    // variants describe the shape; a reader needs them beside the methods,
    // not buried inside a wall of code.
    auto members = [&](const Decl *d, const std::string &mod) {
      if (const auto *e = dyn_cast<EnumDecl>(d))
        for (const auto &v : e->Variants)
          member("variant", v.get(), mod, d->Name, variantSig(v.get()));
      if (const auto *st = dyn_cast<StructDecl>(d))
        for (const auto &f : st->Fields)
          member("field", f.get(), mod, d->Name, fieldSig(f.get()));
      if (const auto *c = dyn_cast<ClassDecl>(d))
        for (const auto &f : c->Fields)
          member("field", f.get(), mod, d->Name, fieldSig(f.get()));
      if (const auto *mk = dyn_cast<MarkDecl>(d))
        for (const auto &at : mk->AssociatedTypes)
          member("assoc", at.get(), mod, d->Name, "type " + at->Name);
    };

    // Macros are taken out of the token stream before anything is parsed, so
    // they are not declarations and cannot be walked with the rest. They are
    // still public API — `vec!` and `assert!` are — so they are written out
    // from the table the expansion pass built.
    // The standard library's own modules are documented on request — that
    // is what `rune doc std::io` reads — and otherwise stay out, since a
    // package's reference is about the package.
    const size_t firstDocModule =
        opts.DocsStdlib ? firstStdlibUnit : firstUserModule;
    auto macroBelongsHere = [&](const MacroDef &def) {
      if (def.Module.empty())
        return false;
      for (size_t i = firstDocModule; i < modules.size(); ++i)
        if (modules[i]->Name == def.Module)
          return !modules[i]->FromLibrary;
      return false;
    };

    for (size_t i = firstDocModule; i < modules.size(); ++i) {
      Module *m = modules[i].get();
      out << "kind module\nname " << m->Name << "\n\n";
      for (const auto &entry : macros) {
        const MacroDef &def = entry.second;
        if (def.Module != m->Name || !macroBelongsHere(def))
          continue;
        out << "kind macro\n";
        out << "module " << m->Name << "\n";
        out << "name " << def.Name << "\n";
        out << "sig " << text(def.Name + "!(...)") << "\n";
        out << "public " << (def.IsPublic ? 1 : 0) << "\n";
        if (!def.Doc.empty()) out << "doc " << text(def.Doc) << "\n";
        out << "\n";
      }
      // The name a method's page is filed under. A method may be written in
      // the type's body or in an `extend` somewhere else entirely, and a
      // reader looking for it does not care which.
      auto ownerName = [](Type *t) -> std::string {
        if (!t)
          return "";
        Type *bare = t;
        while (bare->is(TypeKind::Pointer))
          bare = bare->pointee();
        if (bare->isNominal() && bare->nominal())
          return static_cast<Decl *>(bare->nominal())->Name;
        return bare->toString();
      };

      for (const auto &d : m->Decls) {
        Decl *decl = d.get();
        if (auto *f = dyn_cast<FunctionDecl>(decl)) {
          record("fn", decl, m->Name, "", signature(f), "");
        } else if (auto *c = dyn_cast<ClassDecl>(decl)) {
          record("class", decl, m->Name, "", "",
                 c->Super ? c->Super->Name : std::string());
          members(decl, m->Name);
          for (const auto &meth : c->Methods)
            record("method", meth.get(), m->Name, c->Name,
                   signature(meth.get()), "");
        } else if (isa<StructDecl>(decl) || isa<EnumDecl>(decl) ||
                   isa<MarkDecl>(decl)) {
          record(isa<StructDecl>(decl) ? "struct"
                 : isa<EnumDecl>(decl) ? "enum" : "mark",
                 decl, m->Name, "", "", "");
          members(decl, m->Name);
          // A struct, an enum and a mark may all carry methods in their own
          // body, exactly as a class does.
          for (const auto &meth : static_cast<NominalDecl *>(decl)->Methods)
            record("method", meth.get(), m->Name, decl->Name,
                   signature(meth.get()), "");
        } else if (auto *e = dyn_cast<ExtendDecl>(decl)) {
          // `extend` is how a type gains methods after the fact, and for a
          // struct or an enum it is usually where *all* of them are.
          std::string owner = ownerName(e->ResolvedTarget);
          if (!owner.empty())
            for (const auto &meth : e->Methods)
              record("method", meth.get(), m->Name, owner,
                     signature(meth.get()), "");
        } else if (auto *g = dyn_cast<GlobalVarDecl>(decl)) {
          std::string sig = g->IsMutable ? "global var " : "global ";
          sig += g->Name;
          if (g->Ty) sig += ": " + g->Ty->toString();
          record("global", decl, m->Name, "", sig, "");
        } else if (auto *a = dyn_cast<TypeAliasDecl>(decl)) {
          std::string sig = "type " + a->Name;
          if (!a->Generics.empty()) {
            sig += "<";
            for (size_t gi = 0; gi < a->Generics.size(); ++gi) {
              if (gi) sig += ", ";
              sig += a->Generics[gi].Name;
            }
            sig += ">";
          }
          // What an alias stands for is only resolved when something uses it
          // as a type, and an alias nobody used still has to be documented —
          // so the written form is what is reported.
          if (a->Resolved && !a->Resolved->isError())
            sig += " = " + a->Resolved->toString();
          else if (const auto *named =
                       dyn_cast<NamedTypeRepr>(a->Aliased.get())) {
            std::string spelt;
            for (size_t pi = 0; pi < named->Path.size(); ++pi) {
              if (pi) spelt += "::";
              spelt += named->Path[pi];
            }
            if (!spelt.empty()) sig += " = " + spelt;
          }
          record("alias", decl, m->Name, "", sig, "");
        } else if (auto *b = dyn_cast<BindDecl>(decl)) {
          // What a type carries is part of what it is, so a binding is a
          // public structure in its own right: it is how a reader learns
          // that `Value` prints, converts, or can be subscripted.
          std::string mark;
          for (size_t pi = 0; pi < b->MarkPath.size(); ++pi) {
            if (pi) mark += "::";
            // `operator::"[]"` keeps its quotes: without them the spelling is
            // not something anyone could type back in.
            const std::string &part = b->MarkPath[pi];
            bool spellable = !part.empty();
            for (char ch : part)
              if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_')
                spellable = false;
            mark += spellable ? part : "\"" + part + "\"";
          }
          if (mark.empty()) continue;
          // `As<Fahrenheit>` and `As<Kelvin>` are different bindings, and the
          // path alone does not say which.
          if (b->ResolvedMark && !b->ResolvedMark->TypeArguments.empty()) {
            mark += "<";
            for (size_t ai = 0; ai < b->ResolvedMark->TypeArguments.size(); ++ai) {
              if (ai) mark += ", ";
              Type *arg = b->ResolvedMark->TypeArguments[ai];
              mark += arg ? arg->toString() : std::string("?");
            }
            mark += ">";
          }
          std::string target = ownerName(b->ResolvedTarget);
          if (target.empty() && b->TargetType) target = "?";
          std::string sig = "bind " + mark + " to " + target;
          // Recorded against the target rather than the module, so it lands
          // beneath the type it says something about.
          out << "kind bind\n";
          out << "module " << m->Name << "\n";
          out << "name " << mark << "\n";
          out << "owner " << target << "\n";
          out << "sig " << text(sig) << "\n";
          out << "public " << (b->IsPublic ? 1 : 0) << "\n";
          if (!b->Doc.empty()) out << "doc " << text(b->Doc) << "\n";
          out << "\n";
          // The methods inside are the mark's requirements answered, and the
          // mark is where they are documented. What belongs here is the fact
          // of the binding.
        }
      }
    }
    return 0;
  }

  if (opts.Output == OutputKind::LLVMIR) {
    std::error_code ec;
    llvm::raw_fd_ostream out(outPath, ec, llvm::sys::fs::OF_Text);
    if (ec) {
      diags.fatal("cannot write '{}'", outPath).note(ec.message().c_str());
      return 1;
    }
    llvmModule->print(out, nullptr);
    return 0;
  }

  // One place where the back end is asked for machine code, so `--time` sees
  // it whichever of the outputs below asked.
  auto emitMachineCode = [&](const std::string &path, bool assembly) {
    bool ok = false;
    timer.phase("machine code",
                [&] { ok = writeMachineCode(*llvmModule, path, opts, diags,
                                            assembly); });
    return ok;
  };

  if (opts.Output == OutputKind::Assembly) {
    bool ok = emitMachineCode(outPath, true);
    timer.report(std::cerr);
    return ok ? 0 : 1;
  }

  if (opts.Output == OutputKind::Object) {
    bool ok = emitMachineCode(outPath, false);
    timer.report(std::cerr);
    return ok ? 0 : 1;
  }

  // Everything else needs an object file first.
  std::filesystem::path objPath =
      std::filesystem::path(outPath).replace_extension(".o");
  if (opts.Output == OutputKind::Library)
    objPath = std::filesystem::path(outPath).replace_extension(".rul.o");
  if (opts.Output == OutputKind::Shared)
    objPath = std::filesystem::path(outPath).string() + ".o";
  if (!emitMachineCode(objPath.string(), false)) {
    diags.statusFail("Build failed.");
    return 1;
  }

  if (opts.Output == OutputKind::Library) {
    // A .rul bundles the object code with the module's own source, which is
    // what importers re-parse for public signatures and generic bodies.
    std::vector<std::pair<std::string, std::string>> units;
    for (size_t i = firstUserModule; i < modules.size(); ++i)
      units.emplace_back(modules[i]->Name, sm.file(modules[i]->FileID).Buffer);
    // Its macro files too, which were never modules of the program: an
    // importer builds them into its own macro package, so a library's
    // macros are exported the way its functions are.
    for (const auto &file : exportedMacroFiles)
      units.push_back(file);
    bool ok = false;
    timer.phase("archive", [&] {
      ok = writeLibrary(outPath, objPath.string(), opts.ModuleName, units,
                        opts.Memory, opts, diags);
    });
    if (!ok)
      return 1;
    std::error_code ec;
    std::filesystem::remove(objPath, ec);
    timer.report(std::cerr);
    return 0;
  }

  bool linked = false;
  const bool shared = opts.Output == OutputKind::Shared;
  timer.phase("link", [&] {
    linked = linkExecutable(objPath.string(), libraryObjects, outPath, opts,
                            diags, shared);
  });
  if (!linked) {
    diags.statusFail("Build failed.");
    return 1;
  }
  std::error_code ec;
  std::filesystem::remove(objPath, ec);
  for (const std::string &extra : libraryObjects)
    std::filesystem::remove(extra, ec);
  timer.report(std::cerr);
  return 0;
}

} // namespace rune
