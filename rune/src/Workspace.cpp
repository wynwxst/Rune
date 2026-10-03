//===--- Workspace.cpp - Several packages worked on together -------------===//

#include "Workspace.h"

#include "Console.h"
#include "Manifest.h"
#include "Toml.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace rune::pm {

namespace {

bool readText(const fs::path &p, std::string &out) {
  std::ifstream in(p);
  if (!in)
    return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}

bool readToml(const std::string &dir, TomlDocument &doc) {
  std::string text;
  if (!readText(fs::path(dir) / "Rune.toml", text))
    return false;
  doc = parseToml(text);
  return doc.ok();
}

std::string shellQuote(const std::string &s) {
  std::string out = "'";
  for (char ch : s) {
    if (ch == '\'')
      out += "'\\''";
    else
      out += ch;
  }
  return out + "'";
}

int run(const std::string &cmd, bool verbose) {
  if (verbose)
    std::cerr << c("\x1b[2m") << "  " << cmd << c("\x1b[0m") << "\n";
  int rc = std::system(cmd.c_str());
  if (rc == -1)
    return 127;
  return (rc & 0x7F) ? 128 + (rc & 0x7F) : ((rc >> 8) & 0xFF);
}

/// A member's directory as it is written: relative, forward slashes, no
/// `./` in front and no `/` behind.
std::string normalMember(const std::string &dir) {
  std::string s = fs::path(dir).lexically_normal().generic_string();
  while (s.size() > 1 && s.back() == '/')
    s.pop_back();
  if (s.rfind("./", 0) == 0)
    s = s.substr(2);
  return s.empty() ? "." : s;
}

/// `members = [...]` written out, one to a line once there are several.
std::string membersLine(const std::vector<std::string> &members) {
  if (members.empty())
    return "members = []";
  if (members.size() == 1)
    return "members = [\"" + members[0] + "\"]";
  std::string out = "members = [\n";
  for (const std::string &m : members)
    out += "    \"" + m + "\",\n";
  return out + "]";
}

/// `text` with its `[workspace]` table's `members` replaced — or the table
/// added, at the top, when there is none. Everything else is left as it was
/// written, comments included.
std::string withMembers(const std::string &text,
                        const std::vector<std::string> &members) {
  const std::string line = membersLine(members);
  size_t header = std::string::npos;
  for (size_t at = 0; at < text.size();) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos)
      end = text.size();
    std::string l = text.substr(at, end - at);
    size_t first = l.find_first_not_of(" \t");
    if (first != std::string::npos && l.compare(first, 11, "[workspace]") == 0) {
      header = end;
      break;
    }
    at = end + 1;
  }
  if (header == std::string::npos) {
    std::string out = "[workspace]\n" + line + "\n";
    return text.empty() ? out : out + "\n" + text;
  }
  // The table runs to the next header.
  size_t tableEnd = text.size();
  for (size_t at = header + 1; at < text.size();) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos)
      end = text.size();
    size_t first = text.find_first_not_of(" \t", at);
    if (first < end && text[first] == '[') {
      tableEnd = at;
      break;
    }
    at = end + 1;
  }
  // Its `members`, which may run over several lines.
  for (size_t at = header + 1; at < tableEnd;) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos)
      end = text.size();
    size_t first = text.find_first_not_of(" \t", at);
    if (first < end && text.compare(first, 7, "members") == 0) {
      size_t open = text.find('[', first);
      size_t close = open == std::string::npos ? std::string::npos
                                               : text.find(']', open);
      if (close == std::string::npos)
        break;
      return text.substr(0, at) + line + text.substr(close + 1);
    }
    at = end + 1;
  }
  return text.substr(0, header + 1) + line + "\n" + text.substr(header + 1);
}

bool writeMembers(const std::string &dir, const std::vector<std::string> &members) {
  fs::path p = fs::path(dir) / "Rune.toml";
  std::string text;
  readText(p, text);
  std::ofstream out(p);
  if (!out)
    return false;
  out << withMembers(text, members);
  return true;
}

std::vector<std::string> writtenMembers(const TomlDocument &doc) {
  std::vector<std::string> out;
  if (const TomlValue *ws = doc.get("workspace"))
    if (const TomlValue *m = ws->find("members"))
      for (const TomlValue &v : m->Arr)
        if (v.isString())
          out.push_back(normalMember(v.Str));
  return out;
}

