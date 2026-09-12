//===- Compilation.cpp - Pipeline orchestration ----------------*- C++ -*-===//

#include "rune/Driver.h"

#include <fstream>

#include "rune/AST.h"
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

#include <llvm/IR/LegacyPassManager.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Host.h>

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <map>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern "C" char **environ;

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
  initialiseTargets();

  const llvm::Triple triple = targetTripleOf(opts);
  std::string err;
  if (const llvm::Target *t = llvm::TargetRegistry::lookupTarget(triple, err)) {
    llvm::TargetOptions targetOpts;
    std::unique_ptr<llvm::TargetMachine> tm(t->createTargetMachine(
        triple, "generic", "", targetOpts,
        std::optional<llvm::Reloc::Model>(llvm::Reloc::PIC_)));
    if (tm)
      return tm->createDataLayout().getPointerSizeInBits();
  }
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
  case OutputKind::Docs: return stem + ".rdoc";
  // A PE image is only executable with the suffix, and a cross build is
  // usually copied to the machine it runs on rather than run in place.
  default:
    return targetTripleOf(opts).isOSWindows() ? stem + ".exe" : stem;
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

/// Lowers the module to a native object or assembly file.
bool writeMachineCode(llvm::Module &m, const std::string &path,
                      const CompilerOptions &opts, DiagnosticEngine &diags,
                      bool assembly) {
  initialiseTargets();

  std::string tripleStr = opts.TargetTriple.empty()
                              ? llvm::sys::getDefaultTargetTriple()
                              : opts.TargetTriple;
  // See CodeGen: an un-normalised alias parses as an unknown OS.
  llvm::Triple triple(llvm::Triple::normalize(tripleStr));
  std::string err;
  const llvm::Target *target = llvm::TargetRegistry::lookupTarget(triple, err);
  if (!target) {
    diags.fatal("no backend for target '{}'", tripleStr).note(err.c_str());
    return false;
  }

  llvm::TargetOptions targetOpts;
  auto rm = std::optional<llvm::Reloc::Model>(llvm::Reloc::PIC_);
  // The back end runs its own optimisation pipeline, and left to itself it
  // picks the `-O2` one whatever `-O` asked for. A debug build then pays for
  // scheduling and register-allocation work it did not want, which is most of
  // what an unoptimised compile spends its time on.
  std::unique_ptr<llvm::TargetMachine> tm(target->createTargetMachine(
      triple, "generic", "", targetOpts, rm,
      std::optional<llvm::CodeModel::Model>(), codeGenOptLevel(opts.OptLevel)));
  m.setDataLayout(tm->createDataLayout());
  m.setTargetTriple(triple);

  std::error_code ec;
  llvm::raw_fd_ostream out(path, ec, llvm::sys::fs::OF_None);
  if (ec) {
    diags.fatal("cannot write '{}'", path).note(ec.message().c_str());
    return false;
  }
  llvm::legacy::PassManager pm;
  auto kind = assembly ? llvm::CodeGenFileType::AssemblyFile
                       : llvm::CodeGenFileType::ObjectFile;
  if (tm->addPassesToEmitFile(pm, out, nullptr, kind)) {
    diags.fatal("this target cannot emit {} files", assembly ? "assembly" : "object");
    return false;
  }
  pm.run(m);
  out.flush();
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
bool linkExecutable(const std::string &objPath,
                    const std::vector<std::string> &extraObjects,
                    const std::string &outPath, const CompilerOptions &opts,
                    DiagnosticEngine &diags) {
  // A cross build names its own toolchain driver; that driver already knows
  // its target's libc, startup files and linker, so nothing else has to be
  // taught the platform. Falling back: $RUNE_CC, then the host's `cc`.
  std::string cc = opts.LinkDriver;
  if (cc.empty()) {
    if (const char *env = getenv("RUNE_CC"))
      cc = env;
    else
      cc = "cc";
  }

  std::vector<std::string> argv{cc, objPath};
  // Only a driver that was not chosen for the target needs telling. A
  // `<triple>-gcc` is already the right compiler and rejects `--target`;
  // clang is one binary for every target and needs it.
  if (!opts.TargetTriple.empty() && opts.LinkDriver.empty())
    argv.push_back("--target=" + opts.TargetTriple);
  if (!opts.Sysroot.empty())
    argv.push_back("--sysroot=" + opts.Sysroot);
  for (const std::string &extra : extraObjects)
    argv.push_back(extra);
  argv.push_back("-o");
  argv.push_back(outPath);
  std::filesystem::path runtimeLib =
      std::filesystem::path(opts.RuntimeLibDir) / "libruneruntime.a";
  if (std::filesystem::exists(runtimeLib))
    argv.push_back(runtimeLib.string());
  else
    diags.fatal("cannot find the Rune runtime at '{}'", runtimeLib.string())
        .note("build the `runeruntime` target, or pass --runtime-dir");

  const llvm::Triple triple = targetTripleOf(opts);
  const bool isWindows = triple.isOSWindows();

  // A debug build exports its symbols so a traceback can name the frames it
  // walks; without this the dynamic linker can only resolve the exported ones.
  // A PE image exports through a different mechanism and has no such flag.
  if (opts.DebugInfo && !isWindows)
    argv.push_back("-rdynamic");

  for (const std::string &dir : opts.LinkPaths)
    argv.push_back("-L" + dir);
  for (const std::string &arg : opts.LinkArgs)
    argv.push_back(arg);
  bool wantsMath = false;
  for (const std::string &lib : opts.LinkLibraries) {
    argv.push_back("-l" + lib);
    if (lib == "m")
      wantsMath = true;
  }
  // The standard library calls into libm; only add it if nobody else did, and
  // only where there is one. Windows keeps the math functions in the C
  // runtime and Apple's platforms keep them in libSystem, so on both, asking
  // for `-lm` means the driver goes looking for a library that is not there.
  if (!wantsMath && !isWindows && !triple.isOSDarwin())
    argv.push_back("-lm");

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

} // namespace

int compileWithOptions(const CompilerOptions &opts) {
  PhaseTimer timer(opts.TimeReport);
  SourceManager sm;
  DiagnosticEngine diags(sm);
  diags.detectColor();
  if (opts.ForceColor) diags.setColorEnabled(true);
  if (opts.NoColor) diags.setColorEnabled(false);
  diags.setWarningsAsErrors(opts.WarningsAsErrors);
  diags.setQuietWarnings(opts.NoWarnings);
  diags.setErrorLimit(opts.ErrorLimit);

  std::vector<unsigned> fileIDs;
  for (const std::string &path : opts.Inputs) {
    auto id = sm.loadFile(path);
    if (!id) {
      diags.fatal("cannot open input file '{}'", path)
          .note("check the path and that the file is readable");
      return 1;
    }
    fileIDs.push_back(*id);
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
    for (const auto &libPath : libs) {
      LibraryContents lib;
      if (!readLibrary(libPath.string(), lib, diags)) {
        libraryError = true;
        return;
      }
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
      for (const auto &unit : lib.Interfaces) {
        unsigned id = sm.addBuffer(libPath.string() + " (" + unit.first + ")",
                                   unit.second);
        libraryUnits.push_back({id, unit.first});
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
  const size_t firstStdlibUnit = units.size();
  for (const auto &u : stdlibUnits)
    units.push_back({u.first, u.second, false, /*IsStdlib=*/true});
  const size_t firstUserModule = units.size();
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
  MacroTable macros;
  timer.phase("macros", [&] {
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
                    std::move(tokens[i]), &macros);
      parsed[i] = parser.parseModule();
      parsed[i]->FromLibrary = units[i].FromLibrary;
      parsed[i]->IsStdlib = units[i].IsStdlib;
    });
  });
  // `@Config` decides which declarations exist at all, and is answered here:
  // after parsing, because it is written as an expression, and before anything
  // is collected, so what it rules out is never named, never resolved and
  // never checked. A declaration for another platform may mention types and
  // foreign symbols that exist nowhere on this one.
  {
    const ConfigSet cfg = ConfigSet::forOptions(opts);
    timer.phase("config", [&] {
      for (auto &m : parsed)
        applyConfig(*m, cfg, diags);
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
  for (const auto &m : modules)
    sema.addModule(m.get());
  timer.phase("check", [&] { sema.check(); });
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

  if (diags.hadError()) {
    diags.statusFail("Build failed.");
    return 1;
  }


  // `@type(...)` at the top of a file says what it produces. A file that
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
          {"Object", OutputKind::Object},
          {"Obj", OutputKind::Object},
          {"Assembly", OutputKind::Assembly},
          {"Asm", OutputKind::Assembly},
          {"LLVM", OutputKind::LLVMIR},
      };
      auto it = kinds.find(m->DeclaredOutput);
      if (it == kinds.end()) {
        diags.error(m->DeclaredOutputRange, "unknown output type '{}'",
                    m->DeclaredOutput)
            .note("one of Executable, Library, Object, Assembly or LLVM — or the short forms Exec, Lib, Obj, Asm")
            .code(109);
        continue;
      }
      // An explicit flag on the command line still wins: a build script has
      // the last word over a file's own preference.
      if (!opts.OutputKindFromFlag)
        mutableOpts.Output = it->second;
    }
  }

  // `@link` / `@linkpath` at the top of a file name what that file needs. They
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

  CodeGen cg(sm, diags, typeCtx, sema.result(), opts);
  bool generated = false;
  timer.phase("codegen", [&] { generated = cg.run(); });
  if (!generated) {
    diags.statusFail("Build failed.");
    return 1;
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
      std::string sig = "fn " + f->Name;
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

    auto record = [&](const char *kind, const Decl *d, const std::string &mod,
                      const std::string &owner, const std::string &sig,
                      const std::string &base) {
      out << "kind " << kind << "\n";
      out << "module " << mod << "\n";
      out << "name " << d->Name << "\n";
      if (!owner.empty()) out << "owner " << owner << "\n";
      if (!sig.empty()) out << "sig " << text(sig) << "\n";
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

  // Executable and library both need an object file first.
  std::filesystem::path objPath =
      std::filesystem::path(outPath).replace_extension(".o");
  if (opts.Output == OutputKind::Library)
    objPath = std::filesystem::path(outPath).replace_extension(".rul.o");
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
    bool ok = false;
    timer.phase("archive", [&] {
      ok = writeLibrary(outPath, objPath.string(), opts.ModuleName, units,
                        opts.Memory, diags);
    });
    if (!ok)
      return 1;
    std::error_code ec;
    std::filesystem::remove(objPath, ec);
    timer.report(std::cerr);
    return 0;
  }

  bool linked = false;
  timer.phase("link", [&] {
    linked = linkExecutable(objPath.string(), libraryObjects, outPath, opts,
                            diags);
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
