#include "Manifest.h"
#include "Targets.h"

#include "Toml.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace rune {

namespace {
/// A `[config]` value as `@Config` compares it: a string as itself, a number
/// or a boolean as it prints. Anything else — an array, a table — has no
/// spelling a condition could compare against.
std::string configText(const TomlValue &v) {
  switch (v.K) {
  case TomlValue::Kind::String: return v.Str;
  case TomlValue::Kind::Integer: return std::to_string(v.Int);
  case TomlValue::Kind::Boolean: return v.Bool ? "true" : "false";
  default: return std::string();
  }
}
} // namespace


const TargetSpec *Manifest::findTarget(const std::string &name) const {
  for (const TargetSpec &t : Targets)
    if (t.Name == name)
      return &t;
  return nullptr;
}

namespace {

std::vector<std::string> stringList(const TomlValue *v) {
  std::vector<std::string> out;
  if (!v)
    return out;
  if (v->isString()) {
    out.push_back(v->Str);
    return out;
  }
  if (v->isArray())
    for (const TomlValue &e : v->Arr)
      if (e.isString())
        out.push_back(e.Str);
  return out;
}




/// What a file says it produces, read from the file itself.
///
/// A `@type(...)` decorator is the explicit answer. Failing that, a top-level
/// `fn main` makes the file an executable, because a program with an entry
/// point is a program. Everything else is a component.
static OutputKind kindOfSource(const fs::path &path) {
  std::ifstream in(path);
  if (!in)
    return OutputKind::Component;

  std::string line;
  bool inBlockComment = false;
  bool sawDecl = false;
  while (std::getline(in, line)) {
    // Strip comments so neither a `@type` nor a `fn main` inside one counts.
    std::string code;
    for (size_t i = 0; i < line.size(); ++i) {
      if (inBlockComment) {
        if (line.compare(i, 2, "*/") == 0) { inBlockComment = false; ++i; }
        continue;
      }
      if (line.compare(i, 2, "//") == 0)
        break;
      if (line.compare(i, 2, "/*") == 0) { inBlockComment = true; ++i; continue; }
      code += line[i];
    }

    size_t at = code.find("@type(");
    if (at != std::string::npos && !sawDecl) {
      size_t close = code.find(')', at);
      if (close != std::string::npos) {
        std::string arg = code.substr(at + 6, close - at - 6);
        while (!arg.empty() && isspace((unsigned char)arg.front())) arg.erase(arg.begin());
        while (!arg.empty() && isspace((unsigned char)arg.back())) arg.pop_back();
        if (arg == "Executable" || arg == "Exec") return OutputKind::Executable;
        if (arg == "Library" || arg == "Lib")     return OutputKind::Library;
        if (arg == "Object" || arg == "Obj")      return OutputKind::Object;
        if (arg == "Assembly" || arg == "Asm")    return OutputKind::Assembly;
        if (arg == "LLVM")                        return OutputKind::LLVM;
      }
    }

    // `fn main(` at the start of a line is a top-level definition; one that is
    // indented belongs to something else.
    if (code.compare(0, 8, "fn main(") == 0 ||
        code.compare(0, 12, "pub fn main(") == 0)
      return OutputKind::Executable;

    std::string trimmed = code;
    while (!trimmed.empty() && isspace((unsigned char)trimmed.front()))
      trimmed.erase(trimmed.begin());
    if (!trimmed.empty() && trimmed[0] != '@')
      sawDecl = true;
  }
  return OutputKind::Component;
}


/// The value of `@<name>(...)` among the directives at the top of a file —
/// `@runtime(none)` gives "none" — or empty when the file does not say.
static std::string programDirectiveOf(const fs::path &path,
                                      const std::string &name) {
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    size_t i = line.find_first_not_of(" \t\r");
    if (i == std::string::npos || line.compare(i, 2, "//") == 0)
      continue;
    if (line[i] != '@')
      return ""; // the first declaration: the directives are over
    const std::string open = "@" + name + "(";
    if (line.compare(i, open.size(), open) == 0) {
      size_t close = line.find(')', i);
      if (close == std::string::npos)
        return "";
      std::string arg = line.substr(i + open.size(), close - i - open.size());
      arg.erase(0, arg.find_first_not_of(" \t"));
      arg.erase(arg.find_last_not_of(" \t") + 1);
      return arg;
    }
  }
  return "";
}

