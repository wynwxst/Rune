# Which registry

Every registry has a name: the one its `index.toml` declares, which `rune pkg init` takes from the directory or `--name`. A client adds a registry under that name, or under an alias of its own with `--name`, and the name is how the two are told apart from then on. Once added, a registry stays added — `~/.rune/registries.toml` keeps it for every project on the machine — until `rune pkg server remove` drops it.

```sh
$ rune pkg server add http://packages.example.org
● Added registry 'example' at http://packages.example.org (214 releases)
$ rune pkg server add file:///Volumes/shared/lab --name lab
● Added registry 'lab' at file:///Volumes/shared/lab (3 releases)
  ─  note: it calls itself 'research-lab'; here it is `lab`, as in `rune add lab::<package>`
$ rune pkg server list
example  http://packages.example.org    214 releases
lab      file:///Volumes/shared/lab     3 releases  calls itself 'research-lab'
$ rune pkg server remove lab
● Removed registry 'lab' (file:///Volumes/shared/lab)
```

Two registries may both have a `logger`. Left to itself, `rune add logger` takes the newest version any of them offers; `rune add lab::logger` takes it from `lab` alone, and the manifest records the choice as `logger = { version = "1.0", registry = "lab" }`, so a build anywhere makes the same one. A package that names a registry for one of its own dependencies carries that into the index — `dependencies = ["lab::logger 1.0"]` — and the resolver honours it transitively. Two askers naming different registries for one package is a conflict, reported like any other: one installed copy cannot come from both.

| Spelling | Means |
| --- | --- |
| `rune add logger` | from whichever registry has the newest version |
| `rune add lab::logger`, `rune add --registry lab logger` | from `lab`; recorded in the manifest |
| `rune search lab::.`, `rune search --registry lab` | only what `lab` has |
| `rune desc logger` | the newest anywhere, and `also in:` the others |
| `rune desc ecosystem::logger` | that registry's copy, its versions alone |
| `rune installed --registry lab` | what came from `lab` |
| `rune update --registry lab` | move only what came from `lab` |
| `rune deps --registry lab` | the pinned packages that came from `lab` |
| `rune doc lab::logger` | the documentation of `lab`'s copy |

```sh
$ rune desc logger
logger v1.0.0
  The leveled logger, as the lab ships it: a Trace level below Debug, and a summary of what got through
  registry:     lab (file:///work/registry-lab)
  versions:     1.0.0
  also in:      ecosystem (0.9.0) — `rune add ecosystem::logger` takes it from there
  …
$ rune add lab::logger
● Added logger "1.0.0" from lab to Rune.toml (v1.0.0 installed)
$ rune deps
dashboard v0.1.0
├─ logger v1.0.0 (1.0) [lab]
└─ report v1.2.0 (1.2.0) [ecosystem]
   …
```

> [!NOTE]
> **Names**
>
> A name has to fit in front of `::package`: letters, digits, `_`, `-` and `.`. A registry whose index declares none is refused — a registry must provide a name — and two registries cannot be known by the same one; alias the second with `--name`.
