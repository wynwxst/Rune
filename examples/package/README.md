# A package ecosystem, and the registries that serve it

Seven packages that depend on each other the way real ones do, two programs
that use them, two registries built from them that any `rune` can install
from, and a script that proves the whole thing works. It is the worked
example for [Packages and registries](../../docs/rune-reference.html#packages)
in the reference, and a test bed for `rune pkg`, `add`, `update` and friends.

```
packages/<name>/<version>/     the packages, one directory per released version
lab/<name>/<version>/          the lab's packages: its own logger 1.0.0
apps/dashboard/                uses report, shapes and the lab's logger — the whole graph
apps/legacy/                   pins units to 1.0.0, so two versions coexist
registry/                      "ecosystem": built from packages/ by ecosystem.py; served as is
registry-lab/                  "lab": built from lab/
ecosystem.py                   build | serve [port] | check
```

Two registries, because a registry's *name* is what tells one from another:
both carry a `logger`, and `apps/dashboard` says which it means with
`logger = { version = "1.0", registry = "lab" }`. `rune deps` and
`rune installed` show where each package came from.

## The graph

```
dashboard
├─ logger 1.0.0             [lab]   asked for as lab::logger
├─ report 1.2.0
│  ├─ plot 0.5.0
│  │  ├─ geometry 0.3.0
│  │  └─ stats 2.1.0        plot asks for "2.0"
│  ├─ stats 2.1.0           report asks for "2.1"  — one version serves both
│  └─ units 1.1.0
└─ shapes 1.0.0
   └─ geometry 0.3.0

legacy
├─ stats 2.1.0
└─ units 1.0.0              pinned with "=1.0.0": installed beside 1.1.0
```

| Package | Is for | Versions | Depends on |
|---|---|---|---|
| `units` | lengths, and since 1.1.0 temperatures, with their unit attached | 1.0.0, 1.1.0 | — |
| `geometry` | points and vectors on the plane | 0.3.0 | — |
| `stats` | mean, median, spread; since 2.1.0 percentiles and histograms | 2.0.0, 2.1.0 | — |
| `logger` | a leveled logger with a threshold | 0.9.0 in `ecosystem`; 1.0.0 in `lab`, with `Trace` and `summary()` | — |
| `shapes` | circles, rectangles, polygons — a `Shape` mark | 1.0.0 | geometry `0.3` |
| `plot` | bar charts, sparklines, scatter plots in characters | 0.5.0 | stats `2.0`, geometry `0.3` |
| `report` | text reports with statistics, units and charts | 1.2.0 | stats `2.1`, units `1.1`, plot `0.5` |

Every package has a `docs/index.md` and a `tests/` directory, so `rune doc`
and `rune test` have something to do in each.

## Trying it

```bash
# From this directory, with rune on PATH.
python3 ecosystem.py build            # packages/ -> registry/, lab/ -> registry-lab/   (already done)
python3 ecosystem.py serve 7878       # both registries: ecosystem on 7878, lab on 7879

# In another shell — or use file://$PWD/registry and file://$PWD/registry-lab
# and skip the servers. Each registry is added under the name it declares.
rune pkg server add http://localhost:7878
rune pkg server add http://localhost:7879
rune pkg server list
rune search 'stat|plot'
rune search --registry lab .
rune desc logger                      # the lab's 1.0.0, and "also in: ecosystem (0.9.0)"
rune desc ecosystem::logger           # the main registry's 0.9.0

cd apps/dashboard
rune run                              # installs report, shapes, lab::logger and what they need
rune deps                             # the tree, each package tagged with its registry
rune installed                        # every version, how many projects use it, and from where
rune installed --registry lab
rune doc shapes                       # the documentation of the installed shapes
rune doc ecosystem::logger            # fetches 0.9.0 to read about it
```

`python3 ecosystem.py check` does all of that under a temporary `RUNE_HOME`:
it builds both registries, adds them, runs every package's tests with their
dependencies installed *from the registries*, builds and runs both apps, and
prints the trees. It is also the CTest test `rune_ecosystem`.

## What it exercises

- **A diamond.** `report` needs `stats 2.1` and, through `plot`, `stats 2.0`.
  One version serves both — 2.1.0 — because a `.rul` is one module and two
  versions of it cannot both be linked.
- **Two versions installed at once.** `legacy` pins `units = "=1.0.0"`;
  `dashboard` gets 1.1.0 through `report`. Both sit under
  `~/.rune/pkg/units/`, each referenced by the project that uses it.
- **A version that stays put.** Adding a package to `dashboard` does not move
  `stats` or `units`; only `rune update` does.
- **Packages that test against the registry.** Each package's `tests/` build
  against dependencies installed from `registry/`, exactly as a downstream
  user's would.
- **Two registries, told apart by name.** Both have a `logger`. `dashboard`
  names the lab's — `rune add lab::logger` wrote `registry = "lab"` into its
  manifest — and the resolver takes it from there alone. `rune desc logger`
  shows both and says how to ask for the other.
- **A vector as a slice.** `report` hands `stats::histogram`'s vector to
  `plot::sparkline`, which takes a `[f64]`, through `Vector::asSlice` — no
  copy.

## Releasing a new version

Copy `packages/<name>/<old>/` to `packages/<name>/<new>/` (or under `lab/`
for that registry), edit `version` in its `Rune.toml`, make the change, and
rebuild the registries:

```bash
python3 ecosystem.py build
```

`rune update` in an app then picks the new version up, provided its
requirement allows it: `"2.0"` accepts 2.1.0 but not 3.0.0.
