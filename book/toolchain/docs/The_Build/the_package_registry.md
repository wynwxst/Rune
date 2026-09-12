# The package registry

`Registry.h` / `Registry.cpp`, plus `Console.h` for the status lines it
shares with the build. Everything a package from somewhere else involves:
what a registry is, how one is served, how a project asks for a package,
and how what is installed is tracked.

## A registry is a directory

```
registry.toml                          what this registry is
index.toml                             every release, one [[release]] each
packages/<name>/<name>-<version>.tar   the package, packed
```

That is the whole of the design, and it is the reason for most of what
follows. A registry is static files, so:

- serving one is serving files — `rune pkg server --serve` is a few hundred
  lines of HTTP/1.1 with `Connection: close`, and any web server would do;
- mirroring one is copying a directory;
- a registry on a shared drive needs no server: a `file://` URL or a plain
  path is one, and `fetch` reads it with `readFile`;
- the index is one file, fetched once and cached under
  `~/.rune/cache/index/`, so `search` and `desc` cost nothing afterwards;
- resolution needs nothing but the index — each release lists its
  dependencies — so every archive a build needs is known before the first
  byte of one is downloaded.

The index is TOML rather than JSON because the front end already reads TOML
(`Toml.cpp`) and writes it with `tomlString`; a flat list of `[[release]]`
entries rather than nested tables because the reader handles arrays of
tables and not arrays inside them. Archives are plain `ustar` — no
compression, since Rune sources are small and an archive any `tar` can open
is worth more than the bytes — written and read by `packPackage` and
`unpackTar`, which refuses any path that would escape the directory.

## Adding a package

`rune pkg server --addPackage <project>` loads the project's manifest, packs
`Rune.toml`, `src/`, `docs/`, `tests/`, the C sources the manifest names and
the README/LICENSE beside them (never `target/`), hashes the archive with
the SHA-256 in `Registry.cpp`, writes it under `packages/`, and rewrites
`index.toml` with the release added — its dependencies recorded as
`name requirement` pairs straight from the manifest, `registry::name` when
the manifest insisted on a registry. A path dependency is recorded as
`name *` with a warning: a registry package cannot follow a path.

The index also carries the registry's own name, `[registry] name`, which
`rune pkg init` takes from the directory or `--name`. That name is what a
client adds the registry under, so it is not optional: `pkgAddSource`
refuses an index without one.

## What a project asks for

```toml
[dependencies]
geometry = { path = "../geometry" }
shapes = "1.0"
logger = { version = "1.0", registry = "lab" }
```

A dependency with no `path` is a registry dependency, and its string is a
`Requirement` — Cargo's spellings: `^` (the default), `~`, `=`, comparison
lists, `*`. `Requirement::matches` is the one place those semantics live.
`registry` names the one it must come from; without it, any configured
registry that has the package will do. A `Want` is the triple — name,
requirement, registry — and is what the resolver and every command pass
around; `registryWants` reads them off a manifest.

`Rune.lock` pins what was resolved: name, version, registry, checksum. The
build reads it before anything else. `resolveRegistryDependency` — called
from `resolvePackages` in `main.cpp` where a path dependency would be
followed — answers from the lock when the pinned version is installed *and
still satisfies the manifest* (`pinSatisfies`: the requirement accepts the
pinned version, and the registry named, if any, is where it came from), and
otherwise resolves, installs and pins, holding every other locked package
at its version so a build never moves something on its own. The
satisfaction check is what makes a hand-edited requirement count: change
`stats = "2.1"` to `stats = "=2.2.0"` and the next build resolves `stats`
again — and fails, naming the versions there are, when no such release
exists. `rune deps` shows the same pin as *stale* until then. Because a
registry package's own dependencies are resolved through the *root*
project's lock (`setLockProject`), an installed package never grows a lock
of its own — except the one `rune doc <package>` writes when it builds a
copy in the store, below.

## Resolution

