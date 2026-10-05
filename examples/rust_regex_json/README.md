# rust_regex_json

Two popular crates.io libraries, `regex` and `serde_json`, called from Rune.

Neither exports a C ABI of its own, so `rust/` is a small wrapper crate
(`tk`) that depends on both and exports 20 `extern "C"` functions. The
libraries' own types, `regex::Regex` and `serde_json::Value`, travel as opaque
handles. `Rune.toml` names it as `tk = { cargo = "rust" }`, and `rune build`
does the rest: Cargo fetches and builds the crates, and the bindings are
written to `target/debug/cargo/tk.rune`. `rune ffi rust rust` prints the same
bindings on their own.

```sh
rune run                      # needs network the first time, for crates.io
rune build --target windows   # cross-compiled; run it under wine
```

`src/main.rune` wraps each handle in a Rune struct whose `deinit` frees it,
and takes ownership of every string Rust returns. It covers matching,
finding, replacing, capture groups through a callback and Unicode classes,
then JSON parsing, pointer lookups, editing and printing, plus the error
path of each.
