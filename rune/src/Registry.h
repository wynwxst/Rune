//===- Registry.h - Packages from somewhere else ---------------*- C++ -*-===//
//
// A registry is a directory of static files, and nothing more:
//
//   registry.toml                          what this registry is
//   index.toml                             every release: name, version,
//                                          description, checksum, dependencies
//   packages/<name>/<name>-<version>.tar   the package, packed
//
// Static files are the whole of the design. Serving one is serving files
// (`rune pkg server --serve` does, over HTTP, in a few hundred lines);
// mirroring one is copying a directory; a registry on a shared drive needs no
// server at all, because a `file://` URL or a plain path works as one. The
// index is one file fetched once and cached, so `search` and `desc` cost
// nothing after the first look, and dependency resolution needs nothing but
// the index — every archive a build needs is known before the first byte of
// one is downloaded, which is what lets them all download at once.
//
// Every registry has a name — the one its own `index.toml` declares — and a
// client may know it by another (`rune pkg server add <url> --name alias`).
// The name is how commands tell registries apart: `rune add work::geometry`
// takes the package from that registry alone, `rune search --registry work`
// looks nowhere else, and a manifest records the choice as
// `geometry = { version = "1.0", registry = "work" }`.
//
// Installed packages live under `~/.rune/pkg/<name>/<version>/`, once each
// however many projects use them. Each keeps a list of the projects that
// reference it, so a version nobody uses any more can be found and removed.
// A project pins what it resolved in `Rune.lock`, and its `Rune.toml` names
// what it asked for.
//
//===----------------------------------------------------------------------===//
#ifndef RUNE_PM_REGISTRY_H
#define RUNE_PM_REGISTRY_H

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace rune {
struct Manifest;
struct Dependency;
}

namespace rune::pm {

//===----------------------------------------------------------------------===//
// Versions
//===----------------------------------------------------------------------===//

/// `major.minor.patch`, with an optional `-pre` tag that sorts before the
/// release it precedes.
struct Version {
  int64_t Major = 0, Minor = 0, Patch = 0;
  std::string Pre;

  static bool parse(const std::string &text, Version &out);
  std::string str() const;
  int compare(const Version &o) const;
  bool operator<(const Version &o) const { return compare(o) < 0; }
  bool operator==(const Version &o) const { return compare(o) == 0; }
};

/// What a dependency asks for. The spellings are Cargo's, because they are
/// the ones people already know:
///
///   "1.2.3"  "^1.2.3"   compatible: >=1.2.3 and the same leading non-zero part
///   "~1.2"              >=1.2.0 <1.3.0
///   "=1.2.3"            exactly that
///   ">=1.2, <2.0"       a list of comparisons, all of which must hold
///   "*"                 anything
struct Requirement {
  struct Comparator {
    enum class Op { Ge, Gt, Le, Lt, Eq, Caret, Tilde, Any } O = Op::Any;
    Version V;
    /// How many parts were written: `1.2` differs from `1.2.0` for `^`/`~`.
    int Parts = 3;
  };
  std::vector<Comparator> Comparators;
  std::string Source;

  static bool parse(const std::string &text, Requirement &out);
  bool matches(const Version &v) const;
};

//===----------------------------------------------------------------------===//
// The index
//===----------------------------------------------------------------------===//

/// One release of one package, as the index describes it.
struct Release {
  std::string Name;
  Version V;
  std::string Description;
  std::vector<std::string> Authors;
  std::string License;
  std::string Archive;   ///< relative to the registry root
  std::string Sha256;
  uint64_t Size = 0;
  std::string Added;     ///< ISO 8601, UTC
  /// `name req` pairs, as the package's own `[dependencies]` asked for; a
  /// name is `registry::name` when the package insisted on a registry.
  std::vector<std::pair<std::string, std::string>> Dependencies;
  /// Where this came from, as a URL; filled in when indexes are merged.
  std::string Registry;
  /// The same, by the name the client knows the registry under.
  std::string RegistryName;
};

struct Index {
  std::string Name;
  std::string Updated;
  std::vector<Release> Releases;

