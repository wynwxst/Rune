//===- Fingerprint.h - Deciding what a rebuild has to redo -----*- C++ -*-===//
//
// A build step is worth skipping when nothing it depends on has changed.
// Timestamps answer a slightly different question — whether anything has been
// *written* — and get it wrong in both directions: touching a file, checking
// one out again, or restoring it from a cache all rewrite the timestamp
// without changing a byte, while a change that lands with an older timestamp
// than the output goes unnoticed.
//
// So a step is described by a digest of everything that could change what it
// produces — the command it runs, the bytes of every file it reads, the
// compiler that will read them — and that digest is written beside the output.
// The step reruns when the two disagree, which is exactly when its result
// would differ.
//
//===----------------------------------------------------------------------===//
#ifndef RUNE_PM_FINGERPRINT_H
#define RUNE_PM_FINGERPRINT_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rune::pm {

namespace fs = std::filesystem;

/// A 64-bit digest built from whatever a build step reads.
///
/// FNV-1a: this is change detection, not a defence against anyone choosing
/// their input to collide with a previous one, and it costs a few cycles per
/// byte over sources measured in kilobytes.
class Fingerprint {
public:
  /// Mixes in text — a command line, a flag, a target name.
  void add(std::string_view text);

  /// Mixes in a file's contents. A file that cannot be read mixes in that
  /// fact instead, so an input that disappears is a change like any other.
  void addFile(const fs::path &path);

  /// Mixes in a file's size and modification time rather than its bytes, for
  /// inputs too large to be worth reading — the compiler binary itself. Both
  /// change whenever it is rebuilt or reinstalled.
  void addStamp(const fs::path &path);

  uint64_t value() const { return Hash; }

  /// The digest as the hex string the stamp files carry.
  std::string hex() const;

private:
  uint64_t Hash = 0xcbf29ce484222325ull;
};

/// Where the digests for one build directory live.
///
/// One file per output rather than one index for all of them: several targets
/// are compiled at once, and separate files need no lock between them and
/// cannot leave a half-written index behind if a build is interrupted.
class FingerprintStore {
public:
  explicit FingerprintStore(fs::path buildDir)
      : Dir(std::move(buildDir) / ".fingerprints") {}

  /// True when `output` exists and was produced from exactly this input.
  bool isFresh(const fs::path &output, const Fingerprint &fp) const;

  /// Records that `output` now corresponds to `fp`. Called after the step
  /// succeeds, so a failed build leaves the old digest — and therefore the
  /// old verdict — in place.
  void record(const fs::path &output, const Fingerprint &fp) const;

private:
  fs::path stampFor(const fs::path &output) const;
  fs::path Dir;
};

/// The digest of the toolchain doing the compiling: the compiler binary and
/// every source file of the standard library, which is compiled from source
/// into everything it builds.
///
/// Computed once and remembered, since it is the same for every step of a
/// build. Without it, editing the standard library — or rebuilding the
/// compiler — leaves every package in the tree looking up to date while none
/// of them is.
const Fingerprint &toolchainFingerprint(const std::string &compiler,
                                        const std::string &stdlibDir);

} // namespace rune::pm

#endif
