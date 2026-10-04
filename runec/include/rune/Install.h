//===- Install.h - Where the toolchain's files are, installed or not -*- C++ -*-===//
//
// A toolchain built from source finds its standard library, runtime and
// tool sources where the build left them — paths compiled in. An installed
// one, unpacked from a release archive anywhere at all, finds them beside
// itself instead:
//
//     <prefix>/bin/rune, runec, rune-lint, ...
//     <prefix>/lib/rune/libruneruntime.a
//     <prefix>/share/rune/stdlib/        the standard library
//     <prefix>/share/rune/runetime/      the Rune half of the runtime
//     <prefix>/share/rune/tools/         the tools' sources and books
//
// The installed layout wins when it is there, so a release needs no
// configuration and no environment variables.
//
//===----------------------------------------------------------------------===//
#ifndef RUNE_INSTALL_H
#define RUNE_INSTALL_H

#include <filesystem>
#include <string>

namespace rune {

/// `<prefix>/share/rune` when the binary in `exeDir` is an installed one,
/// else empty.
inline std::filesystem::path installedShare(const std::filesystem::path &exeDir) {
  std::error_code ec;
  std::filesystem::path p = exeDir / ".." / "share" / "rune";
  if (std::filesystem::is_directory(p / "stdlib", ec))
    return std::filesystem::weakly_canonical(p, ec);
  return {};
}

/// `<prefix>/lib/rune` when it holds the runtime, else empty.
inline std::filesystem::path installedRuntime(const std::filesystem::path &exeDir) {
  std::error_code ec;
  std::filesystem::path p = exeDir / ".." / "lib" / "rune";
  if (std::filesystem::is_directory(p, ec))
    return std::filesystem::weakly_canonical(p, ec);
  return {};
}

/// The standard library: installed, else where the build found it.
inline std::string stdlibDirFor(const std::filesystem::path &exeDir, const char *built) {
  std::filesystem::path share = installedShare(exeDir);
  return share.empty() ? std::string(built) : (share / "stdlib").string();
}

/// The runtime libraries: installed, else the build tree's.
inline std::string runtimeDirFor(const std::filesystem::path &exeDir, const char *built) {
  std::filesystem::path lib = installedRuntime(exeDir);
  return lib.empty() ? std::string(built) : lib.string();
}

/// `runetime/`, the runtime's Rune half: installed, else the source tree's.
inline std::string runetimeDirFor(const std::filesystem::path &exeDir, const char *built) {
  std::filesystem::path share = installedShare(exeDir);
  return share.empty() ? std::string(built) : (share / "runetime").string();
}

/// The root the tools' sources and books are under: `share/rune` when
/// installed (it holds `tools/` and `runetime/`), else the source tree.
inline std::string toolchainRootFor(const std::filesystem::path &exeDir, const char *built) {
  std::filesystem::path share = installedShare(exeDir);
  return share.empty() ? std::string(built) : share.string();
}

} // namespace rune

#endif