  bool parse(const std::string &text, std::string &error);
  std::string serialise() const;
  /// The releases of `name`, newest first — from `registry` alone when one
  /// is named, from every registry otherwise.
  std::vector<const Release *> versionsOf(const std::string &name,
                                          const std::string &registry = "") const;
  /// The newest release of `name` that `req` accepts, or null.
  const Release *best(const std::string &name, const Requirement &req,
                      const std::string &registry = "") const;
  /// The names of the registries that have `name`, in index order.
  std::vector<std::string> registriesWith(const std::string &name) const;
};

//===----------------------------------------------------------------------===//
// Bytes: checksums and archives
//===----------------------------------------------------------------------===//

std::string sha256Hex(const std::string &bytes);

/// Packs a project directory as a `ustar` archive: `Rune.toml`, `src/`,
/// `docs/`, `tests/`, the C sources the manifest names, and the README and
/// LICENSE files beside them. Never `target/`.
bool packPackage(const std::filesystem::path &project, std::string &tar,
                 std::string &error);
/// Unpacks a `ustar` archive under `into`, refusing any path that would
/// escape it.
bool unpackTar(const std::string &tar, const std::filesystem::path &into,
               std::string &error);

//===----------------------------------------------------------------------===//
// Fetching
//===----------------------------------------------------------------------===//

/// Reads `url` — `http://`, `file://`, or a plain path — into `out`.
/// `https://` is handed to `curl` when there is one.
bool fetch(const std::string &url, std::string &out, std::string &error);

//===----------------------------------------------------------------------===//
// The client's registries
//===----------------------------------------------------------------------===//

struct RegistrySource {
  /// What this machine calls it: the registry's own name unless `--name`
  /// gave an alias. Commands, manifests and `registry::package` use this.
  std::string Name;
  std::string Url;
  std::string Added;
  /// The name the registry declares for itself, kept so an alias can be
  /// explained. Equal to `Name` when there is no alias.
  std::string Declared;
};

/// `~/.rune/registries.toml`.
std::vector<RegistrySource> loadRegistries();
bool saveRegistries(const std::vector<RegistrySource> &list, std::string &error);
/// The configured registry called `name`, or the one at that URL, or null.
const RegistrySource *findRegistry(const std::vector<RegistrySource> &list,
                                   const std::string &nameOrUrl);
/// The name this machine knows the registry at `url` under, or the URL
/// itself when it is not configured any more.
std::string registryNameOf(const std::vector<RegistrySource> &list,
                           const std::string &url);
/// True when `name` can name a registry: letters, digits, `_`, `-` and `.`,
/// so it never collides with the `::` and `@` that join it to a package.
bool isRegistryName(const std::string &name);
/// `registry::name` taken apart; `registry` is empty when there is no
/// prefix. Anything else — `name`, `name@req` — is left whole in `rest`.
void splitQualified(const std::string &spec, std::string &registry,
                    std::string &rest);

/// The index of every configured registry, fetched at the same time and
/// merged, each release tagged with where it came from. `refresh` bypasses
/// the cached copy under `~/.rune/cache/`.
bool loadMergedIndex(Index &out, bool refresh, std::string &error);

//===----------------------------------------------------------------------===//
// What is installed
//===----------------------------------------------------------------------===//

/// `~/.rune/pkg/<name>/<version>`.
std::filesystem::path installDir(const std::string &name, const Version &v);
/// The projects referencing an installed version, one root per line in its
/// `.rune-refs`.
std::vector<std::string> referencesOf(const std::filesystem::path &dir);
void addReference(const std::filesystem::path &dir, const std::string &project);
void dropReference(const std::filesystem::path &dir, const std::string &project);
/// Turns reference recording off (or on again): a build that is not a
/// project's — `rune doc <package>` building a copy in the store — must not
/// make the store count itself as a user of what it installs.
void setRecordReferences(bool on);

struct Installed {
  std::string Name;
  Version V;
  std::filesystem::path Dir;
  std::vector<std::string> Refs;
  /// The registry it was fetched from, as a URL, from its `.rune-origin`.
  std::string Registry;
};
std::vector<Installed> listInstalled();

//===----------------------------------------------------------------------===//
// Resolution and installation
//===----------------------------------------------------------------------===//

/// One pinned package in `Rune.lock`.
struct Locked {
  std::string Name;
  Version V;
  std::string Registry;
  std::string Sha256;
};

std::vector<Locked> loadLock(const std::filesystem::path &projectRoot);
bool saveLock(const std::filesystem::path &projectRoot,
              const std::vector<Locked> &lock, std::string &error);

/// One thing a project asks for: a package, a version requirement, and
/// optionally the registry it has to come from.
struct Want {
  std::string Name;
  std::string Req;
  std::string Registry;
};

/// Resolves every registry dependency reachable from `wants` to one release
/// per package, transitively, and installs whatever is not there yet, all
/// downloads at once. On success `out` holds every package the project now
/// depends on, direct and transitive, and each is referenced by
/// `projectRoot`.
///
/// `lock` holds what was resolved last time: a package that is still
/// reachable keeps its locked version unless it is named in `moving`, so
/// nothing drifts that was not asked to. A stale lock entry — one nothing
/// reaches any more — is simply left behind.
bool resolveAndInstall(const std::filesystem::path &projectRoot,
                       const std::vector<Want> &wants,
                       const std::vector<Locked> &lock,
                       const std::vector<std::string> &moving,
                       const Index &index, std::vector<Locked> &out,
                       bool verbose, std::string &error);

/// Where the package `name` is installed, for reading rather than building
/// against: the version `projectDir`'s lock pins when that is a project
/// depending on it (a path dependency answers with its path), else the
/// newest version installed, else the newest release the registries — or
/// `registry` alone — offer, installed now. Empty on failure, with the
/// reason in `error`.
std::filesystem::path locatePackage(const std::string &projectDir,
                                    const std::string &name,
                                    const std::string &registry, bool verbose,
                                    std::string &error);

/// The directory a registry dependency of `m` is installed in, resolving —
/// and installing — it first when the lock does not already pin it. Empty on
/// failure, with the reason reported.
std::filesystem::path resolveRegistryDependency(const Manifest &m,
                                                const Dependency &d,
                                                bool verbose);
/// Names the project whose `Rune.lock` pins the build about to happen, so a
/// registry package's own dependencies resolve through it rather than
/// through a lock of their own.
void setLockProject(const std::filesystem::path &root);

//===----------------------------------------------------------------------===//
// Commands
//===----------------------------------------------------------------------===//

/// `rune pkg ...`: the registry server side.
int commandPkg(const std::vector<std::string> &args, bool verbose);
int commandSearch(const std::vector<std::string> &args, bool verbose);
int commandDesc(const std::vector<std::string> &args, bool verbose);
int commandInstalled(const std::vector<std::string> &args, bool verbose);
int commandAdd(const std::string &projectDir, const std::vector<std::string> &args,
               bool verbose);
int commandRemove(const std::string &projectDir,
                  const std::vector<std::string> &args, bool verbose);
int commandUpdate(const std::string &projectDir,
                  const std::vector<std::string> &args, bool verbose);
int commandDeps(const std::string &projectDir,
                const std::vector<std::string> &args, bool verbose);

} // namespace rune::pm

#endif
