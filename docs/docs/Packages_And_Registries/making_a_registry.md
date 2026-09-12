# Making a registry

A registry is a directory: an `index.toml` listing every release with its checksum and dependencies, and a `packages/` tree of plain `tar` archives. `rune pkg init` makes an empty one; `--addPackage` packs a project — `Rune.toml`, `src/`, `docs/`, `tests/`, its C sources, its README and LICENSE, never `target/` — and adds the release to the index. The version comes from the manifest, and a version already there is refused unless `--force` says to replace it.

```sh
$ rune pkg init registry --name ecosystem
● Created registry 'ecosystem' in /work/registry
$ rune pkg server --addPackage ../geometry --dir registry
○ Packing geometry v0.3.0
● Added geometry v0.3.0 (7.5 KB, sha256 2980aae944e4…)
$ rune pkg server --addPackage ../shapes --dir registry
$ cat registry/index.toml
[registry]
name = "ecosystem"
format = 1
updated = "2026-09-11T10:16:41Z"

[[release]]
name = "geometry"
version = "0.3.0"
description = "Points and vectors on the plane: distances, dot products, and the arithmetic between them"
authors = ["The Rune examples"]
license = "MIT"
archive = "packages/geometry/geometry-0.3.0.tar"
sha256 = "2980aae944e4401d744e9f9914d3119a344a968be979b125b0d5b5e74ba982cc"
size = 7680
added = "2026-09-11T10:16:41Z"
dependencies = []

[[release]]
name = "shapes"
version = "1.0.0"
…
dependencies = ["geometry 0.3"]
```

Because it is files, there are three ways to make it reachable, and they need nothing in common:

| Reach it as | Set up with | Good for |
| --- | --- | --- |
| a directory | `rune pkg server add /shared/registry` | a team on one machine or a shared drive |
| `file://…` | `rune pkg server add file:///shared/registry` | the same, spelled as a URL |
| `http://host:port` | `rune pkg server --serve --dir registry --port 7878`, then `rune pkg server add http://host:7878` | a network; any static web server works too |

*Mirroring a registry is copying the directory.*

```sh
$ rune pkg server --serve --dir registry
● Serving /work/registry at http://localhost:7878/
  ─  note: add it to a client with `rune pkg server add http://<this host>:7878`; Ctrl-C stops it

$ rune pkg server add http://localhost:7878
● Added registry 'ecosystem' at http://localhost:7878 (2 releases)
● this registry is not monitored: nothing here reviews what it serves, so read a package before you depend on it
$ rune search 'geo|shape'
geometry  v0.3.0     Points and vectors on the plane: distances, dot products, and the arithmetic between them
shapes    v1.0.0     Circles, rectangles and polygons over geometry: area, perimeter, containment and bounding boxes
```

The server is a static file server with a front page listing what it holds; its `-v` prints each request. A client keeps the registries it uses in `~/.rune/registries.toml` — by name, URL, and the name the registry declares when an alias differs — caches each index under `~/.rune/cache/`, and works from the cached copy when a registry cannot be reached.