/// Members in an order where each comes after those it depends on. A cycle
/// is reported, and its members left in the order they were written.
std::vector<size_t> buildOrder(const Workspace &ws, std::string &cycle) {
  std::vector<int> state(ws.Members.size(), 0); // 0 new, 1 visiting, 2 done
  std::vector<size_t> order;
  std::function<void(size_t)> visit = [&](size_t i) {
    if (state[i] == 2)
      return;
    if (state[i] == 1) {
      if (cycle.empty())
        cycle = ws.Members[i].Dir;
      return;
    }
    state[i] = 1;
    for (size_t d : ws.Members[i].DependsOn)
      visit(d);
    state[i] = 2;
    order.push_back(i);
  };
  for (size_t i = 0; i < ws.Members.size(); ++i)
    visit(i);
  return order;
}

void usage() {
  std::cout << R"(rune ws — several packages worked on together

USAGE
    rune ws new <dir>            make a workspace in a new directory
    rune ws init                 make this directory a workspace, with the
                                 packages already under it as members
    rune ws add <dir> [--lib]    add a member, making a package there first
                                 when there is none
    rune ws remove <dir>         stop treating <dir> as a member (its files
                                 are left alone)
    rune ws list                 the members, in the order they build
    rune ws <command> [args]     build, check, test, clean or doc every
                                 member, dependencies first
    rune ws run <member> [args]  run one member's program

The workspace is the Rune.toml with a [workspace] table:

    [workspace]
    members = ["app", "geometry"]

`rune build` (and check, test, clean, doc) at the root of a workspace that
is not itself a package builds every member.
)";
}

