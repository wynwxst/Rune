# Choosing a dependency's configuration

A package that depends on `gfx` says which backend it wants where it names the dependency, so one build of an application does not have to be one build of everything under it.

**Rune.toml**

```toml
[dependencies]
gfx = { path = "../gfx", config = { backend = "metal", tracing = true } }
```

```sh
$ rune add gfx --config backend=vulkan
```

The values a dependency was built with travel with it. A compiled `.rul` records them, and its interface is re-read with the answers *it* was built under — so a library's own `@Config` can never be re-decided by whoever imports it.

| Situation | What happens |
| --- | --- |
| a key the package never declared | an error naming the package and listing the keys it has |
| two dependents choosing differently for one package | an error naming both, since one build cannot be two things |
| nobody choosing | the default from `[config]` |
| a builtin key (`os`, `memory`, ...) | refused: the target already answers those |

> [!NOTE]
> **One mechanism, two spellings**
>
> A value key and a bare name are the same mechanism. `--cfg tracing` sets `tracing` with no value, which `@Config(tracing)` answers true and `@Config(tracing = false)` answers false.
