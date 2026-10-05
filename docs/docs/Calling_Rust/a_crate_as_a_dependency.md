# A crate as a dependency

**Rune.toml**

```toml
[dependencies]
geometry = { cargo = "rust/geometry" }                  # a Cargo crate on this disk
fast = { cargo = "../fast", features = ["simd"] }       # with features
lean = { cargo = "../lean", default-features = false }
```

`rune build` runs `cargo rustc --lib --crate-type staticlib` on the crate, so it needs no `crate-type` of its own and can stay an ordinary library for its Rust users. The static library is linked into every program that depends on the package, together with the native libraries Rust's standard library needs, which rustc reports. A cross build passes the matching `--target` to Cargo — `aarch64-apple-darwin`, `x86_64-pc-windows-gnu` — whose standard library has to be installed with `rustup target add`.

Both steps are fingerprinted over the crate's `Cargo.toml`, `Cargo.lock`, `build.rs` and everything under `src/`, so a build with nothing changed starts neither Cargo nor the compiler.