/// Packages under `root`, one or two levels down, that are not inside one
/// another's `target/`.
std::vector<std::string> packagesUnder(const fs::path &root) {
  std::vector<std::string> out;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end;
       it.increment(ec)) {
    const fs::path &p = it->path();
    std::string leaf = p.filename().string();
    if (it->is_directory(ec) &&
        (leaf == "target" || leaf == "src" || leaf == "tests" ||
         (!leaf.empty() && leaf[0] == '.'))) {
      it.disable_recursion_pending();
      continue;
    }
    if (it.depth() >= 2)
      it.disable_recursion_pending();
    if (it->is_directory(ec) && fs::exists(p / "Rune.toml", ec)) {
      out.push_back(normalMember(fs::relative(p, root, ec).generic_string()));
      it.disable_recursion_pending();
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

} // namespace

bool isWorkspace(const std::string &dir) {
  TomlDocument doc;
  return readToml(dir, doc) && doc.get("workspace");
}

bool isWorkspaceOnly(const std::string &dir) {
  TomlDocument doc;
  return readToml(dir, doc) && doc.get("workspace") && !doc.get("package");
}

bool loadWorkspace(const std::string &dir, Workspace &out, std::string &error) {
  TomlDocument doc;
  fs::path manifest = fs::path(dir) / "Rune.toml";
  if (!fs::exists(manifest)) {
    error = "no Rune.toml in '" + dir + "'";
    return false;
  }
  if (!readToml(dir, doc)) {
    error = manifest.string() + ":" + std::to_string(doc.ErrorLine) + ": " +
            doc.Error;
    return false;
  }
  if (!doc.get("workspace")) {
    error = manifest.string() + " has no [workspace] table";
    return false;
  }
  out.Root = fs::absolute(dir).lexically_normal().string();
  while (out.Root.size() > 1 && out.Root.back() == '/')
    out.Root.pop_back();
  out.AlsoPackage = doc.get("package") != nullptr;
  std::map<std::string, size_t> byPath;
  for (const std::string &m : writtenMembers(doc)) {
    WorkspaceMember wm;
    wm.Dir = m;
    byPath[(fs::path(out.Root) / m).lexically_normal().string()] =
        out.Members.size();
    out.Members.push_back(wm);
  }
  // Each member's manifest: its name, what it builds, and which other
  // members it depends on by path.
  for (WorkspaceMember &wm : out.Members) {
    fs::path at = fs::path(out.Root) / wm.Dir;
    Manifest m;
    std::string why;
    if (!loadManifest(at.string(), m, why)) {
      error = "member '" + wm.Dir + "': " + why;
      return false;
    }
    wm.Name = m.Name;
    wm.Library = !m.LibraryRoot.empty();
    wm.Binary = !m.BinaryRoot.empty() || !m.Binaries.empty();
    for (const Dependency &d : m.Dependencies) {
      if (d.Path.empty())
        continue;
      std::string target = (at / d.Path).lexically_normal().string();
      while (target.size() > 1 && target.back() == '/')
        target.pop_back();
      auto it = byPath.find(target);
      if (it != byPath.end())
        wm.DependsOn.push_back(it->second);
    }
  }
  return true;
}

int runInMembers(const std::string &dir, const std::string &self,
                 const std::string &command,
                 const std::vector<std::string> &args, bool verbose) {
  Workspace ws;
  std::string error;
  if (!loadWorkspace(dir, ws, error)) {
    failLine(error);
    return 1;
  }
  if (ws.Members.empty()) {
    warnLine("the workspace has no members");
    note("add one with `rune ws add <dir>`");
    return 0;
  }
  std::string cycle;
  std::vector<size_t> order = buildOrder(ws, cycle);
  if (!cycle.empty()) {
    failLine("the members depend on one another in a circle, through '" +
             cycle + "'");
    return 1;
  }
  std::string rest;
  for (const std::string &a : args)
    rest += " " + shellQuote(a);
  size_t done = 0;
  for (size_t i : order) {
    const WorkspaceMember &m = ws.Members[i];
    status("Member", m.Name + " (" + m.Dir + ")");
    std::string cmd = shellQuote(self) + " " + command + " -C " +
                      shellQuote((fs::path(ws.Root) / m.Dir).string()) + rest;
    if (run(cmd, verbose) != 0) {
      failLine("'" + command + "' failed in member '" + m.Dir + "'");
      if (done + 1 < order.size())
        note(std::to_string(order.size() - done - 1) +
             " member(s) after it were not reached");
      return 1;
    }
    ++done;
  }
  okLine("'" + command + "' done in " + std::to_string(done) + " member(s)");
  return 0;
}

int commandWorkspace(const std::vector<std::string> &args,
                     const std::string &dir, const std::string &self,
                     bool verbose) {
  std::string sub = args.empty() ? "list" : args[0];
  std::vector<std::string> rest(args.begin() + (args.empty() ? 0 : 1),
                                args.end());
  if (sub == "-h" || sub == "--help" || sub == "help") {
    usage();
    return 0;
  }

  if (sub == "new" || sub == "init") {
    std::string where = dir;
    if (sub == "new") {
      if (rest.empty()) {
        failLine("`rune ws new` needs a directory");
        note("for example: rune ws new studio");
        return 2;
      }
      where = (fs::path(dir) / rest[0]).string();
    }
    std::error_code ec;
    fs::create_directories(where, ec);
    if (isWorkspace(where)) {
      failLine("'" + where + "' is already a workspace");
      return 1;
    }
    std::vector<std::string> members =
        sub == "init" ? packagesUnder(where) : std::vector<std::string>{};
    if (!writeMembers(where, members)) {
      failLine("cannot write " + (fs::path(where) / "Rune.toml").string());
      return 1;
    }
    std::string shown = fs::absolute(where).lexically_normal().string();
    while (shown.size() > 1 && shown.back() == '/')
      shown.pop_back();
    okLine("Created workspace at " + shown);
    for (const std::string &m : members)
      note("member: " + m);
    if (sub == "new")
      note("add a package with `rune ws add <dir>` inside it");
    return 0;
  }

  // Everything else works on an existing workspace: this directory's, or
  // the nearest one above that lists this directory, as `rune` finds the
  // package around it.
  std::string root = dir;
  if (!isWorkspace(root)) {
    std::error_code ec;
    for (fs::path p = fs::absolute(dir, ec).parent_path(); !ec && !p.empty();
         p = p.parent_path()) {
      if (isWorkspace(p.string())) {
        root = p.string();
        break;
      }
      if (p == p.root_path())
        break;
    }
  }
  if (!isWorkspace(root)) {
    failLine("not in a workspace: no Rune.toml with a [workspace] table here "
             "or above");
    note("make one with `rune ws init` or `rune ws new <dir>`");
    return 1;
  }

  TomlDocument doc;
  readToml(root, doc);
  std::vector<std::string> members = writtenMembers(doc);

  if (sub == "add") {
    bool lib = false;
    std::string what;
    for (const std::string &a : rest) {
      if (a == "--lib")
        lib = true;
      else if (what.empty())
        what = a;
    }
    if (what.empty()) {
      failLine("`rune ws add` needs a directory");
      return 2;
    }
    // Relative to the workspace, wherever it was run from.
    fs::path target = fs::absolute(what).lexically_normal();
    std::error_code ec;
    std::string rel = normalMember(fs::relative(target, root, ec).generic_string());
    if (ec || rel.rfind("..", 0) == 0) {
      failLine("'" + what + "' is not inside the workspace at " + root);
      return 1;
    }
    if (std::find(members.begin(), members.end(), rel) != members.end()) {
      failLine("'" + rel + "' is already a member");
      return 1;
    }
    if (!fs::exists(target / "Rune.toml", ec)) {
      std::string cmd = shellQuote(self) + " new " + shellQuote(target.string()) +
                        (lib ? " --lib" : "");
      if (run(cmd, verbose) != 0)
        return 1;
    }
    members.push_back(rel);
    if (!writeMembers(root, members)) {
      failLine("cannot write the workspace's Rune.toml");
      return 1;
    }
    okLine("Added '" + rel + "' to the workspace");
    return 0;
  }

  if (sub == "remove") {
    if (rest.empty()) {
      failLine("`rune ws remove` needs a directory");
      return 2;
    }
    std::error_code ec;
    std::string rel = normalMember(
        fs::relative(fs::absolute(rest[0]).lexically_normal(), root, ec)
            .generic_string());
    auto it = std::find(members.begin(), members.end(), rel);
    if (it == members.end()) {
      it = std::find(members.begin(), members.end(), normalMember(rest[0]));
      if (it == members.end()) {
        failLine("'" + rest[0] + "' is not a member");
        return 1;
      }
    }
    std::string gone = *it;
    members.erase(it);
    if (!writeMembers(root, members)) {
      failLine("cannot write the workspace's Rune.toml");
      return 1;
    }
    okLine("Removed '" + gone + "' from the workspace; its files are untouched");
    return 0;
  }

  if (sub == "list") {
    Workspace ws;
    std::string error;
    if (!loadWorkspace(root, ws, error)) {
      failLine(error);
      return 1;
    }
    std::string cycle;
    std::vector<size_t> order = buildOrder(ws, cycle);
    plain("workspace " + ws.Root);
    for (size_t i : order) {
      const WorkspaceMember &m = ws.Members[i];
      std::string kind = m.Library && m.Binary ? "lib+bin"
                         : m.Library           ? "lib"
                                               : "bin";
      std::string deps;
      for (size_t d : m.DependsOn)
        deps += (deps.empty() ? "" : ", ") + ws.Members[d].Name;
      plain("  " + m.Name + "  " + m.Dir + "  [" + kind + "]" +
            (deps.empty() ? "" : "  uses " + deps));
    }
    if (!cycle.empty())
      warnLine("the members depend on one another in a circle, through '" +
               cycle + "'");
    return 0;
  }

  if (sub == "run") {
    if (rest.empty()) {
      failLine("`rune ws run` needs the member to run");
      return 2;
    }
    Workspace ws;
    std::string error;
    if (!loadWorkspace(root, ws, error)) {
      failLine(error);
      return 1;
    }
    for (const WorkspaceMember &m : ws.Members) {
      if (m.Name != rest[0] && m.Dir != normalMember(rest[0]))
        continue;
      std::string cmd = shellQuote(self) + " run -C " +
                        shellQuote((fs::path(ws.Root) / m.Dir).string());
      for (size_t i = 1; i < rest.size(); ++i)
        cmd += " " + shellQuote(rest[i]);
      return run(cmd, verbose);
    }
    failLine("no member named '" + rest[0] + "'");
    note("`rune ws list` shows them");
    return 1;
  }

  if (sub == "build" || sub == "check" || sub == "test" || sub == "clean" ||
      sub == "doc")
    return runInMembers(root, self, sub, rest, verbose);

  failLine("unknown workspace command '" + sub + "'");
  note("run `rune ws --help` for the list");
  return 2;
}

} // namespace rune::pm
