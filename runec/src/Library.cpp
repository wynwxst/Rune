#include "rune/Library.h"

#include <cstring>
#include <fstream>
#include <sstream>

namespace rune {

namespace {

constexpr char kMagic[8] = {'R', 'U', 'N', 'E', 'L', 'I', 'B', '\1'};
constexpr uint32_t kVersion = 3;

void putU32(std::ostream &os, uint32_t v) {
  char b[4] = {static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF),
               static_cast<char>((v >> 16) & 0xFF),
               static_cast<char>((v >> 24) & 0xFF)};
  os.write(b, 4);
}

void putU64(std::ostream &os, uint64_t v) {
  putU32(os, static_cast<uint32_t>(v & 0xFFFFFFFFu));
  putU32(os, static_cast<uint32_t>(v >> 32));
}

bool getU32(std::istream &is, uint32_t &out) {
  unsigned char b[4];
  is.read(reinterpret_cast<char *>(b), 4);
  if (is.gcount() != 4)
    return false;
  out = static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
        (static_cast<uint32_t>(b[2]) << 16) | (static_cast<uint32_t>(b[3]) << 24);
  return true;
}

bool getU64(std::istream &is, uint64_t &out) {
  uint32_t lo, hi;
  if (!getU32(is, lo) || !getU32(is, hi))
    return false;
  out = static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
  return true;
}

bool getBytes(std::istream &is, uint64_t n, std::string &out) {
  out.resize(static_cast<size_t>(n));
  if (n == 0)
    return true;
  is.read(out.data(), static_cast<std::streamsize>(n));
  return static_cast<uint64_t>(is.gcount()) == n;
}

} // namespace

std::string publicInterfaceOf(const std::string &source) {
  // The interface is the module's source, kept whole. Generic functions and
  // inline-able bodies have to survive for monomorphisation, and Sema already
  // refuses to resolve anything that is not `pub` across a module boundary, so
  // keeping private declarations here costs nothing but bytes.
  return source;
}

bool writeLibrary(const std::string &path, const std::string &objectPath,
                  const std::string &moduleName,
                  const std::vector<std::pair<std::string, std::string>> &units,
                  MemoryMode memory, const CompilerOptions &opts,
                  DiagnosticEngine &diags) {
  std::ifstream obj(objectPath, std::ios::binary);
  if (!obj) {
    diags.fatal("cannot read the object file '{}'", objectPath);
    return false;
  }
  std::ostringstream objBuf;
  objBuf << obj.rdbuf();
  std::string objectCode = objBuf.str();

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    diags.fatal("cannot write the library '{}'", path);
    return false;
  }
  out.write(kMagic, sizeof(kMagic));
  putU32(out, kVersion);
  putU32(out, memory == MemoryMode::Zombie ? 1u : 0u);
  putU32(out, static_cast<uint32_t>(moduleName.size()));
  out.write(moduleName.data(), static_cast<std::streamsize>(moduleName.size()));
  // The conditions this library was built under, so an importer reading its
  // interface answers them the same way.
  putU32(out, static_cast<uint32_t>(opts.ConfigFlags.size()));
  for (const std::string &flag : opts.ConfigFlags) {
    putU32(out, static_cast<uint32_t>(flag.size()));
    out.write(flag.data(), static_cast<std::streamsize>(flag.size()));
  }
  putU32(out, static_cast<uint32_t>(opts.ConfigValues.size()));
  for (const auto &kv : opts.ConfigValues) {
    putU32(out, static_cast<uint32_t>(kv.first.size()));
    out.write(kv.first.data(), static_cast<std::streamsize>(kv.first.size()));
    putU32(out, static_cast<uint32_t>(kv.second.size()));
    out.write(kv.second.data(), static_cast<std::streamsize>(kv.second.size()));
  }
  putU32(out, static_cast<uint32_t>(units.size()));
  for (const auto &unit : units) {
    putU32(out, static_cast<uint32_t>(unit.first.size()));
    out.write(unit.first.data(),
              static_cast<std::streamsize>(unit.first.size()));
    std::string iface = publicInterfaceOf(unit.second);
    putU32(out, static_cast<uint32_t>(iface.size()));
    out.write(iface.data(), static_cast<std::streamsize>(iface.size()));
  }
  putU64(out, objectCode.size());
  out.write(objectCode.data(), static_cast<std::streamsize>(objectCode.size()));
  return out.good();
}

