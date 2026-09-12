# Packages from a registry

The commands, in brief; [Packages and registries](#packages) walks through making a package, using one, and running a registry.

A registry is a directory of static files: an `index.toml` that lists every release — name, version, description, checksum, dependencies — and a `packages/` tree of plain `tar` archives. Serving one is serving files; mirroring one is copying a directory; a registry on a shared drive needs no server at all. The index is fetched once and cached, so searching costs nothing after the first look, and dependency resolution needs nothing but the index — every archive a build needs is known before the first byte of one is downloaded, which is what lets them all download at once.

| Command | Does |
| --- | --- |
| `rune pkg init [dir] [--name N]` | make a registry here, or in *dir*, named after it or *N* |
| `rune registry --addPackage <project> [--dir D]` | pack a project and add it to the registry's index |
| `rune registry --serve [--port N] [--dir D]` | serve the registry over HTTP (default port 7878) |
| `rune registry add <url> [--name N]` | use a registry from this machine — `http://`, `file://`, or a path — under its own name, or the alias *N* |
| `rune registry list` | the registries this machine uses |
| `rune registry remove <name>` | stop using one; what came from it stays installed |
| `rune search <regex>` | packages whose name or description match |
| `rune desc <name>` | versions, authors, dependencies, where it is from, whether it is installed |
| `rune add <name>[@req]` | depend on it: install, write `Rune.toml`, pin in `Rune.lock` |
| `rune remove <name>` | drop the dependency; with no name, uninstall what nothing uses |
| `rune update [<name>]` | move to the newest versions the requirements allow |
| `rune deps` | the dependency tree, each package tagged with its registry |
| `rune installed` | every installed version, how many projects use it, and where it came from |
| `rune doc <name>` | a package's documentation: the copy this project uses, or the newest, fetched if need be |

*A package is `<name>`, or `<registry>::<name>` to take it from one registry; `search`, `desc`, `add`, `update`, `deps` and `installed` take `--registry <name>` for the same.*

Installed packages live under `~/.rune/pkg/<name>/<version>/`, once each however many projects use them. Each keeps the list of projects that reference it, so `rune installed` can say which versions nothing uses any more and `rune remove` with no arguments can uninstall them. A project pins what it resolved in `Rune.lock` — commit it, and a build on another machine fetches exactly the same versions, from the cache when it has them.

```sh
$ rune pkg init registry --name work
$ rune registry --addPackage ../geometry --dir registry
$ rune registry --serve --dir registry &
$ rune registry add http://localhost:7878
● Added registry 'work' at http://localhost:7878 (1 release)
$ rune search geo
geometry  v0.2.0     Points and distances
$ rune add work::geometry
○ Fetching geometry v0.2.0
● Installed geometry v0.2.0
● Added geometry "0.2.0" from work to Rune.toml (v0.2.0 installed)
$ rune deps
app v0.1.0
└─ geometry v0.2.0 (0.2.0) [work]
$ rune doc geometry
```

> [!WARNING]
> **Trust**
>
> A registry added with `rune registry add` is not monitored: nothing reviews what it serves. The archive's checksum is checked against the index on every install, which catches a corrupted or tampered file, not a malicious package. Read what you depend on.