void collectRuneFiles(const fs::path &dir, std::vector<std::string> &out) {
  std::error_code ec;
  if (!fs::is_directory(dir, ec))
    return;
  std::vector<fs::path> found;
  for (auto it = fs::recursive_directory_iterator(dir, ec);
       !ec && it != fs::recursive_directory_iterator(); ++it)
    if (it->is_regular_file() && it->path().extension() == ".rune")
      found.push_back(it->path());
  std::sort(found.begin(), found.end());
  for (const auto &p : found)
    out.push_back(p.string());
}

} // namespace

bool loadManifest(const std::string &dir, Manifest &out, std::string &error,
                  bool requireSources) {
  fs::path root(dir);
  fs::path manifestPath = root / "Rune.toml";
  std::ifstream in(manifestPath);
  if (!in) {
    error = "no Rune.toml in '" + root.string() + "'";
    return false;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  TomlDocument doc = parseToml(ss.str());
  if (!doc.ok()) {
    error = manifestPath.string() + ":" + std::to_string(doc.ErrorLine) + ": " +
            doc.Error;
    return false;
  }

  out.Root = fs::absolute(root).lexically_normal().string();

  if (const TomlValue *pkg = doc.get("package")) {
    out.Name = pkg->find("name") ? pkg->find("name")->stringOr(out.Name) : out.Name;
    if (const TomlValue *v = pkg->find("version")) out.Version = v->stringOr(out.Version);
    if (const TomlValue *v = pkg->find("description")) out.Description = v->stringOr("");
    if (const TomlValue *v = pkg->find("license")) out.License = v->stringOr("");
    if (const TomlValue *v = pkg->find("edition")) out.Edition = v->stringOr(out.Edition);
    out.Authors = stringList(pkg->find("authors"));
  } else {
    error = manifestPath.string() + ": missing a [package] section";
    return false;
  }

  if (const TomlValue *build = doc.get("build")) {
    if (const TomlValue *v = build->find("safety")) out.Safety = v->stringOr("full");
    if (const TomlValue *v = build->find("memory")) out.Memory = v->stringOr("zombie");
    if (const TomlValue *v = build->find("emit")) out.Emit = v->stringOr("");
    if (const TomlValue *v = build->find("optimize"))
      out.OptLevel = static_cast<unsigned>(std::min<int64_t>(3, std::max<int64_t>(0, v->intOr(0))));
    if (const TomlValue *v = build->find("debug")) out.Debug = v->boolOr(true);
    if (const TomlValue *v = build->find("overflow-checks"))
      out.OverflowChecks = v->boolOr(true) ? 1 : 0;
    if (const TomlValue *v = build->find("warnings-as-errors"))
      out.WarningsAsErrors = v->boolOr(false);
    if (const TomlValue *v = build->find("no-stdlib")) out.NoStdlib = v->boolOr(false);
    if (const TomlValue *v = build->find("runtime")) {
      const std::string r = v->stringOr("hosted");
      if (r != "hosted" && r != "none") {
        error = manifestPath.string() + ": [build] runtime is 'hosted' or "
                "'none', not '" + r + "'";
        return false;
      }
      out.Freestanding = r == "none";
    }
    if (const TomlValue *v = build->find("entry")) {
      const std::string e = v->stringOr("main");
      if (e != "main" && e != "none") {
        error = manifestPath.string() + ": [build] entry is 'main' or "
                "'none', not '" + e + "'";
        return false;
      }
      out.NoEntry = e == "none";
    }
    out.LinkLibraries = stringList(build->find("link"));
    out.LinkPaths = stringList(build->find("link-paths"));
    out.CSources = stringList(build->find("c-sources"));
    out.CFlags = stringList(build->find("c-flags"));
    out.CxxSources = stringList(build->find("cxx-sources"));
    out.CxxFlags = stringList(build->find("cxx-flags"));
    if (const TomlValue *v = build->find("cxx-standard"))
      out.CxxStandard = v->stringOr("");
    out.ConfigFlags = stringList(build->find("cfg"));
    out.LinkArgs = stringList(build->find("link-args"));
    if (const TomlValue *v = build->find("linker-script")) {
      out.LinkerScript = v->stringOr("");
      if (!out.LinkerScript.empty() && !fs::path(out.LinkerScript).is_absolute())
        out.LinkerScript = (root / out.LinkerScript).lexically_normal().string();
    }
    // A source path is relative to the manifest, like everything else in it.
    for (std::string &p : out.CSources)
      if (!fs::path(p).is_absolute())
        p = (root / p).lexically_normal().string();
    for (std::string &p : out.CxxSources)
      if (!fs::path(p).is_absolute())
        p = (root / p).lexically_normal().string();
    for (std::string &p : out.LinkPaths)
      if (!fs::path(p).is_absolute())
        p = (root / p).lexically_normal().string();
  }

  if (const TomlValue *build = doc.get("build"))
    if (const TomlValue *v = build->find("target"))
      out.DefaultTarget = v->stringOr("");

  // `[target.<name>]` describes a toolchain, not a preference: naming one
  // does not build for it. `--target <name>` or `[build] target` does.
  if (const TomlValue *targets = doc.get("target")) {
    for (const auto &entry : targets->Tbl) {
      if (!entry.second.isTable())
        continue;
      TargetSpec t;
      t.Name = entry.first;
      const TomlValue &v = entry.second;
      if (const TomlValue *x = v.find("base")) t.Base = x->stringOr("");
      if (const TomlValue *x = v.find("triple")) t.Triple = x->stringOr("");
      if (const TomlValue *x = v.find("cc")) t.Cc = x->stringOr("");
      if (const TomlValue *x = v.find("cxx")) t.Cxx = x->stringOr("");
      if (const TomlValue *x = v.find("ar")) t.Ar = x->stringOr("");
      if (const TomlValue *x = v.find("sysroot")) t.Sysroot = x->stringOr("");
      if (const TomlValue *x = v.find("sdk")) t.Sdk = x->stringOr("");
      if (const TomlValue *x = v.find("runtime-dir")) t.RuntimeDir = x->stringOr("");
      if (const TomlValue *x = v.find("runner")) t.Runner = x->stringOr("");
      t.CFlags = stringList(v.find("c-flags"));
      t.LinkLibraries = stringList(v.find("link"));
      t.LinkPaths = stringList(v.find("link-paths"));
      t.LinkArgs = stringList(v.find("link-args"));
      if (const TomlValue *x = v.find("linker")) t.Linker = x->stringOr("");
      if (const TomlValue *x = v.find("linker-kind")) t.LinkerKind = x->stringOr("");
      if (const TomlValue *x = v.find("default-flags")) t.DefaultFlags = x->boolOr(true);
      if (!t.LinkerKind.empty() && t.LinkerKind != "driver" && t.LinkerKind != "ld") {
        error = manifestPath.string() + ": [target." + t.Name +
                "] `linker-kind` is \"" + t.LinkerKind +
                "\"; it is \"driver\" (a compiler that links) or \"ld\" (a linker)";
        return false;
      }
      // A target with no triple, and no foreign target to take one from,
      // names nothing; the mistake is worth saying rather than building for
      // the host under another name.
      if (t.Triple.empty() && t.Base.empty() && !findForeignTarget(t.Name)) {
        error = manifestPath.string() + ": [target." + t.Name +
                "] has no `triple`";
        return false;
      }
      // Relative paths in a manifest are relative to the manifest.
      auto absolutise = [&](std::string &p) {
        if (!p.empty() && !fs::path(p).is_absolute())
          p = (root / p).lexically_normal().string();
      };
      absolutise(t.Sysroot);
      absolutise(t.Sdk);
      absolutise(t.RuntimeDir);
      for (std::string &p : t.LinkPaths)
        absolutise(p);
      out.Targets.push_back(std::move(t));
    }
  }

  // `[config]`: this package's own keys, as `@Config` compares them.
  if (const TomlValue *cfg = doc.get("config"))
    if (cfg->isTable())
      for (const auto &kv : cfg->Tbl)
        out.Config[kv.first] = configText(kv.second);

  if (const TomlValue *deps = doc.get("dependencies")) {
    for (const auto &entry : deps->Tbl) {
      Dependency d;
      d.Name = entry.first;
      if (entry.second.isString()) {
        d.Version = entry.second.Str;
      } else if (entry.second.isTable()) {
        if (const TomlValue *p = entry.second.find("path")) d.Path = p->stringOr("");
        if (const TomlValue *v = entry.second.find("version")) d.Version = v->stringOr("");
        if (const TomlValue *r = entry.second.find("registry")) d.Registry = r->stringOr("");
        if (const TomlValue *c = entry.second.find("config"))
          if (c->isTable())
            for (const auto &kv : c->Tbl)
              d.Config[kv.first] = configText(kv.second);
      }
      out.Dependencies.push_back(std::move(d));
    }
  }

  // Layout discovery.
  fs::path src = root / "src";
  collectRuneFiles(src, out.Sources);
  collectRuneFiles(root / "tests", out.TestFiles);

  // Every file that declares an output of its own becomes a target. A file
  // named `lib.rune` or `main.rune` says so by its name; anything else says so
  // with `@type(...)` or by defining `main`.
  for (const std::string &f : out.Sources) {
    fs::path p(f);
    std::string stem = p.stem().string();
    // A source that says `@runtime(none)` or `@entry(none)` says it for the
    // program, as the manifest would.
    if (programDirectiveOf(p, "runtime") == "none")
      out.Freestanding = true;
    if (programDirectiveOf(p, "entry") == "none")
      out.NoEntry = true;
    OutputKind k = kindOfSource(p);
    if (k == OutputKind::Component) {
      if (stem == "lib")  k = OutputKind::Library;
      if (stem == "main") k = OutputKind::Executable;
    }
    if (k == OutputKind::Component)
      continue;
    OutputRoot r;
    r.Name = (stem == "main" || stem == "lib") ? out.Name : stem;
    r.Path = f;
    r.Kind = k;
    out.Roots.push_back(std::move(r));
  }

  std::error_code ec;
  if (fs::exists(src / "lib.rune", ec))
    out.LibraryRoot = (src / "lib.rune").string();
  if (fs::exists(src / "main.rune", ec))
    out.BinaryRoot = (src / "main.rune").string();

  // An explicit [lib] path overrides the convention.
  if (const TomlValue *lib = doc.get("lib"))
    if (const TomlValue *p = lib->find("path"))
      out.LibraryRoot = (root / p->stringOr("src/lib.rune")).string();

  if (const TomlValue *bins = doc.get("bin")) {
    if (bins->isArray()) {
      for (const TomlValue &b : bins->Arr) {
        BinaryTarget t;
        if (const TomlValue *n = b.find("name")) t.Name = n->stringOr("");
        if (const TomlValue *p = b.find("path"))
          t.Path = (root / p->stringOr("")).string();
        if (!t.Name.empty() && !t.Path.empty())
          out.Binaries.push_back(std::move(t));
      }
    } else if (bins->isTable()) {
      BinaryTarget t;
      if (const TomlValue *n = bins->find("name")) t.Name = n->stringOr("");
      if (const TomlValue *p = bins->find("path"))
        t.Path = (root / p->stringOr("")).string();
      if (!t.Name.empty() && !t.Path.empty())
        out.Binaries.push_back(std::move(t));
    }
  }

  if (requireSources && out.Sources.empty() && out.Binaries.empty()) {
    error = "package '" + out.Name + "' has no source files under src/";
    return false;
  }
  return true;
}

std::string defaultManifestText(const std::string &name, bool isLibrary) {
  std::ostringstream os;
  os << "[package]\n"
     << "name = \"" << name << "\"\n"
     << "version = \"0.1.0\"\n"
     << "edition = \"2026\"\n"
     << "description = \"\"\n"
     << "authors = []\n"
     << "\n"
     << "[build]\n"
     << "# none | minimal | full — how much checking the compiler inserts.\n"
     << "safety = \"full\"\n"
     << "# zombie | arc — single ownership proven by the Zombie borrow\n"
     << "# checker with no counting at all, or reference counting.\n"
     << "memory = \"zombie\"\n"
     << "optimize = 0\n"
     << "debug = true\n"
     << "warnings-as-errors = false\n"
     << "\n"
     << "[dependencies]\n";
  if (isLibrary)
    os << "\n# This package builds src/lib.rune into a .rul library.\n";
  else
    os << "\n# This package builds src/main.rune into an executable.\n";
  return os.str();
}


const char *outputKindName(OutputKind k) {
  switch (k) {
  case OutputKind::Executable: return "executable";
  case OutputKind::Library:    return "library";
  case OutputKind::Object:     return "object";
  case OutputKind::Assembly:   return "assembly";
  case OutputKind::LLVM:       return "LLVM IR";
  case OutputKind::Component:  break;
  }
  return "component";
}

const char *outputKindSuffix(OutputKind k) {
  switch (k) {
  case OutputKind::Library:  return ".rul";
  case OutputKind::Object:   return ".o";
  case OutputKind::Assembly: return ".s";
  case OutputKind::LLVM:     return ".ll";
  default: break;
  }
  return "";
}

std::vector<std::string> Manifest::componentSources() const {
  std::vector<std::string> out;
  for (const std::string &s : Sources) {
    bool isRoot = false;
    for (const OutputRoot &r : Roots)
      if (r.Path == s)
        isRoot = true;
    if (!isRoot)
      out.push_back(s);
  }
  return out;
}

} // namespace rune
