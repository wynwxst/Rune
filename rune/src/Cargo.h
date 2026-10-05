//===- Cargo.h - Rust crates as Rune dependencies ---------------*- C++ -*-===//
//
// A Rust crate joins a Rune build the way a C library does: through the C
// ABI. Rust already speaks it — `#[no_mangle] pub extern "C" fn` is a C
// function, `#[repr(C)] struct` is a C struct — so what is missing is the
// Rune side's declarations, and a static library to link.
//
// Cargo makes the library: `cargo rustc --lib --crate-type staticlib`, so the
// crate needs no `crate-type` of its own and can stay an ordinary `rlib` for
// its Rust users. The declarations are read from the crate's source here,
// without a Rust compiler's help: exported functions, `#[repr(C)]` structs
// and fieldless enums, and `pub const`s with literal values. What cannot be
// expressed in Rune — a generic, a type with no C layout passed by value — is
// left out with a note saying why, rather than declared wrongly.
//
//===----------------------------------------------------------------------===//
#ifndef RUNE_PM_CARGO_H
#define RUNE_PM_CARGO_H

#include <string>
#include <vector>

namespace rune::pm {

/// The Rune module standing for a crate: its source, and what was left out
/// of it and why.
struct RustBindings {
  std::string Source;
  std::vector<std::string> Skipped;
  size_t Functions = 0;
};

/// Reads every `.rs` file under `crateDir/src` and writes the Rune module
/// `moduleName`'s source declaring what the crate exports over the C ABI.
/// `windows` says how wide `c_long` is: 32 bits there, the pointer elsewhere.
RustBindings generateRustBindings(const std::string &crateDir,
                                  const std::string &moduleName,
                                  bool windows);

/// The file name Cargo gives the crate's library, without `lib` or `.a`:
/// `[lib] name`, or the package name with `-` as `_`. Empty, with `error`
/// set, when `Cargo.toml` cannot be read.
std::string cargoLibraryName(const std::string &crateDir, std::string &error);

/// The Rust target for an LLVM triple: `arm64-apple-macosx` is
/// `aarch64-apple-darwin` to Cargo, a MinGW triple is `-pc-windows-gnu`.
std::string rustTargetFor(const std::string &llvmTriple);

/// The native libraries a Rust static library needs at link time, out of
/// what `--print native-static-libs` wrote: `-lSystem`, `-framework
/// CoreFoundation`, and so on, as separate arguments.
std::vector<std::string> nativeStaticLibs(const std::string &rustcOutput);

} // namespace rune::pm

#endif
