//===- Source.h - Source buffers, locations and ranges ---------*- C++ -*-===//
//
// The Rune compiler tracks every byte it reads through a SourceManager. A
// SourceLoc is a 32-bit offset into a global, virtual "source space" that is
// partitioned between the loaded files, which keeps AST nodes small.
//
//===----------------------------------------------------------------------===//
#ifndef RUNE_SOURCE_H
#define RUNE_SOURCE_H

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace rune {

/// An opaque offset into the SourceManager's global coordinate space.
class SourceLoc {
public:
  SourceLoc() = default;
  explicit SourceLoc(uint32_t offset) : Offset(offset) {}

  bool isValid() const { return Offset != kInvalid; }
  uint32_t raw() const { return Offset; }

  SourceLoc offsetBy(int32_t delta) const {
    return SourceLoc(static_cast<uint32_t>(static_cast<int64_t>(Offset) + delta));
  }

  bool operator==(const SourceLoc &o) const { return Offset == o.Offset; }
  bool operator!=(const SourceLoc &o) const { return Offset != o.Offset; }
  bool operator<(const SourceLoc &o) const { return Offset < o.Offset; }
  bool operator<=(const SourceLoc &o) const { return Offset <= o.Offset; }

private:
  static constexpr uint32_t kInvalid = 0xFFFFFFFFu;
  uint32_t Offset = kInvalid;
};

/// A half-open [Begin, End) span of source text.
class SourceRange {
public:
  SourceRange() = default;
  SourceRange(SourceLoc begin, SourceLoc end) : Begin(begin), End(end) {}
  explicit SourceRange(SourceLoc loc) : Begin(loc), End(loc.offsetBy(1)) {}

  bool isValid() const { return Begin.isValid() && End.isValid(); }
  SourceLoc begin() const { return Begin; }
  SourceLoc end() const { return End; }
  uint32_t length() const { return End.raw() - Begin.raw(); }

  /// The smallest range covering both operands. Invalid operands are ignored,
  /// so `a.merge(b)` is safe when either side came from a recovered parse.
  SourceRange merge(SourceRange other) const {
    if (!isValid()) return other;
    if (!other.isValid()) return *this;
    return SourceRange(Begin < other.Begin ? Begin : other.Begin,
                       other.End < End ? End : other.End);
  }

private:
  SourceLoc Begin, End;
};

/// One file (or in-memory buffer) loaded into the SourceManager.
struct SourceFile {
  unsigned ID = 0;
  std::string Path;      ///< Path as written on the command line.
  std::string Name;      ///< Basename, used in diagnostics.
  std::string Buffer;    ///< Full text, always NUL-terminated.
  uint32_t StartOffset = 0;
  std::vector<uint32_t> LineStarts; ///< Buffer-relative offset of each line.
};

/// Human-readable position: 1-based line, 0-based column (matching the
/// `file [line:colStart..colEnd]` header style used by the diagnostic printer).
struct PresumedLoc {
  const SourceFile *File = nullptr;
  unsigned Line = 0;
  unsigned Column = 0;
  bool isValid() const { return File != nullptr; }
};

class SourceManager {
public:
  /// Reads `path` from disk. Returns nullopt if the file cannot be opened.
  std::optional<unsigned> loadFile(const std::string &path);

  /// Registers an in-memory buffer (used by tests and the REPL).
  unsigned addBuffer(std::string name, std::string contents);

  const SourceFile &file(unsigned id) const { return Files[id]; }
  unsigned fileCount() const { return static_cast<unsigned>(Files.size()); }

  /// Locates the file owning `loc`; null if the location is invalid.
  const SourceFile *fileFor(SourceLoc loc) const;

  PresumedLoc decode(SourceLoc loc) const;

  /// Raw text for a line (1-based), without the trailing newline.
  std::string lineText(const SourceFile &f, unsigned line) const;

  /// The text covered by `range`, which must not straddle two files.
  std::string textFor(SourceRange range) const;

  SourceLoc locForFileOffset(unsigned fileID, uint32_t offset) const {
    return SourceLoc(Files[fileID].StartOffset + offset);
  }

private:
  std::vector<SourceFile> Files;
  uint32_t NextOffset = 1; // 0 stays free so a default SourceLoc looks distinct.

  void computeLineStarts(SourceFile &f);
};

} // namespace rune

#endif
