//===- Library.h - The .rul Rune library container -------------*- C++ -*-===//
//
// A `.rul` file is a small container holding
//
//   * the compiled object code for the module, and
//   * the module's public interface, kept as Rune source.
//
// Storing source rather than a serialised symbol table means importers get the
// exact declarations the author wrote, including generic bodies, which
// monomorphisation needs. Non-public items are stripped on the way in, so a
// library never leaks its internals.
//
// Layout — every integer little-endian, every string a `u32` length followed
// by that many bytes of UTF-8, and no alignment or padding anywhere:
//
//   magic       8 bytes  "RUNELIB\1"
//   version     u32      the container's own version; 3 today
//   memory      u32      0 = reference counting, 1 = Zombie
//   name        string   the library's module name
//
//   flagCount   u32      `#Config` names that were set when this was built
//     flag      string
//   valueCount  u32      `#Config` keys that had values
//     key       string
//     value     string
//
//   unitCount   u32      one per module compiled into the library
//     path      string   the dotted module path, e.g. "geometry::shapes"
//     source    string   its **public interface**, as Rune source
//
//   objectLen   u64
//   object      bytes    a native object file for one target
//
// Three things are worth knowing about it.
//
// **The interface is source, not a symbol table.** An importer re-parses it,
// so it gets the declarations exactly as they were written — including
// generic bodies, which monomorphisation needs, and `pub macro` definitions,
// which expansion needs. `publicInterfaceOf` strips everything not `pub` on
// the way in, so a library never carries its internals.
//
// **The conditions travel with it.** The interface is source, so its
// `#Config` conditions have to be answered again on import — and answered the
// way they were when the object code was made, not the way the importer's own
// build would answer them. That is what `flagCount` and `valueCount` are for.
//
// **A `.rul` is one target and one memory model.** The object code bakes in
// retains and releases, or their absence and the moved-in argument
// convention, so importing a library built the other way is refused rather
// than linked. There is no fat `.rul`: cross-compiling means building the
// dependency for that target too.
//
// The version is checked exactly, not for a range: a mismatch says "built by
// a different compiler version" and stops. The format is small enough that
// rebuilding is always the right answer.
//
//===----------------------------------------------------------------------===//
#ifndef RUNE_LIBRARY_H
#define RUNE_LIBRARY_H

#include "rune/Diagnostics.h"
#include "rune/Driver.h"

#include <string>
#include <utility>
#include <vector>

namespace rune {

struct LibraryContents {
  std::string ModuleName;
  /// The memory model the object code was compiled for. Reference-counted
  /// code retains its parameters and releases on the way out; Zombie code
  /// does neither, so the two cannot share a link. Checked on import.
  MemoryMode Memory = MemoryMode::Arc;
  /// One entry per module compiled into the library: its dotted module path
  /// and the Rune source that declares it.
  std::vector<std::pair<std::string, std::string>> Interfaces;
  /// The `#Config` answers this library was built with: the names that were
  /// set, and the keys that had values. An importer reads the interface as
  /// source, so its conditions have to be answered the way they were when
  /// the object code was produced — not the way the importer's own build
  /// would answer them.
  std::vector<std::string> ConfigFlags;
  std::vector<std::pair<std::string, std::string>> ConfigValues;
  std::string ObjectCode;
};

/// Writes `path` from the object file at `objectPath` plus the given sources.
bool writeLibrary(const std::string &path, const std::string &objectPath,
                  const std::string &moduleName,
                  const std::vector<std::pair<std::string, std::string>> &units,
                  MemoryMode memory, const CompilerOptions &opts,
                  DiagnosticEngine &diags);

/// Reads a `.rul`. Returns false and reports if the file is missing or
/// malformed.
bool readLibrary(const std::string &path, LibraryContents &out,
                 DiagnosticEngine &diags);

/// Extracts just the object code to `objectPath`, for handing to the linker.
bool extractLibraryObject(const std::string &path, const std::string &objectPath,
                          DiagnosticEngine &diags);

/// Reduces a module's source to the declarations an importer may see: `pub`
/// items keep their bodies (generics need them), everything else is dropped.
std::string publicInterfaceOf(const std::string &source);

} // namespace rune

#endif
