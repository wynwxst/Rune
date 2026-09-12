# Using a package

A dependency is a path, for a package on this disk, or a version, for one from a registry. Either way `rune build` builds it first and passes its `.rul` — and every native library it asked for — down to whatever depends on it.

```sh
[dependencies]
geometry = { path = "../geometry" }              # while both are being written
shapes = "1.0"                                   # from a registry: ^1.0, the newest 1.x
logger = { version = "1.0", registry = "lab" }   # from that registry alone
```

`rune add` writes the line, installs the package and everything it needs, and pins the result in `Rune.lock`. The other commands work on the same three things — manifest, lock, install store.

| Command | Manifest | Lock | Store |
| --- | --- | --- | --- |
| `rune add shapes[@1.0]` | adds `shapes = "…"` | pins shapes and what it needs | installs what is missing |
| `rune build` / `run` / `test` | — | written if a dependency is not yet pinned | installs what is missing |
| `rune update [shapes]` | — | moves to the newest allowed | installs what is missing |
| `rune remove shapes` | drops the line | unpins what nothing needs | drops this project's reference |
| `rune remove` | — | — | uninstalls every version no project uses |
| `rune deps` | reads | reads | reads |
| `rune installed` | — | — | lists every version and who uses it |
| `rune doc shapes` | — | reads, to pick the version | reads; installs the newest if none is there |

```sh
$ rune add report
○ Fetching report v1.2.0
○ Fetching plot v0.5.0
○ Fetching stats v2.1.0
○ Fetching units v1.1.0
○ Fetching geometry v0.3.0
● Installed geometry v0.3.0
● Installed units v1.1.0
● Installed stats v2.1.0
● Installed plot v0.5.0
● Installed report v1.2.0
● Added report "1.2.0" to Rune.toml (v1.2.0 installed)
$ rune deps
dashboard v0.1.0
└─ report v1.2.0 (1.2.0)
   ├─ plot v0.5.0 (0.5)
   │  ├─ geometry v0.3.0 (0.3)
   │  └─ stats v2.1.0 (2.0)
   ├─ stats v2.1.0 (2.1)
   └─ units v1.1.0 (1.1)
```

One version of a package serves everyone in a build: `plot` asks for `stats 2.0` and `report` for `stats 2.1`, and 2.1.0 satisfies both. Two requirements no single version satisfies are an error that names them. Two *projects* may want different versions — `examples/package/apps/legacy` pins `units = "=1.0.0"` while `dashboard` uses 1.1.0 — and both are installed, each referenced by the project that uses it.

> [!NOTE]
> **The lock**
>
> Commit `Rune.lock`. A build on another machine then fetches exactly the versions this one resolved, and `rune update` is the only thing that moves them.

A package's documentation is a command away, whether or not the project here uses it. `rune doc shapes` builds and opens the documentation of the copy this project's lock pins — or, outside a project, the newest version installed — and when nothing is installed it fetches the newest release a registry has, which then sits in the store unreferenced until `rune remove` reclaims it. `rune doc lab::logger` reads the copy from one registry; `--no-open` only says where the page was written.
