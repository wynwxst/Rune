#include "rune/Driver.h"

#include "rune/Diagnostics.h"
#include "rune/Lexer.h"
#include "rune/Source.h"

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

MODULES AND LINKING
    -I <dir>             Add <dir> to the module search path
    -L <dir>             Add <dir> to the native library search path
    -l <name>            Link against native library <name>
    --module <name>      Set the module name (default: first input's stem)
    --cfg <name>         Set <name> for `@Config(...)` conditions
    --no-stdlib          Do not implicitly import the standard library
    --stdlib <dir>       Override the standard library location

CROSS COMPILATION
    --cc <program>       C toolchain driver used to link (default: cc)
    --sysroot <dir>      Pass --sysroot=<dir> to the link driver
    --link-arg <arg>     Append <arg> to the link command verbatim
    --runtime-dir <dir>  Where this target's libruneruntime.a lives

DIAGNOSTICS
    --color / --no-color Force colour on or off
    -Werror              Treat warnings as errors
    -w                   Suppress warnings
    --error-limit <n>    Stop after <n> errors (default 20, 0 = unlimited)

INSPECTION
    --dump-tokens        Print the token stream
    --dump-ast           Print the parse tree
    --dump-symbols       Print the symbol table
    --dump-types         Print inferred types for every expression
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

std::string stemOf(const std::string &path) {
  size_t slash = path.find_last_of("/\\");
  std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
  size_t dot = base.find_last_of('.');
  return dot == std::string::npos ? base : base.substr(0, dot);
}

} // namespace

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
    if (a == "--safety") {
      std::string v = needsValue(i, "--safety");
      if (!parseSafety(v, opts.Safety)) {
        std::cerr << "runec: unknown safety level '" << v
                  << "' (expected none, minimal or full)\n";
        return 2;
      }
      continue;
    }
    if (a == "--overflow-checks") { opts.Overflow = OverflowChecks::On; continue; }
    if (a == "--no-overflow-checks") { opts.Overflow = OverflowChecks::Off; continue; }
    if (a == "-I") { opts.ImportPaths.push_back(needsValue(i, "-I")); continue; }
    if (a == "-L") { opts.LinkPaths.push_back(needsValue(i, "-L")); continue; }
    if (a == "-l") { opts.LinkLibraries.push_back(needsValue(i, "-l")); continue; }
    if (a.rfind("-I", 0) == 0 && a.size() > 2) { opts.ImportPaths.push_back(a.substr(2)); continue; }
    if (a.rfind("-L", 0) == 0 && a.size() > 2) { opts.LinkPaths.push_back(a.substr(2)); continue; }
    if (a.rfind("-l", 0) == 0 && a.size() > 2) { opts.LinkLibraries.push_back(a.substr(2)); continue; }
    if (a == "--module") { opts.ModuleName = needsValue(i, "--module"); continue; }
    if (a == "--cfg") { opts.ConfigFlags.push_back(needsValue(i, "--cfg")); continue; }
    if (a == "--no-stdlib") { opts.NoStdlib = true; continue; }
    if (a == "--stdlib") { opts.StdlibDir = needsValue(i, "--stdlib"); continue; }
    if (a == "--runtime-dir") {
      opts.RuntimeLibDir = needsValue(i, "--runtime-dir");
      continue;
    }
    if (a == "--color") { opts.ForceColor = true; continue; }
    if (a == "--no-color") { opts.NoColor = true; continue; }
    if (a == "-Werror") { opts.WarningsAsErrors = true; continue; }
    if (a == "-w") { opts.NoWarnings = true; continue; }
    if (a == "--error-limit") { opts.ErrorLimit = static_cast<unsigned>(atoi(needsValue(i, "--error-limit"))); continue; }
    if (a == "--dump-tokens") { opts.Dump = DumpKind::Tokens; continue; }
    if (a == "--dump-ast") { opts.Dump = DumpKind::AST; continue; }
    if (a == "--dump-symbols") { opts.Dump = DumpKind::Symbols; continue; }
    if (a == "--dump-types") { opts.Dump = DumpKind::Types; continue; }
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
