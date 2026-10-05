# rune ffi rust: Rune bindings for a Rust crate

```sh
rune ffi rust rust/mycrate              # the module, to standard output
rune ffi rust . -o src/mycrate.rune     # into a file
rune ffi rust . --target x86_64-pc-windows-gnu
```

A Rust crate already speaks C, as long as it exports `#[no_mangle] pub extern
"C"` functions and `#[repr(C)]` types. `rune ffi rust` reads those from the
crate's source and writes the Rune module that declares them. It is the
same module `rune build` writes for a `[dependencies] name = { cargo = "..." }`
entry. Use it on its own to read the bindings before depending on a crate, or
for a crate built some other way.

It needs neither libclang nor a Rust compiler: the crate's `.rs` files under
`src/` are read directly. What it binds, how each Rust type is spelt in Rune,
and when a function is safe to call without `unsafe` is all on the language
reference's *Calling Rust* page.

## Options

| Option | Does |
| --- | --- |
| `[<crate dir>]` | the directory holding `Cargo.toml`; `.` by default |
| `-o`, `--output <file>` | write the module there instead of to standard output |
| `--target <triple>` | the target the crate is built for: on Windows `c_long` is 32 bits |

A summary goes to standard error: how many functions were bound, and how
many items were left out. The module itself ends with a `// Not bound:` list
saying why for each one, such as a generic, an `enum` with data, or a `String`
by value.

## Linking it yourself

The module declares functions; the crate's static library defines them.
Without `rune build`, build the library with

```sh
cargo rustc --lib --crate-type staticlib --release -- --print native-static-libs
```

and pass the library and the system libraries rustc lists to the link:
`--link-arg target/release/libmycrate.a`, and `-l` for each one.

## A crate from crates.io

A published crate rarely exports a C ABI of its own. The way in is a small
wrapper crate that depends on it and exports what Rune needs. Its own types
(`regex::Regex`, `serde_json::Value`) travel as opaque handles; strings
travel as `*const c_char` in and `*mut c_char` out, with a function to free
the latter:

```rust
use regex::Regex;
use std::ffi::{c_char, CStr};

#[no_mangle]
pub unsafe extern "C" fn rx_new(pattern: *const c_char) -> *mut Regex {
    match Regex::new(CStr::from_ptr(pattern).to_str().unwrap_or("")) {
        Ok(r) => Box::into_raw(Box::new(r)),
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn rx_free(rx: *mut Regex) {
    if !rx.is_null() { drop(Box::from_raw(rx)) }
}
```

`Regex` becomes `pub type Regex = u8` on the Rune side, and a Rune struct with
a `deinit` that calls `rx_free` owns it.
