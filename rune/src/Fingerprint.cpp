#include "Fingerprint.h"

#include <algorithm>
#include <fstream>
#include <mutex>

namespace rune::pm {

namespace {

constexpr uint64_t kPrime = 0x100000001b3ull;

/// A name a filesystem will take, derived from a path. Two different outputs
/// must not collapse onto one stamp, so the full path is folded into the
/// suffix rather than only its last component.
std::string stampName(const fs::path &output) {
  std::string base = output.filename().string();
  for (char &ch : base)
    if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '.' &&
        ch != '-' && ch != '_')
      ch = '_';
  Fingerprint whole;
  whole.add(output.lexically_normal().string());
  return base + "." + whole.hex();
}

} // namespace

void Fingerprint::add(std::string_view text) {
  for (unsigned char ch : text) {
    Hash ^= ch;
    Hash *= kPrime;
  }
  // A separator, so `add("ab"); add("c")` and `add("a"); add("bc")` differ.
  Hash ^= 0xffu;
  Hash *= kPrime;
}

void Fingerprint::addFile(const fs::path &path) {
  add(path.string());
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    add("<missing>");
    return;
  }
  char buffer[64 * 1024];
  while (in.read(buffer, sizeof buffer) || in.gcount())
    add(std::string_view(buffer, static_cast<size_t>(in.gcount())));
}

void Fingerprint::addStamp(const fs::path &path) {
  add(path.string());
  std::error_code ec;
  auto size = fs::file_size(path, ec);
  if (ec) {
    add("<missing>");
    return;
  }
  add(std::to_string(size));
  auto when = fs::last_write_time(path, ec);
  if (!ec)
    add(std::to_string(
        static_cast<long long>(when.time_since_epoch().count())));
}

std::string Fingerprint::hex() const {
  static const char *digits = "0123456789abcdef";
  std::string out(16, '0');
  uint64_t v = Hash;
  for (int i = 15; i >= 0; --i) {
    out[static_cast<size_t>(i)] = digits[v & 0xF];
    v >>= 4;
  }
  return out;
}

fs::path FingerprintStore::stampFor(const fs::path &output) const {
  return Dir / stampName(output);
}

bool FingerprintStore::isFresh(const fs::path &output,
                               const Fingerprint &fp) const {
  std::error_code ec;
  if (!fs::exists(output, ec))
    return false;
  std::ifstream in(stampFor(output));
  if (!in)
    return false;
  std::string recorded;
  in >> recorded;
  return recorded == fp.hex();
}

void FingerprintStore::record(const fs::path &output,
                              const Fingerprint &fp) const {
  std::error_code ec;
  fs::create_directories(Dir, ec);
  std::ofstream out(stampFor(output), std::ios::trunc);
  if (out)
    out << fp.hex() << "\n";
}

const Fingerprint &toolchainFingerprint(const std::string &compiler,
                                        const std::string &stdlibDir) {
  static std::mutex mutex;
  static bool computed = false;
  static Fingerprint fp;
  std::lock_guard<std::mutex> lock(mutex);
  if (computed)
    return fp;
  computed = true;

  fp.add("toolchain");
  fp.addStamp(compiler);

  // Every file, in a settled order: a directory walk hands them over in
  // whatever order the filesystem keeps, and the digest must not depend on
  // that.
  std::error_code ec;
  std::vector<fs::path> files;
  for (auto it = fs::recursive_directory_iterator(stdlibDir, ec);
       !ec && it != fs::recursive_directory_iterator(); ++it)
    if (it->is_regular_file() && it->path().extension() == ".rune")
      files.push_back(it->path());
  std::sort(files.begin(), files.end());
  for (const fs::path &f : files)
    fp.addFile(f);
  return fp;
}

} // namespace rune::pm
