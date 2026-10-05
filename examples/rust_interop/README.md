# rust_interop

A Rune program calling a Rust crate. `rust/geometry` is an ordinary Cargo
crate; `Rune.toml` names it as

```toml
[dependencies]
geometry = { cargo = "rust/geometry" }
```

and `rune build` does the rest. Cargo builds the crate as a static library,
and what it exports over the C ABI is written into the Rune module
`geometry`, in `target/debug/cargo/geometry.rune`. That covers its
`#[no_mangle] extern "C"` functions, `#[repr(C)]` structs and enums, and
constants.

```sh
rune run                      # add: 42, distance: 5.0, … polygon area: 12.0
rune build --target windows   # cross-compiled; run it under wine
```

`src/main.rune` shows the shapes this takes:

- safe calls;
- structs by value;
- an enum;
- a Rune function as a C callback;
- a string Rust allocates and frees;
- an opaque Rust object owned by a Rune struct whose `deinit` gives it back.

The language reference's *Calling Rust* page has the full mapping.
