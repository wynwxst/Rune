# Features and dependencies

`--cfg` on the command line and `cfg` in the manifest set names of your own. Every dependency's name is set too, so a package can ask whether it has one without anybody writing it down twice.

**Rune.toml**

```toml
[package]
name = "report"
version = "0.1.0"

[build]
cfg = ["telemetry"]

[dependencies]
statistics = { path = "../statistics" }
```

**What that makes true**

```rune
@Config(telemetry)
fn record(event: String) { /* ... */ }

@Config(!telemetry)
fn record(event: String) {}

// True exactly when `statistics` is in [dependencies].
@Config(statistics)
fn summarise() -> String { "with statistics" }
```

```sh
$ rune build                     # telemetry, statistics
$ rune build --cfg verbose       # telemetry, statistics, verbose
```

> [!NOTE]
> **Declarations only**
>
> `@Config` applies to declarations, not to statements. To make part of a body conditional, put it in a function of its own and give every branch a definition — which also means a missing case is a name that cannot be found, rather than silence.
