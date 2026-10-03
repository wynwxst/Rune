//===--- Workspace.h - Several packages worked on together -----*- C++ -*-===//
//
// A workspace is a directory whose Rune.toml has a `[workspace]` table
// naming the packages under it:
//
//     [workspace]
//     members = ["app", "geometry", "tools/report"]
//
// Each member is an ordinary package with a Rune.toml of its own. `rune ws`
// adds, removes and lists them, and runs a command over all of them in
// dependency order — a member that depends on another by `path` comes after
// it. `rune build`, `check`, `test`, `clean` and `doc` at the root of a
// workspace with no `[package]` of its own do the same.
//
//===----------------------------------------------------------------------===//

#ifndef RUNE_PM_WORKSPACE_H
#define RUNE_PM_WORKSPACE_H

#include <string>
#include <vector>

namespace rune::pm {

struct WorkspaceMember {
  std::string Dir;      ///< as written in `members`, relative to the root
  std::string Name;     ///< the package's name, once its manifest is read
  bool Library = false;
  bool Binary = false;
  std::vector<size_t> DependsOn; ///< other members it names by `path`
};

struct Workspace {
  std::string Root;     ///< absolute
  bool AlsoPackage = false; ///< the root's Rune.toml has a `[package]` too
  std::vector<WorkspaceMember> Members;
};

/// True when `dir/Rune.toml` has a `[workspace]` table.
bool isWorkspace(const std::string &dir);
/// True when `dir/Rune.toml` has a `[workspace]` table and no `[package]`.
bool isWorkspaceOnly(const std::string &dir);

/// Reads the workspace at `dir`, and each member's manifest.
bool loadWorkspace(const std::string &dir, Workspace &out, std::string &error);

/// `rune ws ...`. `self` is the `rune` executable, which runs each member's
/// command; `dir` is where the workspace is (or is to be made).
int commandWorkspace(const std::vector<std::string> &args,
                     const std::string &dir, const std::string &self,
                     bool verbose);

/// Runs `rune <command> <args>` in every member, dependencies first.
int runInMembers(const std::string &dir, const std::string &self,
                 const std::string &command,
                 const std::vector<std::string> &args, bool verbose);

} // namespace rune::pm

#endif