bool readLibrary(const std::string &path, LibraryContents &out,
                 DiagnosticEngine &diags) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    diags.fatal("cannot open the library '{}'", path);
    return false;
  }
  char magic[8];
  in.read(magic, sizeof(magic));
  if (in.gcount() != sizeof(magic) || std::memcmp(magic, kMagic, 8) != 0) {
    diags.fatal("'{}' is not a Rune library", path)
        .note("a .rul file begins with the marker RUNELIB");
    return false;
  }
  uint32_t version = 0;
  if (!getU32(in, version) || version != kVersion) {
    diags.fatal("'{}' was built by a different compiler version", path)
        .note("rebuild the dependency with this toolchain");
    return false;
  }
  uint32_t memory = 0;
  if (!getU32(in, memory) || memory > 1) {
    diags.fatal("'{}' is truncated", path);
    return false;
  }
  out.Memory = memory == 1 ? MemoryMode::Zombie : MemoryMode::Arc;
  uint32_t nameLen = 0;
  if (!getU32(in, nameLen) || !getBytes(in, nameLen, out.ModuleName)) {
    diags.fatal("'{}' is truncated", path);
    return false;
  }
  uint32_t flagCount = 0;
  if (!getU32(in, flagCount)) {
    diags.fatal("'{}' is truncated", path);
    return false;
  }
  out.ConfigFlags.clear();
  for (uint32_t i = 0; i < flagCount; ++i) {
    uint32_t len = 0;
    std::string flag;
    if (!getU32(in, len) || !getBytes(in, len, flag)) {
      diags.fatal("'{}' is truncated", path);
      return false;
    }
    out.ConfigFlags.push_back(std::move(flag));
  }
  uint32_t valueCount = 0;
  if (!getU32(in, valueCount)) {
    diags.fatal("'{}' is truncated", path);
    return false;
  }
  out.ConfigValues.clear();
  for (uint32_t i = 0; i < valueCount; ++i) {
    uint32_t keyLen = 0, valueLen = 0;
    std::string key, value;
    if (!getU32(in, keyLen) || !getBytes(in, keyLen, key) ||
        !getU32(in, valueLen) || !getBytes(in, valueLen, value)) {
      diags.fatal("'{}' is truncated", path);
      return false;
    }
    out.ConfigValues.push_back({std::move(key), std::move(value)});
  }
  uint32_t units = 0;
  if (!getU32(in, units)) {
    diags.fatal("'{}' is truncated", path);
    return false;
  }
  out.Interfaces.clear();
  for (uint32_t i = 0; i < units; ++i) {
    uint32_t nameBytes = 0, len = 0;
    std::string unitName, unit;
    if (!getU32(in, nameBytes) || !getBytes(in, nameBytes, unitName) ||
        !getU32(in, len) || !getBytes(in, len, unit)) {
      diags.fatal("'{}' is truncated", path);
      return false;
    }
    out.Interfaces.emplace_back(std::move(unitName), std::move(unit));
  }
  uint64_t objLen = 0;
  if (!getU64(in, objLen) || !getBytes(in, objLen, out.ObjectCode)) {
    diags.fatal("'{}' is truncated", path);
    return false;
  }
  return true;
}

bool extractLibraryObject(const std::string &path, const std::string &objectPath,
                          DiagnosticEngine &diags) {
  LibraryContents lib;
  if (!readLibrary(path, lib, diags))
    return false;
  std::ofstream out(objectPath, std::ios::binary | std::ios::trunc);
  if (!out) {
    diags.fatal("cannot write '{}'", objectPath);
    return false;
  }
  out.write(lib.ObjectCode.data(),
            static_cast<std::streamsize>(lib.ObjectCode.size()));
  return out.good();
}

} // namespace rune
