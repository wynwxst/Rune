# Keys with a value of your own

A name that is either set or not answers a yes-or-no question. A key with a value answers a *which* question, which is what a package wants when it has three backends rather than one optional one. Declare the keys a package understands, and their defaults, in a `[config]` table; every one is then comparable in a condition.

**Rune.toml**

```toml
[package]
name = "gfx"
version = "0.1.0"

[config]
backend = "software"
api_level = 1
tracing = false
```

**What a value key answers**

```text
@Config(backend == "metal")
fn present() -> String { "metal" }

@Config(backend == "vulkan")
fn present() -> String { "vulkan" }

@Config(backend != "metal" && backend != "vulkan")
fn present() -> String { "software" }

// `=` reads as the comparison: there is nothing else it could mean in a
// condition, and `tracing = true` is how the key was written in the manifest.
@Config(tracing = true)
fn trace(what: String) { /* ... */ }

@Config(tracing = false)
fn trace(what: String) {}

@Config(api_level == 3)
fn modern() -> bool { true }
```

The value may be a string, a number, a boolean or a bare word, and either side of the comparison may be the key — `@Config("metal" == backend)` says the same thing. A key the package never declared is an error rather than a silently false condition, so a typo is caught where it is written.

```sh
$ runec --cfg backend=metal --cfg api_level=3 --cfg tracing=true app.rune
$ rune build --cfg backend=vulkan
```
