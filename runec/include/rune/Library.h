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
// Layout (all integers little-endian):
//
//   magic      8 bytes  "RUNELIB\1"
//   version    u32
//   nameLen    u32      module name, UTF-8
//   name       bytes
//   unitCount  u32      number of interface units
//     nameLen  u32
//     name     bytes    module path, e.g. "geometry::shapes"
//     unitLen  u32
//     unit     bytes    Rune source for that module
//   objectLen  u64
//   object     bytes    native object file
//
//===----------------------------------------------------------------------===//
#ifndef RUNE_LIBRARY_H
#define RUNE_LIBRARY_H

#include "rune/Diagnostics.h"

#include <string>
#include <utility>
#include <vector>

namespace rune {

struct LibraryContents {
  std::string ModuleName;
  /// One entry per module compiled into the library: its dotted module path
  /// and the Rune source that declares it.
  std::vector<std::pair<std::string, std::string>> Interfaces;
  std::string ObjectCode;
};

/// Writes `path` from the object file at `objectPath` plus the given sources.
bool writeLibrary(const std::string &path, const std::string &objectPath,
                  const std::string &moduleName,
                  const std::vector<std::pair<std::string, std::string>> &units,
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
