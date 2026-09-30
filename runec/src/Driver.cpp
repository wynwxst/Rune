#include "rune/Driver.h"

#include "rune/Diagnostics.h"
#include "rune/Lexer.h"
#include "rune/Source.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

namespace rune {

namespace {

void printUsage(std::ostream &os) {
  os << R"(runec - the Rune compiler

USAGE
    runec [options] <input.rune>...

OUTPUT
    -o <path>            Write output to <path>
    -c                   Emit an object file (.o) and stop
    --emit-llvm          Emit textual LLVM IR (.ll)
    --emit-asm           Emit target assembly (.s)
    --emit-lib           Emit a Rune library (.rul)
    --shared             Emit a native shared library (.dylib/.so/.dll)
    --emit-docs          Emit a documentation sidecar (.rdoc)
    --docs-stdlib        With --emit-docs: cover the standard library's modules
    --check              Type-check only; produce no output

CODE GENERATION
    -O0 -O1 -O2 -O3      Optimisation level (default -O0)
    -g                   Emit debug information
    --target <triple>    Cross-compile for <triple>
    --safety <level>     none | minimal | full   (default full)
    --overflow-checks    Trap when integer arithmetic overflows (default at -O0)
    --no-overflow-checks Wrap instead (default at -O1 and above)
    --memory <mode>      zombie | arc   (default zombie): single ownership
                         proven by the Zombie borrow checker, or reference
                         counting
    --no-zombie-stdlib   Silence Zombie findings inside the standard library
                         (they are reported by default)

MODULES AND LINKING
    -I <dir>             Add <dir> to the module search path
    -L <dir>             Add <dir> to the native library search path
    -l <name>            Link against native library <name>
    --module <name>      Set the module name (default: first input's stem)
    --cfg <name>         Set <name> for `@Config(...)` conditions
    --cfg <key>=<value>  Give <key> a value, compared with `@Config(k == v)`
    --no-stdlib          Do not implicitly import the standard library
    --stdlib <dir>       Override the standard library location

CROSS COMPILATION
    --cc <program>       C toolchain driver used to link (default: cc)
    --sysroot <dir>      Pass --sysroot=<dir> to the link driver
    --link-arg <arg>     Append <arg> to the link command verbatim
    --link-cxx           Link the C++ runtime (implied by `extern "C++"`)
    --runtime-dir <dir>  Where this target's libruneruntime.a lives

DIAGNOSTICS
    --color / --no-color Force colour on or off
    --source <path>=<file>
                         Compile <path> as the text in <file> (an editor's
                         unsaved buffer), reporting it as <path>
    --query-members <file>:<start>:<end>
                         After checking, print the type and members of the
                         expression at those byte offsets, as JSON
    --query-hints <file>
                         After checking, print as JSON what could be written
                         out in the file: each unannotated binding's type and
                         each positional argument's parameter name
    --diagnostic-format <fmt>
                         human | json | short   (default human): json writes
                         one object per line on stderr, for editors and
                         tools; short one `file:line:col: error: message`
    -Werror              Treat warnings as errors
    -w                   Suppress warnings
    --error-limit <n>    Stop after <n> errors (default 20, 0 = unlimited)

INSPECTION
    --dump-tokens        Print the token stream
    --dump-ast           Print the parse tree
    --dump-symbols       Print the symbol table
    --dump-types         Print inferred types for every expression
    --dump-zombie        Print the borrow checker's view of every body
    -v, --verbose        Report each pipeline stage
    --time               Report how long each stage took
    --version            Print the version and exit
    -h, --help           Print this message

ENVIRONMENT
    RUNE_JOBS            Threads to lex and parse with (default: cores)
    RUNE_CC              The link driver, when --cc does not name one
)";
}

bool parseSafety(const std::string &s, SafetyLevel &out) {
  if (s == "none") { out = SafetyLevel::None; return true; }
  if (s == "minimal") { out = SafetyLevel::Minimal; return true; }
  if (s == "full") { out = SafetyLevel::Full; return true; }
  return false;
}

bool parseMemory(const std::string &s, MemoryMode &out) {
  if (s == "arc") { out = MemoryMode::Arc; return true; }
  if (s == "zombie") { out = MemoryMode::Zombie; return true; }
  return false;
}

std::string stemOf(const std::string &path) {
  size_t slash = path.find_last_of("/\\");
  std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
  size_t dot = base.find_last_of('.');
  return dot == std::string::npos ? base : base.substr(0, dot);
}

} // namespace

const char *memoryModeName(MemoryMode mode) {
  return mode == MemoryMode::Zombie ? "zombie" : "arc";
}

int runCompilerMain(int argc, char **argv) {
  CompilerOptions opts;
  opts.StdlibDir = RUNE_DEFAULT_STDLIB_DIR;
  opts.RuntimeLibDir = RUNE_RUNTIME_LIB_DIR;

  auto needsValue = [&](int &i, const char *flag) -> const char * {
    if (i + 1 >= argc) {
      std::cerr << "runec: option '" << flag << "' requires a value\n";
      exit(2);
    }
    return argv[++i];
  };

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "-h" || a == "--help") { printUsage(std::cout); return 0; }
    if (a == "--version") {
      std::cout << "runec " << RUNE_VERSION_STRING << "\n";
      return 0;
    }
    if (a == "-o") { opts.OutputPath = needsValue(i, "-o"); continue; }
    if (a == "-c") { opts.Output = OutputKind::Object;
                      opts.OutputKindFromFlag = true; continue; }
    if (a == "--emit-llvm") { opts.Output = OutputKind::LLVMIR;
                      opts.OutputKindFromFlag = true; continue; }
    if (a == "--emit-asm") { opts.Output = OutputKind::Assembly;
                      opts.OutputKindFromFlag = true; continue; }
    if (a == "--emit-lib") { opts.Output = OutputKind::Library;
                      opts.OutputKindFromFlag = true; continue; }
    if (a == "--shared") { opts.Output = OutputKind::Shared;
                      opts.OutputKindFromFlag = true; continue; }
    if (a == "--emit-docs") { opts.Output = OutputKind::Docs;
                      opts.OutputKindFromFlag = true; continue; }
    if (a == "--check") { opts.Output = OutputKind::None;
                      opts.OutputKindFromFlag = true; continue; }
    if (a == "-g") { opts.DebugInfo = true; continue; }
    if (a.rfind("-O", 0) == 0 && a.size() == 3 && a[2] >= '0' && a[2] <= '3') {
      opts.OptLevel = static_cast<unsigned>(a[2] - '0');
      continue;
    }
    if (a == "--target") { opts.TargetTriple = needsValue(i, "--target"); continue; }
    if (a == "--cc") { opts.LinkDriver = needsValue(i, "--cc"); continue; }
    if (a == "--sysroot") { opts.Sysroot = needsValue(i, "--sysroot"); continue; }
    if (a == "--link-arg") {
      opts.LinkArgs.push_back(needsValue(i, "--link-arg"));
      continue;
    }
    if (a == "--link-cxx") { opts.LinkCxx = true; continue; }
    if (a == "--safety") {
      std::string v = needsValue(i, "--safety");
      if (!parseSafety(v, opts.Safety)) {
        std::cerr << "runec: unknown safety level '" << v
                  << "' (expected none, minimal or full)\n";
        return 2;
      }
      continue;
    }
    if (a == "--memory") {
      std::string v = needsValue(i, "--memory");
      if (!parseMemory(v, opts.Memory)) {
        std::cerr << "runec: unknown memory mode '" << v
                  << "' (expected arc or zombie)\n";
        return 2;
      }
      continue;
    }
    if (a == "--zombie-stdlib") { opts.ZombieStdlib = true; continue; }
    if (a == "--no-zombie-stdlib") { opts.ZombieStdlib = false; continue; }
    if (a == "--overflow-checks") { opts.Overflow = OverflowChecks::On; continue; }
    if (a == "--no-overflow-checks") { opts.Overflow = OverflowChecks::Off; continue; }
    if (a == "-I") { opts.ImportPaths.push_back(needsValue(i, "-I")); continue; }
    if (a == "-L") { opts.LinkPaths.push_back(needsValue(i, "-L")); continue; }
    if (a == "-l") { opts.LinkLibraries.push_back(needsValue(i, "-l")); continue; }
    if (a.rfind("-I", 0) == 0 && a.size() > 2) { opts.ImportPaths.push_back(a.substr(2)); continue; }
    if (a.rfind("-L", 0) == 0 && a.size() > 2) { opts.LinkPaths.push_back(a.substr(2)); continue; }
    if (a.rfind("-l", 0) == 0 && a.size() > 2) { opts.LinkLibraries.push_back(a.substr(2)); continue; }
    if (a == "--module") { opts.ModuleName = needsValue(i, "--module"); continue; }
    if (a == "--cfg") {
      std::string v = needsValue(i, "--cfg");
      // `--cfg name` sets a name; `--cfg key=value` gives a key a value.
      size_t eq = v.find('=');
      if (eq == std::string::npos)
        opts.ConfigFlags.push_back(v);
      else
        opts.ConfigValues.push_back({v.substr(0, eq), v.substr(eq + 1)});
      continue;
    }
    if (a == "--no-stdlib") { opts.NoStdlib = true; continue; }
    if (a == "--stdlib") { opts.StdlibDir = needsValue(i, "--stdlib"); continue; }
    if (a == "--runtime-dir") {
      opts.RuntimeLibDir = needsValue(i, "--runtime-dir");
      continue;
    }
    if (a == "--color") { opts.ForceColor = true; continue; }
    if (a == "--no-color") { opts.NoColor = true; continue; }
    if (a == "--source") {
      std::string v = needsValue(i, "--source");
      size_t eq = v.find('=');
      if (eq == std::string::npos || eq == 0 || eq + 1 == v.size()) {
        std::cerr << "runec: --source takes <path>=<file>\n";
        return 2;
      }
      opts.SourceOverrides[v.substr(0, eq)] = v.substr(eq + 1);
      continue;
    }
    if (a == "--query-members") {
      // <file>:<start>:<end>, split from the right: a path may hold a `:`.
      std::string v = needsValue(i, "--query-members");
      size_t b = v.rfind(':');
      size_t a2 = b == std::string::npos ? b : v.rfind(':', b - 1);
      if (a2 == std::string::npos || a2 == 0) {
        std::cerr << "runec: --query-members takes <file>:<start>:<end>\n";
        return 2;
      }
      opts.QueryFile = v.substr(0, a2);
      opts.QueryStart = static_cast<uint32_t>(std::strtoul(v.c_str() + a2 + 1, nullptr, 10));
      opts.QueryEnd = static_cast<uint32_t>(std::strtoul(v.c_str() + b + 1, nullptr, 10));
      opts.Output = OutputKind::None;
      opts.OutputKindFromFlag = true;
      continue;
    }
    if (a == "--query-hints") {
      opts.QueryFile = needsValue(i, "--query-hints");
      opts.QueryHints = true;
      opts.Output = OutputKind::None;
      opts.OutputKindFromFlag = true;
      continue;
    }
    if (a == "--diagnostic-format") {
      std::string v = needsValue(i, "--diagnostic-format");
      opts.JsonDiagnostics = v == "json";
      opts.ShortDiagnostics = v == "short";
      if (v != "json" && v != "human" && v != "short") {
        std::cerr << "runec: --diagnostic-format takes 'human', 'json' or 'short', not '"
                  << v << "'\n";
        return 2;
      }
      continue;
    }
    if (a == "-Werror") { opts.WarningsAsErrors = true; continue; }
    if (a == "-w") { opts.NoWarnings = true; continue; }
    if (a == "--error-limit") { opts.ErrorLimit = static_cast<unsigned>(atoi(needsValue(i, "--error-limit"))); continue; }
    if (a == "--dump-tokens") { opts.Dump = DumpKind::Tokens; continue; }
    if (a == "--dump-ast") { opts.Dump = DumpKind::AST; continue; }
    if (a == "--dump-symbols") { opts.Dump = DumpKind::Symbols; continue; }
    if (a == "--dump-types") { opts.Dump = DumpKind::Types; continue; }
    if (a == "--dump-zombie") { opts.Dump = DumpKind::Zombie; continue; }
    if (a == "-v" || a == "--verbose") { opts.Verbose = true; continue; }
    if (a == "--time") { opts.TimeReport = true; continue; }
    if (a == "--docs-stdlib") { opts.DocsStdlib = true; continue; }
    if (!a.empty() && a[0] == '-') {
      std::cerr << "runec: unknown option '" << a << "'\n"
                << "       run `runec --help` for the list of options\n";
      return 2;
    }
    opts.Inputs.push_back(a);
  }

  // Documenting the standard library needs no input of its own: the
  // library is read whether or not anything imports it.
  if (opts.Inputs.empty() && !(opts.DocsStdlib && opts.Output == OutputKind::Docs)) {
    std::cerr << "runec: no input files\n";
    printUsage(std::cerr);
    return 2;
  }
  if (opts.ModuleName.empty())
    opts.ModuleName = opts.Inputs.empty() ? "std" : stemOf(opts.Inputs.front());

  return compileWithOptions(opts);
}

} // namespace rune
