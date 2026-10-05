# Rust crates

A `[dependencies]` entry with `cargo = "path"` is a Rust crate, not a Rune
package. `loadManifest` puts it in `Manifest::CargoCrates` instead of
`Dependencies`, so package resolution never looks for a `Rune.toml` there.
`rune/src/Cargo.{h,cpp}` holds everything Rust-specific, and
`buildCargoCrates` in `main.cpp` runs it from `prepareInputs`, beside the
package's C and C++ sources.

## Building the crate

```
cargo rustc --lib --crate-type staticlib --manifest-path <crate>/Cargo.toml \
    --target-dir target/<profile>/cargo [--release] [--target <rust triple>] \
    [--features a,b] [--no-default-features] -- --print native-static-libs
```

- **`cargo rustc --crate-type`** rather than `cargo build`, so the crate needs no `crate-type = ["staticlib"]` and stays an ordinary library for its Rust users.
- **The library** is `<target-dir>/[<rust triple>/]<debug|release>/lib<name>.a`, where the name is `[lib] name` or the package name with `-` as `_` (`cargoLibraryName`).
- **Native libraries** are what rustc prints after `native-static-libs:` (`nativeStaticLibs`). Cargo prints that only when rustc runs, so it is kept in `<name>.native-libs` beside the library and read on every build. On Apple, `-lSystem`, `-lc` and `-lm` are dropped as they are read, because the C driver links libSystem already and naming it again only makes `ld` warn.
- **The Rust target** comes from the LLVM triple via `rustTargetFor`: `arm64-apple-macosx` becomes `aarch64-apple-darwin`, and a MinGW triple becomes `x86_64-pc-windows-gnu`.
- **The fingerprint** covers the command, `Cargo.toml`, `Cargo.lock`, `build.rs` and every file under `src/`. A crate that depends on another path crate is not rebuilt when only that one changes; Cargo would notice, but the fingerprint does not ask it.

The library goes into `DependencyInputs::Objects` and the native libraries into `LinkArgs`, so they reach every executable that depends on the package, the same way a C half's objects do.

## Reading the crate

`generateRustBindings` reads every `.rs` file under `src/` (sorted, for determinism) without a Rust compiler. The reading is token-level:

- **`lexRust`** keeps identifiers, punctuation, numbers, strings (raw, byte and ordinary), lifetimes and `///` lines, and drops every other comment. A lifetime and a character literal are told apart by whether a `'` follows the identifier.
- **`scanFile`** gathers attributes, then reads qualifiers (`pub`, `pub(crate)`, `unsafe`, `const`, `extern "ABI"`) and then the item keyword. Anything else clears what was gathered. `extern "C" { … }` blocks are skipped: those are the crate's *imports*. So is any `#[cfg(test)] mod`. Item bodies are skipped by bracket matching, which is why a string containing `#[no_mangle] fn` is never read as an item.
- **Exported** means `#[no_mangle]` (or `#[unsafe(no_mangle)]`, or `#[export_name]`) together with an `extern` ABI. The item's `pub` is irrelevant: the symbol is global whatever its visibility, so a function in a private module, or an associated function in an `impl`, is bound all the same.

`cfg` is not evaluated and `use` is not resolved. A type is recognised by the last segment of its path (`std::os::raw::c_int` is `c_int`). Both are fine for a C ABI, which has to be spelt out in full anyway.

## Writing the module

`TypeMapper` turns each type into its Rune spelling. When it can't, it leaves the type empty with a reason, and the item is listed under `// Not bound:` at the end of the file. The build prints a note pointing there whenever the binding is compiled again.

Structs are decided by a fixpoint. A struct holding an unbindable struct by value is itself unbindable, and the set is narrowed until it settles. Only then are functions mapped, so nothing names a type that was not written. A name that appears only behind a pointer becomes `pub type Name = u8`, an opaque handle; so does the `_private: [u8; 0]` idiom.

Safety follows the Rust author:

- **Safe wrapper.** A function that is not `unsafe fn` and whose *parameters* hold no address gets a `#safe` wrapper over a private `#as("__rust_<name>")` declaration. A returned pointer doesn't count, since only the callee vouches for it.
- **Plain C declaration.** Everything else goes in a `pub extern "C"` block under its own name, called from `unsafe` like C.

The module is written to `target/<profile>/cargo/<name>.rune` (only when its text changed) and compiled with the package's own flags into `deps/<name>.rul`. The flags have to match, because `--memory` must agree or the importer refuses the library. It is then staged like any dependency's library.

## Tested by

`tests/cargo_test.py` (CTest `rune_cargo`, skipped without `cargo`). It builds `tests/cargo/tricky`, a crate of awkward syntax plus items that must be left out, and `examples/rust_interop`, from copies. It checks the output, the reasons given for what was left out, that a no-op build starts neither Cargo nor the compiler, that a changed crate is rebuilt and bound again, and that a missing crate is reported.
