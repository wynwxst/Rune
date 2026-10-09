#include "rune/Source.h"

#include <cstdio>

#include <fstream>
#include <sstream>

namespace rune {

void SourceManager::computeLineStarts(SourceFile &f) {
  f.LineStarts.clear();
  f.LineStarts.push_back(0);
  for (uint32_t i = 0; i < f.Buffer.size(); ++i)
    if (f.Buffer[i] == '\n')
      f.LineStarts.push_back(i + 1);
}

static std::string basenameOf(const std::string &path) {
  auto slash = path.find_last_of("/\\");
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::optional<unsigned> SourceManager::loadFile(const std::string &path) {
  // In one read, sized up front: a stream copying the file through its
  // buffer a character at a time was a noticeable part of reading the
  // standard library on every compile.
  std::FILE *f = std::fopen(path.c_str(), "rb");
  if (!f)
    return std::nullopt;
  std::string text;
  if (std::fseek(f, 0, SEEK_END) == 0) {
    long size = std::ftell(f);
    if (size > 0)
      text.resize(static_cast<size_t>(size));
    std::rewind(f);
  }
  size_t got = text.empty() ? 0 : std::fread(text.data(), 1, text.size(), f);
  text.resize(got);
  // Whatever is left — a file that grew, or one that cannot seek — is read
  // to the end.
  char chunk[16384];
  for (size_t n; (n = std::fread(chunk, 1, sizeof chunk, f)) > 0;)
    text.append(chunk, n);
  const bool failed = std::ferror(f) != 0;
  std::fclose(f);
  if (failed)
    return std::nullopt;
  unsigned id = addBuffer(path, std::move(text));
  Files[id].Name = basenameOf(path);
  return id;
}

unsigned SourceManager::addBuffer(std::string name, std::string contents) {
  SourceFile f;
  f.ID = static_cast<unsigned>(Files.size());
  f.Path = name;
  f.Name = basenameOf(name);
  f.Buffer = std::move(contents);
  f.StartOffset = NextOffset;
  // +1 so that the one-past-the-end location of a file never collides with the
  // first location of the next one.
  NextOffset += static_cast<uint32_t>(f.Buffer.size()) + 1;
  computeLineStarts(f);
  Files.push_back(std::move(f));
  return Files.back().ID;
}

const SourceFile *SourceManager::fileFor(SourceLoc loc) const {
  if (!loc.isValid())
    return nullptr;
  uint32_t off = loc.raw();
  // Files are appended with monotonically increasing StartOffsets, so a binary
  // search finds the owner in log time even for large multi-file builds.
  unsigned lo = 0, hi = static_cast<unsigned>(Files.size());
  while (lo < hi) {
    unsigned mid = (lo + hi) / 2;
    if (Files[mid].StartOffset <= off)
      lo = mid + 1;
    else
      hi = mid;
  }
  if (lo == 0)
    return nullptr;
  const SourceFile &f = Files[lo - 1];
  if (off > f.StartOffset + f.Buffer.size())
    return nullptr;
  return &f;
}

PresumedLoc SourceManager::decode(SourceLoc loc) const {
  PresumedLoc pl;
  const SourceFile *f = fileFor(loc);
  if (!f)
    return pl;
  uint32_t rel = loc.raw() - f->StartOffset;
  unsigned lo = 0, hi = static_cast<unsigned>(f->LineStarts.size());
  while (lo < hi) {
    unsigned mid = (lo + hi) / 2;
    if (f->LineStarts[mid] <= rel)
      lo = mid + 1;
    else
      hi = mid;
  }
  pl.File = f;
  pl.Line = lo; // 1-based: LineStarts[0] == 0 corresponds to line 1.
  pl.Column = rel - f->LineStarts[lo - 1];
  return pl;
}

std::string SourceManager::lineText(const SourceFile &f, unsigned line) const {
  if (line == 0 || line > f.LineStarts.size())
    return {};
  uint32_t start = f.LineStarts[line - 1];
  uint32_t end = line < f.LineStarts.size() ? f.LineStarts[line]
                                            : static_cast<uint32_t>(f.Buffer.size());
  while (end > start && (f.Buffer[end - 1] == '\n' || f.Buffer[end - 1] == '\r'))
    --end;
  return f.Buffer.substr(start, end - start);
}

std::string SourceManager::textFor(SourceRange range) const {
  const SourceFile *f = fileFor(range.begin());
  if (!f)
    return {};
  uint32_t start = range.begin().raw() - f->StartOffset;
  uint32_t len = range.length();
  if (start > f->Buffer.size())
    return {};
  if (start + len > f->Buffer.size())
    len = static_cast<uint32_t>(f->Buffer.size()) - start;
  return f->Buffer.substr(start, len);
}

} // namespace rune
