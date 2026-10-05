# Calling Rust

A Rust crate is a dependency like any other: name it with `cargo = "..."` in `[dependencies]`, and `import` it. Cargo builds it; `rune` reads what it exports over the C ABI into a Rune module.

## Pages

- [A crate as a dependency](a_crate_as_a_dependency.md)
- [What is bound](what_is_bound.md)
- [Bindings on their own: `rune ffi rust`](bindings_on_their_own_rune_ffi_rust.md)
- [Ownership across the line](ownership_across_the_line.md)
