# The example ecosystem

`examples/package/` has seven packages — `units` and `stats` in two versions each, `geometry`, `logger`, `shapes`, `plot` and `report`, with a diamond through `stats` — two programs, two registries built from them (`ecosystem`, and `lab` with its own `logger 1.0.0` that `dashboard` asks for by name), and a script that builds them, serves them, or proves the whole cycle under a temporary `RUNE_HOME`. It is also the CTest test `rune_ecosystem`.

```sh
$ cd examples/package
$ python3 ecosystem.py check
== building the registries
● Added geometry v0.3.0 (7.5 KB, sha256 …)
…
== adding them to a fresh client
● Added registry 'ecosystem' at file:///…/registry (9 releases)
● Added registry 'lab' at file:///…/registry-lab (1 release)
== every package's tests, against dependencies from the registries
  ecosystem::geometry 0.3.0: ● Test result: 1 file(s) passed, 0 failed
  ecosystem::report 1.2.0:   ● Test result: 1 file(s) passed, 0 failed
  lab::logger 1.0.0:         ● Test result: 1 file(s) passed, 0 failed
  …
== the apps
-- dashboard: rune run
   Shapes
   ======
   1. Circles
      circle r=1.0 at (0.0, 0.0)
      …
-- dashboard: rune deps
   dashboard v0.1.0
   ├─ logger v1.0.0 (1.0) [lab]
   ├─ report v1.2.0 (1.2.0) [ecosystem]
   …
== what is installed, and who uses it
geometry v0.3.0  4 projects  [ecosystem]
logger v1.0.0    1 project   [lab]
stats v2.1.0     4 projects  [ecosystem]
units v1.0.0     1 project   [ecosystem]
units v1.1.0     2 projects  [ecosystem]
ecosystem: every package tests clean and both apps run
```