`resolve` picks one release per package such that every requirement anyone
has of it holds, transitively. It is a fixed-point iteration: pick the
newest release of each requested package that satisfies all of its
requirements; add the requirements those picks bring; repeat until no new
requirement appears. A package with no satisfying version is reported with
what was asked and what exists. There is no backtracking — one version per
package, which is the rule the `.rul` format needs anyway, since two
versions of one module cannot both be linked.

A registry named for a package narrows `Index::versionsOf` to that
registry's releases; two askers naming different registries for one
package is a conflict, reported as such, because one installed copy cannot
come from both. With no registry named, the merged index answers newest
first, and the same version in two registries keeps the order the
registries were configured in. `resolveAndInstall` holds a locked package
at its version *and* its registry while that registry is still configured,
so nothing drifts sideways either.

## Installation

`~/.rune/pkg/<name>/<version>/` holds each installed version once, however
many projects use it. Installing is `installOne`: the archive from
`~/.rune/cache/archives/<sha256>.tar` if it is there, else fetched; the
checksum verified against the index; unpacked into a sibling
`.installing` directory and renamed into place whole, so a crash leaves
nothing half-installed. Every missing package is one `Job`, and
`runGraph` runs them on `jobLimit()` threads — the same pool the build
uses, so `-j` governs both. Each job writes only under its own directory,
and the status lines go through `writeSerialized`.

Beside each installed version, `.rune-refs` lists the projects that depend
on it, one absolute root per line. `rune add` and a build that installs add
a line; `rune remove` and `rune update` drop one when a version stops being
used; `rune installed` counts them (dropping any whose project no longer
has a manifest) and warns about versions with none, which `rune remove`
with no arguments deletes. `.rune-origin` records where the archive came
from and its checksum; `listInstalled` reads the registry back out of it,
which is what `--registry` filters on and the `[name]` tags show.

`rune doc <package>` is the one build that happens *inside* the store.
`locatePackage` finds the copy to read — the version the project here
pins, else the newest installed, else the newest release a registry has,
installed on the spot — and `commandDocPackage` runs `rune doc --open` in
that directory. Before it does, `setRecordReferences(false)` turns
reference recording off: the store is not a project, and a copy built for
reading must not count as a user of what it pulled in. Whatever that
installs is unreferenced, and `rune remove` reclaims it like the package
itself once nothing uses it.

## The client's registries

`~/.rune/registries.toml` lists them, by name, URL, and — when an alias was
given — the name the registry declares. `rune pkg server add <url>` fetches
the index first, so a typo is caught then rather than at the first search;
takes the registry's name from it, or `--name` as an alias; refuses a
second registry under a name already taken; and warns that nothing
monitors what the registry serves. `list` prints them, with the release
count from the cached index and nothing fetched; `remove <name>` drops one
and its cached index, leaving what was installed from it in place — a lock
names a registry by URL, so a build still knows where each package came
from. With several configured, `loadMergedIndex` fetches all their indexes
at once and tags each release with its URL and its name; a registry that
cannot be reached is skipped with a warning when a cached copy exists.

Names are the handle everywhere else. `splitQualified` takes
`registry::name` apart (only when the prefix is a valid name, so a regex
with `::` in it survives `rune search`), `takeRegistryFlag` pulls
`--registry <name>` out of a command's arguments, and `qualify` reconciles
the two — a prefix and a flag that disagree are an error. A name is
letters, digits, `_`, `-` and `.`, checked by `isRegistryName`, so it can
never be mistaken for the `::` or `@` beside it.

## What is deliberately not here

- **HTTPS in the client.** `https://` is handed to `curl`, which knows about
  certificates and this code should not.
- **Signatures.** The checksum in the index catches corruption and
  tampering with an archive, not a malicious index. A signed index is the
  next step, and belongs with a registry someone runs for other people.
- **Yanking, ownership, search on the server.** All of those are a
  database; a directory of files has none, on purpose.
