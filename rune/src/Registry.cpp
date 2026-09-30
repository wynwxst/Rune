//===- Registry.cpp - Packages from somewhere else ---------------*- C++ -*-===//
//
// See Registry.h for the shape of it. This file is in the order things
// happen: versions, then bytes (checksums, archives, HTTP), then the index,
// then what is installed, then resolution, then the commands over all of it.
//
//===----------------------------------------------------------------------===//

#include "Registry.h"

#include "Console.h"
#include "Jobs.h"
#include "Manifest.h"
#include "Toml.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <iomanip>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET rune_socket;
#define RUNE_BAD_SOCKET INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
typedef int rune_socket;
#define RUNE_BAD_SOCKET (-1)
#endif

namespace fs = std::filesystem;

namespace rune::pm {

namespace {

//===----------------------------------------------------------------------===//
// Small helpers
//===----------------------------------------------------------------------===//

std::string trim(const std::string &s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

std::vector<std::string> splitOn(const std::string &s, char sep) {
  std::vector<std::string> out;
  std::string cur;
  for (char ch : s) {
    if (ch == sep) { out.push_back(cur); cur.clear(); }
    else cur += ch;
  }
  out.push_back(cur);
  return out;
}

bool readFile(const fs::path &p, std::string &out) {
  std::ifstream in(p, std::ios::binary);
  if (!in)
    return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}

bool writeFile(const fs::path &p, const std::string &text) {
  std::error_code ec;
  fs::create_directories(p.parent_path(), ec);
  std::ofstream out(p, std::ios::binary);
  if (!out)
    return false;
  out << text;
  return static_cast<bool>(out);
}

/// Now, as `2026-09-11T10:20:30Z`.
std::string nowIso() {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}

/// A string as TOML writes it.
std::string tomlString(const std::string &s) {
  std::string out = "\"";
  for (char ch : s) {
    switch (ch) {
    case '"': out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n"; break;
    case '\t': out += "\\t"; break;
    case '\r': out += "\\r"; break;
    default: out += ch;
    }
  }
  return out + "\"";
}

std::string tomlList(const std::vector<std::string> &items) {
  std::string out = "[";
  for (size_t i = 0; i < items.size(); ++i) {
    if (i) out += ", ";
    out += tomlString(items[i]);
  }
  return out + "]";
}

std::vector<std::string> stringList(const TomlValue *v) {
  std::vector<std::string> out;
  if (!v || !v->isArray())
    return out;
  for (const TomlValue &e : v->Arr)
    if (e.isString())
      out.push_back(e.Str);
  return out;
}

std::string humanSize(uint64_t bytes) {
  char buf[32];
  if (bytes < 1024) { std::snprintf(buf, sizeof buf, "%llu B", (unsigned long long)bytes); return buf; }
  if (bytes < 1024 * 1024) { std::snprintf(buf, sizeof buf, "%.1f KB", bytes / 1024.0); return buf; }
  std::snprintf(buf, sizeof buf, "%.1f MB", bytes / (1024.0 * 1024.0));
  return buf;
}

/// The project's identity in a reference list: its root, absolute and
/// normalised, so the same project reached by two spellings counts once.
std::string projectKey(const fs::path &root) {
  std::error_code ec;
  fs::path abs = fs::absolute(root, ec).lexically_normal();
  std::string s = abs.string();
  while (s.size() > 1 && (s.back() == '/' || s.back() == '\\'))
    s.pop_back();
  return s;
}

} // namespace

//===----------------------------------------------------------------------===//
// Versions
//===----------------------------------------------------------------------===//

bool Version::parse(const std::string &text, Version &out) {
  std::string t = trim(text);
  if (!t.empty() && t[0] == 'v')
    t = t.substr(1);
  std::string pre;
  size_t dash = t.find('-');
  if (dash != std::string::npos) {
    pre = t.substr(dash + 1);
    t = t.substr(0, dash);
  }
  auto parts = splitOn(t, '.');
  if (parts.empty() || parts.size() > 3)
    return false;
  int64_t nums[3] = {0, 0, 0};
  for (size_t i = 0; i < parts.size(); ++i) {
    if (parts[i].empty())
      return false;
    for (char ch : parts[i])
      if (!std::isdigit(static_cast<unsigned char>(ch)))
        return false;
    nums[i] = std::atoll(parts[i].c_str());
  }
  out.Major = nums[0];
  out.Minor = nums[1];
  out.Patch = nums[2];
  out.Pre = pre;
  return true;
}

std::string Version::str() const {
  std::string s = std::to_string(Major) + "." + std::to_string(Minor) + "." +
                  std::to_string(Patch);
  if (!Pre.empty())
    s += "-" + Pre;
  return s;
}

int Version::compare(const Version &o) const {
  if (Major != o.Major) return Major < o.Major ? -1 : 1;
  if (Minor != o.Minor) return Minor < o.Minor ? -1 : 1;
  if (Patch != o.Patch) return Patch < o.Patch ? -1 : 1;
  // A pre-release comes before the release it precedes; two pre-releases
  // order by their tags.
  if (Pre.empty() != o.Pre.empty())
    return Pre.empty() ? 1 : -1;
  if (Pre != o.Pre)
    return Pre < o.Pre ? -1 : 1;
  return 0;
}

bool Requirement::parse(const std::string &text, Requirement &out) {
  out.Comparators.clear();
  out.Source = trim(text);
  std::string t = out.Source;
  if (t.empty() || t == "*") {
    out.Comparators.push_back(Comparator{});
    return true;
  }
  for (std::string piece : splitOn(t, ',')) {
    piece = trim(piece);
    if (piece.empty())
      return false;
    Comparator c;
    if (piece.rfind(">=", 0) == 0) { c.O = Comparator::Op::Ge; piece = piece.substr(2); }
    else if (piece.rfind("<=", 0) == 0) { c.O = Comparator::Op::Le; piece = piece.substr(2); }
    else if (piece[0] == '>') { c.O = Comparator::Op::Gt; piece = piece.substr(1); }
    else if (piece[0] == '<') { c.O = Comparator::Op::Lt; piece = piece.substr(1); }
    else if (piece[0] == '=') { c.O = Comparator::Op::Eq; piece = piece.substr(1); }
    else if (piece[0] == '^') { c.O = Comparator::Op::Caret; piece = piece.substr(1); }
    else if (piece[0] == '~') { c.O = Comparator::Op::Tilde; piece = piece.substr(1); }
    else if (piece == "*") { c.O = Comparator::Op::Any; out.Comparators.push_back(c); continue; }
    else c.O = Comparator::Op::Caret;
    piece = trim(piece);
    std::string bare = piece;
    size_t dash = bare.find('-');
    if (dash != std::string::npos)
      bare = bare.substr(0, dash);
    c.Parts = static_cast<int>(splitOn(bare, '.').size());
    if (!Version::parse(piece, c.V))
      return false;
    out.Comparators.push_back(c);
  }
  return true;
}

bool Requirement::matches(const Version &v) const {
  for (const Comparator &c : Comparators) {
    const Version &r = c.V;
    // A pre-release only ever satisfies a requirement that names one.
    if (!v.Pre.empty() && r.Pre.empty() && c.O != Comparator::Op::Any)
      return false;
    switch (c.O) {
    case Comparator::Op::Any:
      break;
    case Comparator::Op::Ge: if (v.compare(r) < 0) return false; break;
    case Comparator::Op::Gt: if (v.compare(r) <= 0) return false; break;
    case Comparator::Op::Le: if (v.compare(r) > 0) return false; break;
    case Comparator::Op::Lt: if (v.compare(r) >= 0) return false; break;
    case Comparator::Op::Eq:
      if (v.Major != r.Major) return false;
      if (c.Parts >= 2 && v.Minor != r.Minor) return false;
      if (c.Parts >= 3 && (v.Patch != r.Patch || v.Pre != r.Pre)) return false;
      break;
    case Comparator::Op::Caret: {
      if (v.compare(r) < 0) return false;
      if (r.Major > 0) { if (v.Major != r.Major) return false; }
      else if (c.Parts >= 2 && r.Minor > 0) {
        if (v.Major != 0 || v.Minor != r.Minor) return false;
      } else if (c.Parts >= 3) {
        if (v.Major != 0 || v.Minor != 0 || v.Patch != r.Patch) return false;
      } else if (c.Parts == 2) {
        if (v.Major != 0 || v.Minor != 0) return false;
      } else {
        if (v.Major != 0) return false;
      }
      break;
    }
    case Comparator::Op::Tilde:
      if (v.compare(r) < 0) return false;
      if (v.Major != r.Major) return false;
      if (c.Parts >= 2 && v.Minor != r.Minor) return false;
      break;
    }
  }
  return true;
}

//===----------------------------------------------------------------------===//
// SHA-256
//===----------------------------------------------------------------------===//

namespace {

constexpr uint32_t kSha256Round[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }

} // namespace

std::string sha256Hex(const std::string &bytes) {
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  std::string data = bytes;
  uint64_t bits = static_cast<uint64_t>(bytes.size()) * 8;
  data += static_cast<char>(0x80);
  while (data.size() % 64 != 56)
    data += '\0';
  for (int i = 7; i >= 0; --i)
    data += static_cast<char>((bits >> (i * 8)) & 0xff);
  for (size_t off = 0; off < data.size(); off += 64) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
      w[i] = (static_cast<uint32_t>(static_cast<unsigned char>(data[off + i * 4])) << 24) |
             (static_cast<uint32_t>(static_cast<unsigned char>(data[off + i * 4 + 1])) << 16) |
             (static_cast<uint32_t>(static_cast<unsigned char>(data[off + i * 4 + 2])) << 8) |
             static_cast<uint32_t>(static_cast<unsigned char>(data[off + i * 4 + 3]));
    for (int i = 16; i < 64; ++i) {
      uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
      uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      uint32_t ch = (e & f) ^ (~e & g);
      uint32_t t1 = hh + s1 + ch + kSha256Round[i] + w[i];
      uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      uint32_t t2 = s0 + maj;
      hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
  }
  static const char *digits = "0123456789abcdef";
  std::string out;
  for (uint32_t v : h)
    for (int i = 7; i >= 0; --i)
      out += digits[(v >> (i * 4)) & 0xf];
  return out;
}

//===----------------------------------------------------------------------===//
// Archives: ustar, the plain kind
//
// No compression: Rune sources are small, and an archive any `tar` can open
// is worth more than the bytes. Only regular files are stored; directories
// are implied by the paths in them.
//===----------------------------------------------------------------------===//

namespace {

void tarNumber(char *field, size_t width, uint64_t value) {
  // Octal, NUL-terminated, zero-padded to the field.
  std::string s;
  for (size_t i = 0; i < width - 1; ++i) {
    s.insert(s.begin(), static_cast<char>('0' + (value & 7)));
    value >>= 3;
  }
  std::memcpy(field, s.c_str(), width - 1);
  field[width - 1] = '\0';
}

void tarAppend(std::string &tar, const std::string &name,
               const std::string &content) {
  char header[512];
  std::memset(header, 0, sizeof header);
  // ustar splits a long name across `prefix` (155) and `name` (100).
  std::string base = name, prefix;
  if (base.size() > 100) {
    size_t cut = base.rfind('/', 155);
    if (cut != std::string::npos && base.size() - cut - 1 <= 100) {
      prefix = base.substr(0, cut);
      base = base.substr(cut + 1);
    }
  }
  std::memcpy(header, base.c_str(), std::min<size_t>(base.size(), 100));
  tarNumber(header + 100, 8, 0644);
  tarNumber(header + 108, 8, 0);
  tarNumber(header + 116, 8, 0);
  tarNumber(header + 124, 12, content.size());
  tarNumber(header + 136, 12, 0);
  std::memset(header + 148, ' ', 8);
  header[156] = '0';
  std::memcpy(header + 257, "ustar", 5);
  header[262] = '\0';
  std::memcpy(header + 263, "00", 2);
  std::memcpy(header + 345, prefix.c_str(), std::min<size_t>(prefix.size(), 155));
  unsigned sum = 0;
  for (unsigned char ch : header)
    sum += ch;
  tarNumber(header + 148, 7, sum);
  header[155] = ' ';
  tar.append(header, 512);
  tar += content;
  size_t pad = (512 - content.size() % 512) % 512;
  tar.append(pad, '\0');
}

bool isPackedFile(const fs::path &rel) {
  std::string first = rel.begin() != rel.end() ? rel.begin()->string() : "";
  std::string name = rel.filename().string();
  if (rel == "Rune.toml")
    return true;
  if (first == "src" || first == "docs" || first == "tests")
    return true;
  if (rel.parent_path().empty()) {
    std::string upper = name;
    for (char &ch : upper) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    if (upper.rfind("README", 0) == 0 || upper.rfind("LICENSE", 0) == 0 ||
        upper.rfind("LICENCE", 0) == 0 || upper.rfind("CHANGELOG", 0) == 0)
      return true;
  }
  return false;
}

} // namespace

bool packPackage(const fs::path &project, std::string &tar, std::string &error) {
  Manifest m;
  if (!loadManifest(project.string(), m, error, /*requireSources=*/false))
    return false;
  std::vector<std::string> files;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(project, ec), end; it != end;
       it.increment(ec)) {
    if (ec) { error = ec.message(); return false; }
    fs::path rel = fs::relative(it->path(), project, ec);
    std::string first = rel.begin() != rel.end() ? rel.begin()->string() : "";
    if (first == "target" || first == ".git" || first == ".rune") {
      it.disable_recursion_pending();
      continue;
    }
    if (!it->is_regular_file())
      continue;
    if (isPackedFile(rel))
      files.push_back(rel.generic_string());
  }
  // The C half travels with the package, exactly as a build needs it.
  for (const std::string &c : m.CSources) {
    fs::path rel = fs::relative(c, project, ec);
    if (!ec && !rel.empty() && rel.generic_string().rfind("..", 0) != 0)
      files.push_back(rel.generic_string());
  }
  std::sort(files.begin(), files.end());
  files.erase(std::unique(files.begin(), files.end()), files.end());
  tar.clear();
  for (const std::string &f : files) {
    std::string content;
    if (!readFile(project / f, content)) {
      error = "cannot read " + (project / f).string();
      return false;
    }
    tarAppend(tar, f, content);
  }
  tar.append(1024, '\0');
  return true;
}

bool unpackTar(const std::string &tar, const fs::path &into, std::string &error) {
  size_t off = 0;
  std::error_code ec;
  fs::create_directories(into, ec);
  while (off + 512 <= tar.size()) {
    const char *h = tar.data() + off;
    bool blank = true;
    for (int i = 0; i < 512; ++i)
      if (h[i]) { blank = false; break; }
    if (blank)
      break;
    std::string name(h, strnlen(h, 100));
    std::string prefix(h + 345, strnlen(h + 345, 155));
    if (!prefix.empty())
      name = prefix + "/" + name;
    uint64_t size = std::strtoull(std::string(h + 124, 12).c_str(), nullptr, 8);
    char type = h[156];
    off += 512;
    if (off + size > tar.size()) {
      error = "archive is truncated";
      return false;
    }
    // Nothing may reach outside the directory it is being unpacked into.
    fs::path rel(name);
    bool escapes = rel.is_absolute();
    for (const auto &part : rel)
      if (part == "..")
        escapes = true;
    if (escapes) {
      error = "archive names a path outside itself: " + name;
      return false;
    }
    if (type == '0' || type == '\0') {
      fs::path dest = into / rel;
      fs::create_directories(dest.parent_path(), ec);
      if (!writeFile(dest, tar.substr(off, size))) {
        error = "cannot write " + dest.string();
        return false;
      }
    }
    off += size + (512 - size % 512) % 512;
  }
  return true;
}

//===----------------------------------------------------------------------===//
// HTTP, the small part of it a registry needs
//
// A client that can GET, and a server that can serve a directory. Both speak
// HTTP/1.1 with `Connection: close`, which is the whole protocol when every
// request is one file. `https://` is left to `curl`, which knows about
// certificates and this code does not.
//===----------------------------------------------------------------------===//

namespace {

struct SocketsInit {
  SocketsInit() {
#ifdef _WIN32
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
#else
    // A peer that hangs up mid-write would otherwise kill the process.
    signal(SIGPIPE, SIG_IGN);
#endif
  }
};

void ensureSockets() { static SocketsInit once; }

void closeSocket(rune_socket s) {
#ifdef _WIN32
  closesocket(s);
#else
  close(s);
#endif
}

bool sendAll(rune_socket s, const std::string &data) {
  size_t sent = 0;
  while (sent < data.size()) {
#ifdef _WIN32
    int n = send(s, data.data() + sent, static_cast<int>(data.size() - sent), 0);
#else
    ssize_t n = send(s, data.data() + sent, data.size() - sent, 0);
#endif
    if (n <= 0)
      return false;
    sent += static_cast<size_t>(n);
  }
  return true;
}

/// `http://host[:port]/path` taken apart.
struct Url {
  std::string Scheme, Host, Path;
  int Port = 80;
};

bool parseUrl(const std::string &url, Url &out) {
  size_t colon = url.find("://");
  if (colon == std::string::npos)
    return false;
  out.Scheme = url.substr(0, colon);
  std::string rest = url.substr(colon + 3);
  size_t slash = rest.find('/');
  std::string hostPort = slash == std::string::npos ? rest : rest.substr(0, slash);
  out.Path = slash == std::string::npos ? "/" : rest.substr(slash);
  size_t p = hostPort.rfind(':');
  if (p != std::string::npos) {
    out.Host = hostPort.substr(0, p);
    out.Port = std::atoi(hostPort.substr(p + 1).c_str());
  } else {
    out.Host = hostPort;
    out.Port = out.Scheme == "https" ? 443 : 80;
  }
  return !out.Host.empty();
}

rune_socket connectTo(const std::string &host, int port, std::string &error) {
  ensureSockets();
  struct addrinfo hints;
  std::memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo *res = nullptr;
  std::string portText = std::to_string(port);
  if (getaddrinfo(host.c_str(), portText.c_str(), &hints, &res) != 0 || !res) {
    error = "cannot resolve '" + host + "'";
    return RUNE_BAD_SOCKET;
  }
  rune_socket s = RUNE_BAD_SOCKET;
  for (struct addrinfo *a = res; a; a = a->ai_next) {
    s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
    if (s == RUNE_BAD_SOCKET)
      continue;
    if (connect(s, a->ai_addr, static_cast<int>(a->ai_addrlen)) == 0)
      break;
    closeSocket(s);
    s = RUNE_BAD_SOCKET;
  }
  freeaddrinfo(res);
  if (s == RUNE_BAD_SOCKET)
    error = "cannot connect to " + host + ":" + portText;
  return s;
}

std::string lowerAscii(std::string s) {
  for (char &ch : s)
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return s;
}

/// One GET over plain HTTP. Follows a redirect a few times.
bool httpGet(const std::string &url, std::string &body, std::string &error,
             int redirects = 0) {
  Url u;
  if (!parseUrl(url, u)) {
    error = "malformed URL '" + url + "'";
    return false;
  }
  rune_socket s = connectTo(u.Host, u.Port, error);
  if (s == RUNE_BAD_SOCKET)
    return false;
  std::string request = "GET " + u.Path + " HTTP/1.1\r\nHost: " + u.Host +
                        "\r\nUser-Agent: rune\r\nAccept: */*\r\n"
                        "Connection: close\r\n\r\n";
  if (!sendAll(s, request)) {
    closeSocket(s);
    error = "cannot send to " + u.Host;
    return false;
  }
  std::string raw;
  char buf[65536];
  for (;;) {
#ifdef _WIN32
    int n = recv(s, buf, sizeof buf, 0);
#else
    ssize_t n = recv(s, buf, sizeof buf, 0);
#endif
    if (n <= 0)
      break;
    raw.append(buf, static_cast<size_t>(n));
  }
  closeSocket(s);

  size_t headEnd = raw.find("\r\n\r\n");
  if (headEnd == std::string::npos) {
    error = "no HTTP response from " + u.Host;
    return false;
  }
  std::string head = raw.substr(0, headEnd);
  std::string rest = raw.substr(headEnd + 4);
  std::istringstream lines(head);
  std::string statusLine;
  std::getline(lines, statusLine);
  int status = 0;
  {
    size_t sp = statusLine.find(' ');
    if (sp != std::string::npos)
      status = std::atoi(statusLine.c_str() + sp + 1);
  }
  std::map<std::string, std::string> headers;
  std::string line;
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    size_t c = line.find(':');
    if (c != std::string::npos)
      headers[lowerAscii(line.substr(0, c))] = trim(line.substr(c + 1));
  }
  if (status == 301 || status == 302 || status == 307 || status == 308) {
    if (redirects >= 5 || !headers.count("location")) {
      error = "too many redirects from " + url;
      return false;
    }
    std::string to = headers["location"];
    if (to.rfind("http", 0) != 0)
      to = u.Scheme + "://" + u.Host + ":" + std::to_string(u.Port) +
           (to.empty() || to[0] != '/' ? "/" : "") + to;
    return httpGet(to, body, error, redirects + 1);
  }
  if (status != 200) {
    error = "HTTP " + std::to_string(status) + " for " + url;
    return false;
  }
  if (lowerAscii(headers["transfer-encoding"]).find("chunked") != std::string::npos) {
    body.clear();
    size_t off = 0;
    for (;;) {
      size_t eol = rest.find("\r\n", off);
      if (eol == std::string::npos) break;
      size_t len = std::strtoul(rest.substr(off, eol - off).c_str(), nullptr, 16);
      if (len == 0) break;
      body += rest.substr(eol + 2, len);
      off = eol + 2 + len + 2;
    }
    return true;
  }
  body = rest;
  if (headers.count("content-length")) {
    size_t want = std::strtoul(headers["content-length"].c_str(), nullptr, 10);
    if (body.size() < want) {
      error = "short response from " + u.Host;
      return false;
    }
    body.resize(want);
  }
  return true;
}

} // namespace

bool fetch(const std::string &url, std::string &out, std::string &error) {
  if (url.rfind("file://", 0) == 0)
    return readFile(url.substr(7), out) ? true : (error = "cannot read " + url, false);
  if (url.rfind("http://", 0) == 0)
    return httpGet(url, out, error);
  if (url.rfind("https://", 0) == 0) {
    // curl knows about certificates; this code does not, and should not.
    std::string cmd = "curl -fsSL " + quote(url);
    std::string captured;
    int rc = runCaptured(cmd, captured);
    if (rc != 0) {
      error = "curl failed for " + url + (captured.empty() ? "" : ": " + trim(captured));
      return false;
    }
    out = captured;
    return true;
  }
  // A plain path is a registry on a disk.
  return readFile(url, out) ? true : (error = "cannot read " + url, false);
}

//===----------------------------------------------------------------------===//
// The server
//===----------------------------------------------------------------------===//

namespace {

const char *contentTypeFor(const fs::path &p) {
  std::string ext = lowerAscii(p.extension().string());
  if (ext == ".toml" || ext == ".txt" || ext == ".md") return "text/plain; charset=utf-8";
  if (ext == ".html") return "text/html; charset=utf-8";
  if (ext == ".json") return "application/json";
  if (ext == ".tar") return "application/x-tar";
  return "application/octet-stream";
}

std::string htmlEscape(const std::string &s) {
  std::string out;
  for (char ch : s) {
    switch (ch) {
    case '&': out += "&amp;"; break;
    case '<': out += "&lt;"; break;
    case '>': out += "&gt;"; break;
    case '"': out += "&quot;"; break;
    default: out += ch;
    }
  }
  return out;
}

/// The page at `/`: what this registry holds, for a person with a browser.
std::string registryFrontPage(const fs::path &root) {
  std::string text;
  Index index;
  std::string err;
  if (readFile(root / "index.toml", text))
    index.parse(text, err);
  std::string html = "<!doctype html><meta charset=utf-8><title>" +
                     htmlEscape(index.Name) + "</title>"
                     "<style>body{font:15px/1.5 system-ui,sans-serif;max-width:52rem;"
                     "margin:3rem auto;padding:0 1rem;color:#222}table{border-collapse:"
                     "collapse;width:100%}td,th{text-align:left;padding:.35rem .6rem;"
                     "border-bottom:1px solid #ddd}code{font-size:.92em}</style>"
                     "<h1>" + htmlEscape(index.Name) + "</h1>"
                     "<p>A Rune package registry. Add it with "
                     "<code>rune registry add &lt;this url&gt;</code>, then "
                     "<code>rune search</code>, <code>rune add &lt;name&gt;</code>.</p>"
                     "<table><tr><th>Package</th><th>Latest</th><th>Description</th></tr>";
  std::set<std::string> seen;
  for (const Release &r : index.Releases) {
    if (!seen.insert(r.Name).second)
      continue;
    const Release *latest = index.versionsOf(r.Name).front();
    html += "<tr><td><code>" + htmlEscape(r.Name) + "</code></td><td>" +
            htmlEscape(latest->V.str()) + "</td><td>" +
            htmlEscape(latest->Description) + "</td></tr>";
  }
  html += "</table><p><a href=\"index.toml\">index.toml</a></p>";
  return html;
}

void serveOne(rune_socket client, const fs::path &root, bool verbose) {
  std::string raw;
  char buf[8192];
  // Only the request line and headers matter; read until the blank line.
  while (raw.find("\r\n\r\n") == std::string::npos) {
#ifdef _WIN32
    int n = recv(client, buf, sizeof buf, 0);
#else
    ssize_t n = recv(client, buf, sizeof buf, 0);
#endif
    if (n <= 0)
      break;
    raw.append(buf, static_cast<size_t>(n));
    if (raw.size() > 65536)
      break;
  }
  std::istringstream in(raw);
  std::string method, target, version;
  in >> method >> target >> version;

  auto respond = [&](int code, const char *reason, const std::string &type,
                     const std::string &body, bool headOnly) {
    std::string head = "HTTP/1.1 " + std::to_string(code) + " " + reason +
                       "\r\nContent-Type: " + type +
                       "\r\nContent-Length: " + std::to_string(body.size()) +
                       "\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n";
    sendAll(client, headOnly ? head : head + body);
  };

  bool head = method == "HEAD";
  if (method != "GET" && !head) {
    respond(405, "Method Not Allowed", "text/plain", "only GET\n", false);
    closeSocket(client);
    return;
  }
  size_t q = target.find('?');
  if (q != std::string::npos)
    target = target.substr(0, q);
  // Percent-decoding, for a name with a space in it.
  std::string path;
  for (size_t i = 0; i < target.size(); ++i) {
    if (target[i] == '%' && i + 2 < target.size()) {
      path += static_cast<char>(std::strtol(target.substr(i + 1, 2).c_str(), nullptr, 16));
      i += 2;
    } else {
      path += target[i];
    }
  }
  int code = 200;
  if (path == "/" || path.empty()) {
    respond(200, "OK", "text/html; charset=utf-8", registryFrontPage(root), head);
  } else {
    fs::path rel(path.substr(1));
    bool escapes = rel.is_absolute();
    for (const auto &part : rel)
      if (part == "..")
        escapes = true;
    std::string body;
    std::error_code ec;
    fs::path file = root / rel;
    if (escapes || !fs::is_regular_file(file, ec) || !readFile(file, body)) {
      code = 404;
      respond(404, "Not Found", "text/plain", "no such file\n", head);
    } else {
      respond(200, "OK", contentTypeFor(file), body, head);
    }
  }
  if (verbose || code != 200)
    plain(std::string("  ") + method + " " + path + " -> " + std::to_string(code));
  closeSocket(client);
}

int serveDirectory(const fs::path &root, int port, bool verbose) {
  ensureSockets();
  std::error_code ec;
  if (!fs::exists(root / "index.toml", ec)) {
    failLine("'" + root.string() + "' is not a package registry");
    note("there is no index.toml; run `rune registry init` there first");
    return 1;
  }
  rune_socket server = socket(AF_INET, SOCK_STREAM, 0);
  if (server == RUNE_BAD_SOCKET) {
    failLine("cannot create a socket");
    return 1;
  }
  int yes = 1;
  setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&yes), sizeof yes);
  struct sockaddr_in addr;
  std::memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(static_cast<uint16_t>(port));
  if (bind(server, reinterpret_cast<struct sockaddr *>(&addr), sizeof addr) != 0) {
    failLine("cannot listen on port " + std::to_string(port));
    note("is something else already serving there?");
    closeSocket(server);
    return 1;
  }
  if (listen(server, 64) != 0) {
    failLine("cannot listen on port " + std::to_string(port));
    closeSocket(server);
    return 1;
  }
  okLine("Serving " + fs::absolute(root).lexically_normal().string() +
         " at http://localhost:" + std::to_string(port) + "/");
  note("add it to a client with `rune registry add http://<this host>:" +
       std::to_string(port) + "`; Ctrl-C stops it");
  for (;;) {
    struct sockaddr_storage peer;
#ifdef _WIN32
    int len = sizeof peer;
#else
    socklen_t len = sizeof peer;
#endif
    rune_socket client = accept(server, reinterpret_cast<struct sockaddr *>(&peer), &len);
    if (client == RUNE_BAD_SOCKET)
      continue;
    // One thread per request: a request is one file, read and written once,
    // so there is nothing for a pool to save.
    std::thread(serveOne, client, root, verbose).detach();
  }
  return 0;
}

} // namespace

//===----------------------------------------------------------------------===//
// The index
//===----------------------------------------------------------------------===//

bool Index::parse(const std::string &text, std::string &error) {
  Releases.clear();
  TomlDocument doc = parseToml(text);
  if (!doc.ok()) {
    error = "index.toml line " + std::to_string(doc.ErrorLine) + ": " + doc.Error;
    return false;
  }
  if (const TomlValue *reg = doc.get("registry")) {
    if (const TomlValue *n = reg->find("name")) Name = n->stringOr("");
    if (const TomlValue *u = reg->find("updated")) Updated = u->stringOr("");
  }
  const TomlValue *releases = doc.get("release");
  if (!releases || !releases->isArray())
    return true;
  for (const TomlValue &v : releases->Arr) {
    if (!v.isTable())
      continue;
    Release r;
    if (const TomlValue *x = v.find("name")) r.Name = x->stringOr("");
    std::string version;
    if (const TomlValue *x = v.find("version")) version = x->stringOr("");
    if (r.Name.empty() || !Version::parse(version, r.V))
      continue;
    if (const TomlValue *x = v.find("description")) r.Description = x->stringOr("");
    if (const TomlValue *x = v.find("license")) r.License = x->stringOr("");
    if (const TomlValue *x = v.find("archive")) r.Archive = x->stringOr("");
    if (const TomlValue *x = v.find("sha256")) r.Sha256 = x->stringOr("");
    if (const TomlValue *x = v.find("size")) r.Size = static_cast<uint64_t>(x->intOr(0));
    if (const TomlValue *x = v.find("added")) r.Added = x->stringOr("");
    r.Authors = stringList(v.find("authors"));
    for (const std::string &d : stringList(v.find("dependencies"))) {
      size_t sp = d.find(' ');
      if (sp == std::string::npos)
        r.Dependencies.push_back({trim(d), "*"});
      else
        r.Dependencies.push_back({trim(d.substr(0, sp)), trim(d.substr(sp + 1))});
    }
    Releases.push_back(std::move(r));
  }
  return true;
}

std::string Index::serialise() const {
  std::string out = "# The packages this registry holds. Written by `rune registry "
                    "--addPackage`;\n# every release is one entry, and the "
                    "archive it names is under packages/.\n\n[registry]\nname = " +
                    tomlString(Name) + "\nformat = 1\nupdated = " +
                    tomlString(Updated) + "\n";
  std::vector<const Release *> sorted;
  for (const Release &r : Releases)
    sorted.push_back(&r);
  std::sort(sorted.begin(), sorted.end(), [](const Release *a, const Release *b) {
    if (a->Name != b->Name) return a->Name < b->Name;
    return a->V < b->V;
  });
  for (const Release *r : sorted) {
    out += "\n[[release]]\nname = " + tomlString(r->Name) + "\nversion = " +
           tomlString(r->V.str()) + "\n";
    if (!r->Description.empty()) out += "description = " + tomlString(r->Description) + "\n";
    if (!r->Authors.empty()) out += "authors = " + tomlList(r->Authors) + "\n";
    if (!r->License.empty()) out += "license = " + tomlString(r->License) + "\n";
    out += "archive = " + tomlString(r->Archive) + "\nsha256 = " + tomlString(r->Sha256) +
           "\nsize = " + std::to_string(r->Size) + "\nadded = " + tomlString(r->Added) + "\n";
    std::vector<std::string> deps;
    for (const auto &d : r->Dependencies)
      deps.push_back(d.first + " " + d.second);
    out += "dependencies = " + tomlList(deps) + "\n";
  }
  return out;
}

std::vector<const Release *> Index::versionsOf(const std::string &name,
                                               const std::string &registry) const {
  std::vector<const Release *> out;
  for (const Release &r : Releases)
    if (r.Name == name && (registry.empty() || r.RegistryName == registry))
      out.push_back(&r);
  // Newest first; the same version in two registries keeps index order,
  // which is the order the registries were configured in.
  std::stable_sort(out.begin(), out.end(),
                   [](const Release *a, const Release *b) { return b->V < a->V; });
  return out;
}

const Release *Index::best(const std::string &name, const Requirement &req,
                           const std::string &registry) const {
  for (const Release *r : versionsOf(name, registry))
    if (req.matches(r->V))
      return r;
  return nullptr;
}

std::vector<std::string> Index::registriesWith(const std::string &name) const {
  std::vector<std::string> out;
  for (const Release &r : Releases)
    if (r.Name == name &&
        std::find(out.begin(), out.end(), r.RegistryName) == out.end())
      out.push_back(r.RegistryName);
  return out;
}

//===----------------------------------------------------------------------===//
// The client's registries
//===----------------------------------------------------------------------===//

namespace {

fs::path registriesFile() { return runeHome() / "registries.toml"; }

/// Where a fetched index is kept: one file per registry, named by a hash of
/// its URL so two registries never share one.
fs::path indexCacheFor(const std::string &url) {
  return runeHome() / "cache" / "index" / (sha256Hex(url).substr(0, 24) + ".toml");
}

/// `url` with `rel` appended: a registry's archives are named relative to
/// its root, whatever kind of place that root is.
std::string joinUrl(const std::string &base, const std::string &rel) {
  if (base.empty())
    return rel;
  if (base.back() == '/')
    return base + rel;
  return base + "/" + rel;
}

} // namespace

const RegistrySource *findRegistry(const std::vector<RegistrySource> &list,
                                   const std::string &nameOrUrl) {
  for (const RegistrySource &r : list)
    if (r.Name == nameOrUrl)
      return &r;
  for (const RegistrySource &r : list)
    if (r.Url == nameOrUrl)
      return &r;
  return nullptr;
}

std::string registryNameOf(const std::vector<RegistrySource> &list,
                           const std::string &url) {
  for (const RegistrySource &r : list)
    if (r.Url == url)
      return r.Name;
  return url;
}

bool isRegistryName(const std::string &name) {
  if (name.empty())
    return false;
  for (char c : name)
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-' &&
        c != '.')
      return false;
  return true;
}

void splitQualified(const std::string &spec, std::string &registry,
                    std::string &rest) {
  size_t sep = spec.find("::");
  if (sep == std::string::npos || !isRegistryName(spec.substr(0, sep))) {
    registry.clear();
    rest = spec;
    return;
  }
  registry = spec.substr(0, sep);
  rest = spec.substr(sep + 2);
}

std::vector<RegistrySource> loadRegistries() {
  std::vector<RegistrySource> out;
  std::string text;
  if (!readFile(registriesFile(), text))
    return out;
  TomlDocument doc = parseToml(text);
  if (!doc.ok())
    return out;
  const TomlValue *list = doc.get("registry");
  if (!list || !list->isArray())
    return out;
  for (const TomlValue &v : list->Arr) {
    RegistrySource r;
    if (const TomlValue *x = v.find("name")) r.Name = x->stringOr("");
    if (const TomlValue *x = v.find("url")) r.Url = x->stringOr("");
    if (const TomlValue *x = v.find("added")) r.Added = x->stringOr("");
    if (const TomlValue *x = v.find("declared")) r.Declared = x->stringOr("");
    if (r.Declared.empty())
      r.Declared = r.Name;
    if (!r.Url.empty() && !r.Name.empty())
      out.push_back(r);
  }
  return out;
}

bool saveRegistries(const std::vector<RegistrySource> &list, std::string &error) {
  std::string out = "# Registries `rune search`, `rune add` and friends read.\n"
                    "# Added with `rune registry add <url>`; `rune registry "
                    "list` shows them,\n# `rune registry remove <name>` drops "
                    "one. `name` is what commands and manifests\n# use; "
                    "`declared` is what the registry calls itself.\n";
  for (const RegistrySource &r : list) {
    out += "\n[[registry]]\nname = " + tomlString(r.Name) + "\nurl = " +
           tomlString(r.Url) + "\nadded = " + tomlString(r.Added) + "\n";
    if (!r.Declared.empty() && r.Declared != r.Name)
      out += "declared = " + tomlString(r.Declared) + "\n";
  }
  if (!writeFile(registriesFile(), out)) {
    error = "cannot write " + registriesFile().string();
    return false;
  }
  return true;
}

bool loadMergedIndex(Index &out, bool refresh, std::string &error) {
  std::vector<RegistrySource> sources = loadRegistries();
  if (sources.empty()) {
    error = "no registries are configured";
    return false;
  }
  // Every registry's index at once: a slow one does not hold up the rest.
  std::vector<std::string> texts(sources.size());
  std::vector<std::string> errors(sources.size());
  std::vector<Job> jobs;
  for (size_t i = 0; i < sources.size(); ++i) {
    Job j;
    j.Run = [&, i]() {
      fs::path cached = indexCacheFor(sources[i].Url);
      std::error_code ec;
      if (!refresh && fs::exists(cached, ec) && readFile(cached, texts[i]))
        return true;
      std::string fetched, err;
      if (!fetch(joinUrl(sources[i].Url, "index.toml"), fetched, err)) {
        // Offline is not fatal when there is a copy from last time.
        if (readFile(cached, texts[i])) {
          errors[i] = "using the cached index for " + sources[i].Name + ": " + err;
          return true;
        }
        errors[i] = err;
        return false;
      }
      texts[i] = fetched;
      writeFile(cached, fetched);
      return true;
    };
    jobs.push_back(std::move(j));
  }
  bool ok = runGraph(jobs);
  out.Releases.clear();
  for (size_t i = 0; i < sources.size(); ++i) {
    if (!errors[i].empty() && !texts[i].empty())
      warnLine(errors[i]);
    if (texts[i].empty())
      continue;
    Index one;
    std::string err;
    if (!one.parse(texts[i], err)) {
      warnLine(sources[i].Name + ": " + err);
      continue;
    }
    for (Release &r : one.Releases) {
      r.Registry = sources[i].Url;
      r.RegistryName = sources[i].Name;
      out.Releases.push_back(std::move(r));
    }
  }
  if (!ok) {
    error.clear();
    for (size_t i = 0; i < sources.size(); ++i)
      if (texts[i].empty() && !errors[i].empty())
        error += (error.empty() ? "" : "; ") + sources[i].Name + ": " + errors[i];
    return false;
  }
  return true;
}

//===----------------------------------------------------------------------===//
// What is installed
//===----------------------------------------------------------------------===//

fs::path installDir(const std::string &name, const Version &v) {
  return runeHome() / "registry" / name / v.str();
}

std::vector<std::string> referencesOf(const fs::path &dir) {
  std::vector<std::string> out;
  std::string text;
  if (!readFile(dir / ".rune-refs", text))
    return out;
  for (const std::string &line : splitOn(text, '\n')) {
    std::string t = trim(line);
    if (!t.empty())
      out.push_back(t);
  }
  return out;
}

namespace {

void writeReferences(const fs::path &dir, const std::vector<std::string> &refs) {
  std::string text = "# Projects that depend on this installed version, one per line.\n"
                     "# Kept by `rune add` and `rune remove`; `rune installed` reads it.\n";
  for (const std::string &r : refs)
    text += r + "\n";
  writeFile(dir / ".rune-refs", text);
}

std::mutex gRefsMutex;
bool gRecordReferences = true;

} // namespace

void setRecordReferences(bool on) { gRecordReferences = on; }

void addReference(const fs::path &dir, const std::string &project) {
  if (!gRecordReferences)
    return;
  std::lock_guard<std::mutex> lock(gRefsMutex);
  std::vector<std::string> refs = referencesOf(dir);
  if (std::find(refs.begin(), refs.end(), project) == refs.end()) {
    refs.push_back(project);
    writeReferences(dir, refs);
  }
}

void dropReference(const fs::path &dir, const std::string &project) {
  std::lock_guard<std::mutex> lock(gRefsMutex);
  std::vector<std::string> refs = referencesOf(dir);
  auto it = std::remove(refs.begin(), refs.end(), project);
  if (it != refs.end()) {
    refs.erase(it, refs.end());
    writeReferences(dir, refs);
  }
}

std::vector<Installed> listInstalled() {
  std::vector<Installed> out;
  fs::path root = runeHome() / "registry";
  std::error_code ec;
  if (!fs::is_directory(root, ec))
    return out;
  for (const auto &registry : fs::directory_iterator(root, ec)) {
    if (!registry.is_directory())
      continue;
    for (const auto &ver : fs::directory_iterator(registry.path(), ec)) {
      if (!ver.is_directory())
        continue;
      Installed i;
      i.Name = registry.path().filename().string();
      if (!Version::parse(ver.path().filename().string(), i.V))
        continue;
      i.Dir = ver.path();
      // A project that has gone stops counting: a reference from a
      // directory with no manifest in it is a leftover, not a user.
      std::vector<std::string> live;
      for (const std::string &r : referencesOf(i.Dir))
        if (fs::exists(fs::path(r) / "Rune.toml", ec))
          live.push_back(r);
      if (live.size() != referencesOf(i.Dir).size())
        writeReferences(i.Dir, live);
      i.Refs = live;
      std::string origin;
      if (readFile(i.Dir / ".rune-origin", origin)) {
        TomlDocument doc = parseToml(origin);
        if (doc.ok())
          if (const TomlValue *r = doc.get("registry"))
            i.Registry = r->stringOr("");
      }
      out.push_back(std::move(i));
    }
  }
  std::sort(out.begin(), out.end(), [](const Installed &a, const Installed &b) {
    if (a.Name != b.Name) return a.Name < b.Name;
    return a.V < b.V;
  });
  return out;
}

//===----------------------------------------------------------------------===//
// The lock
//===----------------------------------------------------------------------===//

std::vector<Locked> loadLock(const fs::path &projectRoot) {
  std::vector<Locked> out;
  std::string text;
  if (!readFile(projectRoot / "Rune.lock", text))
    return out;
  TomlDocument doc = parseToml(text);
  if (!doc.ok())
    return out;
  const TomlValue *list = doc.get("package");
  if (!list || !list->isArray())
    return out;
  for (const TomlValue &v : list->Arr) {
    Locked l;
    std::string version;
    if (const TomlValue *x = v.find("name")) l.Name = x->stringOr("");
    if (const TomlValue *x = v.find("version")) version = x->stringOr("");
    if (const TomlValue *x = v.find("registry")) l.Registry = x->stringOr("");
    if (const TomlValue *x = v.find("sha256")) l.Sha256 = x->stringOr("");
    if (!l.Name.empty() && Version::parse(version, l.V))
      out.push_back(l);
  }
  return out;
}

bool saveLock(const fs::path &projectRoot, const std::vector<Locked> &lock,
              std::string &error) {
  std::vector<Locked> sorted = lock;
  std::sort(sorted.begin(), sorted.end(),
            [](const Locked &a, const Locked &b) { return a.Name < b.Name; });
  std::string out = "# What this project's registry dependencies resolved to.\n"
                    "# Written by `rune add`, `rune update` and `rune build`; "
                    "commit it so a build elsewhere gets the same versions.\n";
  for (const Locked &l : sorted)
    out += "\n[[package]]\nname = " + tomlString(l.Name) + "\nversion = " +
           tomlString(l.V.str()) + "\nregistry = " + tomlString(l.Registry) +
           "\nsha256 = " + tomlString(l.Sha256) + "\n";
  if (!writeFile(projectRoot / "Rune.lock", out)) {
    error = "cannot write " + (projectRoot / "Rune.lock").string();
    return false;
  }
  return true;
}

//===----------------------------------------------------------------------===//
// Resolution and installation
//===----------------------------------------------------------------------===//

namespace {

/// Picks one release per package such that every requirement anyone has of
/// it holds, transitively. Iterated to a fixed point: a choice brings its
/// own requirements, which may move another choice.
///
/// A package asked for from a named registry is taken from that registry
/// alone, and two askers naming different registries for one package is a
/// conflict — one installed copy cannot come from both.
bool resolve(const std::vector<Want> &wants, const Index &index,
             std::map<std::string, const Release *> &chosen,
             std::string &error) {
  std::map<std::string, std::vector<Requirement>> reqs;
  std::map<std::string, std::string> from;
  std::vector<RegistrySource> sources = loadRegistries();
  auto addReq = [&](const std::string &name, const std::string &text,
                    const std::string &registry) -> bool {
    Requirement r;
    if (!Requirement::parse(text, r)) {
      error = "'" + text + "' is not a version requirement for " + name;
      return false;
    }
    if (!registry.empty()) {
      if (!findRegistry(sources, registry)) {
        error = "'" + name + "' is asked for from a registry called '" +
                registry + "', which is not configured";
        return false;
      }
      auto it = from.find(name);
      if (it != from.end() && it->second != registry) {
        error = "'" + name + "' is asked for from registry '" + it->second +
                "' and from registry '" + registry + "'";
        return false;
      }
      from[name] = registry;
    }
    for (const Requirement &have : reqs[name])
      if (have.Source == r.Source)
        return true;
    reqs[name].push_back(r);
    return true;
  };
  for (const Want &w : wants)
    if (!addReq(w.Name, w.Req, w.Registry))
      return false;

  for (int round = 0; round < 64; ++round) {
    chosen.clear();
    for (const auto &entry : reqs) {
      const std::string &registry = from[entry.first];
      const Release *pick = nullptr;
      for (const Release *r : index.versionsOf(entry.first, registry)) {
        bool all = true;
        for (const Requirement &q : entry.second)
          if (!q.matches(r->V)) { all = false; break; }
        if (all) { pick = r; break; }
      }
      if (!pick) {
        std::string where =
            registry.empty() ? "no registry" : "registry '" + registry + "'";
        if (index.versionsOf(entry.first, registry).empty()) {
          error = where + " has a package called '" + entry.first + "'";
          if (!registry.empty() && !index.versionsOf(entry.first).empty())
            error += " (" + std::to_string(index.registriesWith(entry.first).size()) +
                     " other registr" +
                     (index.registriesWith(entry.first).size() == 1 ? "y does" : "ies do") +
                     ")";
        } else {
          std::string asked;
          for (const Requirement &q : entry.second)
            asked += (asked.empty() ? "" : ", ") + q.Source;
          std::string have;
          for (const Release *r : index.versionsOf(entry.first, registry))
            have += (have.empty() ? "" : ", ") + r->V.str();
          error = "no version of '" + entry.first + "'" +
                  (registry.empty() ? "" : " in registry '" + registry + "'") +
                  " satisfies " + asked + " (available: " + have + ")";
        }
        return false;
      }
      chosen[entry.first] = pick;
    }
    // What the choices themselves ask for.
    size_t before = 0;
    for (const auto &e : reqs)
      before += e.second.size();
    for (const auto &c : chosen)
      for (const auto &d : c.second->Dependencies) {
        std::string registry, name;
        splitQualified(d.first, registry, name);
        if (!addReq(name, d.second, registry))
          return false;
      }
    size_t after = 0;
    for (const auto &e : reqs)
      after += e.second.size();
    if (after == before)
      return true;
  }
  error = "dependency resolution did not settle";
  return false;
}

fs::path archiveCache(const std::string &sha) {
  return runeHome() / "cache" / "archives" / (sha + ".tar");
}

/// Downloads, verifies and unpacks one release. Safe to run beside others:
/// each writes only under its own directory, and moves it into place whole.
bool installOne(const Release &r, bool verbose, std::string &error) {
  fs::path dir = installDir(r.Name, r.V);
  std::error_code ec;
  if (fs::exists(dir / "Rune.toml", ec))
    return true;
  std::string tar;
  fs::path cached = archiveCache(r.Sha256);
  if (!(r.Sha256.size() == 64 && readFile(cached, tar))) {
    std::string url = joinUrl(r.Registry, r.Archive);
    status("Fetching", r.Name + " v" + r.V.str());
    if (!fetch(url, tar, error))
      return false;
    if (!r.Sha256.empty() && sha256Hex(tar) != r.Sha256) {
      error = r.Name + " v" + r.V.str() + ": the archive's checksum does not match the index";
      return false;
    }
    writeFile(cached, tar);
  }
  fs::path staging = dir.parent_path() / (r.V.str() + ".installing");
  fs::remove_all(staging, ec);
  if (!unpackTar(tar, staging, error))
    return false;
  writeFile(staging / ".rune-origin",
            "registry = " + tomlString(r.Registry) + "\nsha256 = " +
                tomlString(r.Sha256) + "\ninstalled = " + tomlString(nowIso()) + "\n");
  fs::remove_all(dir, ec);
  fs::rename(staging, dir, ec);
  if (ec) {
    error = "cannot move " + r.Name + " into place: " + ec.message();
    return false;
  }
  okLine("Installed " + r.Name + " v" + r.V.str());
  return true;
}

} // namespace

bool resolveAndInstall(const fs::path &projectRoot,
                       const std::vector<Want> &wants,
                       const std::vector<Locked> &lock,
                       const std::vector<std::string> &moving, const Index &index,
                       std::vector<Locked> &out, bool verbose, std::string &error) {
  // Twice: once free, to learn what is reachable, and once with everything
  // reachable and locked held at its version. The pins are dropped again
  // only when they cannot all hold beside what was newly asked for, and
  // then the packages that had to move are named.
  std::map<std::string, const Release *> chosen;
  if (!resolve(wants, index, chosen, error))
    return false;
  std::vector<Want> pinned = wants;
  std::vector<RegistrySource> sources = loadRegistries();
  size_t pins = 0;
  for (const Locked &l : lock) {
    if (!chosen.count(l.Name) ||
        std::find(moving.begin(), moving.end(), l.Name) != moving.end())
      continue;
    // Held where it was: the version, and the registry it came from while
    // that one is still configured.
    Want w;
    w.Name = l.Name;
    w.Req = "=" + l.V.str();
    if (const RegistrySource *src = findRegistry(sources, l.Registry))
      w.Registry = src->Name;
    pinned.push_back(w);
    ++pins;
  }
  if (pins) {
    std::map<std::string, const Release *> held;
    std::string pinError;
    if (resolve(pinned, index, held, pinError)) {
      chosen = held;
    } else {
      for (const Locked &l : lock)
        if (chosen.count(l.Name) && !(chosen[l.Name]->V == l.V))
          warnLine(l.Name + " moves from v" + l.V.str() + " to v" +
                   chosen[l.Name]->V.str() + " to satisfy what was asked for");
    }
  }

  // Everything that is not there yet, fetched at the same time: the index
  // already said what each is, so nothing waits on anything.
  std::vector<const Release *> missing;
  std::error_code ec;
  for (const auto &c : chosen)
    if (!fs::exists(installDir(c.first, c.second->V) / "Rune.toml", ec))
      missing.push_back(c.second);
  if (!missing.empty()) {
    std::vector<std::string> errors(missing.size());
    std::vector<Job> jobs;
    for (size_t i = 0; i < missing.size(); ++i) {
      Job j;
      j.Run = [&, i]() { return installOne(*missing[i], verbose, errors[i]); };
      jobs.push_back(std::move(j));
    }
    if (!runGraph(jobs)) {
      for (const std::string &e : errors)
        if (!e.empty())
          error += (error.empty() ? "" : "; ") + e;
      return false;
    }
  }

  std::string key = projectKey(projectRoot);
  out.clear();
  for (const auto &c : chosen) {
    addReference(installDir(c.first, c.second->V), key);
    Locked l;
    l.Name = c.first;
    l.V = c.second->V;
    l.Registry = c.second->Registry;
    l.Sha256 = c.second->Sha256;
    out.push_back(l);
  }
  return true;
}

namespace {

/// The project whose lock pins a build. Set by the build front end before it
/// walks the dependency graph, so a registry package's own dependencies are
/// resolved by the root project's lock rather than a lock of their own.
fs::path gLockProject;

/// The registry dependencies a manifest names.
std::vector<Want> registryWants(const Manifest &m) {
  std::vector<Want> out;
  for (const Dependency &d : m.Dependencies)
    if (d.Path.empty())
      out.push_back({d.Name, d.Version.empty() ? "*" : d.Version, d.Registry});
  return out;
}

} // namespace

namespace {

/// True when the lock's pin for `d` still answers what the manifest asks:
/// a version the requirement accepts, from the registry it names, if it
/// names one. A manifest edited by hand — `stats = "=2.2.0"` — makes the
/// pin stale, and a stale pin must not be built against in silence.
bool pinSatisfies(const Dependency &d, const Locked &l,
                  const std::vector<RegistrySource> &sources, std::string &why) {
  Requirement req;
  if (!Requirement::parse(d.Version.empty() ? "*" : d.Version, req)) {
    why = "'" + d.Version + "' is not a version requirement";
    return false;
  }
  if (!req.matches(l.V)) {
    why = "Rune.lock pins v" + l.V.str() + ", which " + req.Source + " does not allow";
    return false;
  }
  if (!d.Registry.empty() && registryNameOf(sources, l.Registry) != d.Registry) {
    why = "Rune.lock pins the copy from " + registryNameOf(sources, l.Registry) +
          ", not from registry '" + d.Registry + "'";
    return false;
  }
  return true;
}

} // namespace

fs::path resolveRegistryDependency(const Manifest &m, const Dependency &d,
                                   bool verbose) {
  fs::path lockRoot = gLockProject.empty() ? fs::path(m.Root) : gLockProject;
  std::vector<Locked> lock = loadLock(lockRoot);
  std::vector<RegistrySource> sources = loadRegistries();
  std::error_code ec;
  // What the pin has to move for: the package itself when the manifest no
  // longer accepts what was pinned, nothing otherwise.
  std::vector<std::string> moving;
  for (const Locked &l : lock)
    if (l.Name == d.Name) {
      std::string why;
      if (!pinSatisfies(d, l, sources, why)) {
        status("Resolving", d.Name + " again: " + why);
        moving.push_back(d.Name);
        break;
      }
      fs::path dir = installDir(l.Name, l.V);
      if (fs::exists(dir / "Rune.toml", ec)) {
        addReference(dir, projectKey(lockRoot));
        return dir;
      }
      break;
    }
  // Not pinned, pinned to something the manifest no longer asks for, or
  // pinned to something no longer installed: resolve it now, with
  // everything else the project asks for, and pin the result.
  Index index;
  std::string error;
  if (!loadMergedIndex(index, /*refresh=*/false, error)) {
    failLine("dependency '" + d.Name + "' needs a registry: " + error);
    note("add one with `rune registry add <url>`, or give the dependency a `path`");
    return {};
  }
  Manifest rootManifest;
  std::string mErr;
  if (!loadManifest(lockRoot.string(), rootManifest, mErr, false))
    rootManifest = m;
  auto wants = registryWants(rootManifest);
  bool named = false;
  for (const Want &w : wants)
    if (w.Name == d.Name)
      named = true;
  if (!named)
    wants.push_back({d.Name, d.Version.empty() ? "*" : d.Version, d.Registry});
  std::vector<Locked> resolved;
  if (!resolveAndInstall(lockRoot, wants, lock, moving, index, resolved, verbose,
                         error)) {
    failLine("cannot resolve dependency '" + d.Name + "': " + error);
    if (!moving.empty())
      note("`rune desc " + d.Name + "` lists the versions there are; the "
           "requirement is in " + (lockRoot / "Rune.toml").string());
    return {};
  }
  if (!saveLock(lockRoot, resolved, error))
    warnLine(error);
  for (const Locked &l : resolved)
    if (l.Name == d.Name)
      return installDir(l.Name, l.V);
  return {};
}

void setLockProject(const fs::path &root) { gLockProject = root; }

fs::path locatePackage(const std::string &projectDir, const std::string &name,
                       const std::string &registry, bool verbose,
                       std::string &error) {
  std::vector<RegistrySource> sources = loadRegistries();
  if (!registry.empty() && !findRegistry(sources, registry)) {
    error = "no registry called '" + registry + "' is configured";
    return {};
  }
  std::error_code ec;
  auto fromRegistry = [&](const std::string &url) {
    return registry.empty() || registryNameOf(sources, url) == registry;
  };

  // The project here, when it depends on the package: what it uses is what
  // to read about.
  Manifest m;
  std::string mErr;
  if (loadManifest(projectDir, m, mErr, /*requireSources=*/false)) {
    for (const Dependency &d : m.Dependencies) {
      if (d.Name != name)
        continue;
      if (!d.Path.empty()) {
        if (registry.empty())
          return fs::absolute(fs::path(m.Root) / d.Path).lexically_normal();
        break;
      }
      for (const Locked &l : loadLock(m.Root)) {
        if (l.Name != name || !fromRegistry(l.Registry))
          continue;
        fs::path dir = installDir(l.Name, l.V);
        if (fs::exists(dir / "Rune.toml", ec))
          return dir;
      }
    }
  }

  // Anything installed: the newest version.
  std::vector<Installed> all = listInstalled();
  const Installed *newest = nullptr;
  for (const Installed &i : all)
    if (i.Name == name && fromRegistry(i.Registry) && (!newest || newest->V < i.V))
      newest = &i;
  if (newest)
    return newest->Dir;

  // Nothing here: the newest release a registry offers, fetched now. It is
  // installed like any other package, referenced by nobody, so it is
  // `rune remove`'s to reclaim.
  Index index;
  if (!loadMergedIndex(index, /*refresh=*/false, error))
    return {};
  std::vector<const Release *> versions = index.versionsOf(name, registry);
  if (versions.empty()) {
    if (registry.empty()) {
      error = "no registry has a package called '" + name + "', and none is installed";
    } else {
      error = "registry '" + registry + "' has no package called '" + name + "'";
      std::vector<std::string> elsewhere = index.registriesWith(name);
      if (!elsewhere.empty()) {
        error += " (it is in: ";
        for (size_t i = 0; i < elsewhere.size(); ++i)
          error += (i ? ", " : "") + elsewhere[i];
        error += ")";
      }
    }
    return {};
  }
  const Release *r = versions.front();
  if (!installOne(*r, verbose, error))
    return {};
  note("installed to read; no project depends on it, so `rune remove` with "
       "no arguments would uninstall it again");
  return installDir(r->Name, r->V);
}

//===----------------------------------------------------------------------===//
// Editing Rune.toml
//
// A manifest is the author's file, comments and all, so it is edited as text
// rather than parsed and written back: one line under `[dependencies]` is
// added, replaced, or removed, and nothing else moves.
//===----------------------------------------------------------------------===//

namespace {

bool isSectionHeader(const std::string &line) {
  std::string t = trim(line);
  return !t.empty() && t[0] == '[';
}

/// The key a dependency line names, or empty when the line is not one.
std::string keyOfLine(const std::string &line) {
  std::string t = trim(line);
  if (t.empty() || t[0] == '#' || t[0] == '[')
    return "";
  size_t eq = t.find('=');
  if (eq == std::string::npos)
    return "";
  std::string key = trim(t.substr(0, eq));
  if (!key.empty() && key.front() == '"' && key.back() == '"' && key.size() >= 2)
    key = key.substr(1, key.size() - 2);
  return key;
}

bool editDependencies(const fs::path &manifest, const std::string &name,
                      const std::string &newLine, bool remove, std::string &error) {
  std::string text;
  if (!readFile(manifest, text)) {
    error = "cannot read " + manifest.string();
    return false;
  }
  std::vector<std::string> lines = splitOn(text, '\n');
  bool trailingNewline = !text.empty() && text.back() == '\n';
  if (trailingNewline)
    lines.pop_back();

  size_t sectionStart = lines.size(), sectionEnd = lines.size();
  for (size_t i = 0; i < lines.size(); ++i) {
    if (trim(lines[i]) == "[dependencies]") {
      sectionStart = i;
      sectionEnd = lines.size();
      for (size_t j = i + 1; j < lines.size(); ++j)
        if (isSectionHeader(lines[j])) { sectionEnd = j; break; }
      break;
    }
  }
  if (sectionStart == lines.size()) {
    if (remove)
      return true;
    if (!lines.empty() && !trim(lines.back()).empty())
      lines.push_back("");
    lines.push_back("[dependencies]");
    lines.push_back(newLine);
  } else {
    bool replaced = false;
    for (size_t i = sectionStart + 1; i < sectionEnd; ++i) {
      if (keyOfLine(lines[i]) != name)
        continue;
      if (remove) {
        lines.erase(lines.begin() + static_cast<long>(i));
      } else {
        lines[i] = newLine;
      }
      replaced = true;
      break;
    }
    if (!replaced && !remove) {
      // After the last dependency already there, or straight under the
      // header when there is none — never after a trailing comment.
      size_t at = sectionStart + 1;
      for (size_t i = sectionStart + 1; i < sectionEnd; ++i)
        if (!keyOfLine(lines[i]).empty())
          at = i + 1;
      lines.insert(lines.begin() + static_cast<long>(at), newLine);
    }
  }
  std::string out;
  for (size_t i = 0; i < lines.size(); ++i)
    out += lines[i] + "\n";
  if (!writeFile(manifest, out)) {
    error = "cannot write " + manifest.string();
    return false;
  }
  return true;
}

/// `name@1.2` or `name` on the command line, taken apart.
void splitSpec(const std::string &spec, std::string &name, std::string &req) {
  size_t at = spec.find('@');
  if (at == std::string::npos) {
    name = spec;
    req.clear();
  } else {
    name = spec.substr(0, at);
    req = spec.substr(at + 1);
  }
}

bool loadProject(const std::string &dir, Manifest &m) {
  std::string error;
  if (!loadManifest(dir, m, error, /*requireSources=*/false)) {
    failLine(error);
    return false;
  }
  return true;
}

void warnUnreferenced() {
  size_t unused = 0;
  for (const Installed &i : listInstalled())
    if (i.Refs.empty())
      ++unused;
  if (unused)
    warnLine(std::to_string(unused) + " installed package version" +
             (unused == 1 ? " is" : "s are") + " not used by any project; "
             "`rune remove` with no arguments uninstalls " +
             (unused == 1 ? "it" : "them"));
}

} // namespace

//===----------------------------------------------------------------------===//
// rune registry: the server side
//===----------------------------------------------------------------------===//

namespace {

int registryInit(const fs::path &dir, const std::string &name) {
  std::error_code ec;
  if (fs::exists(dir / "index.toml", ec)) {
    failLine("'" + dir.string() + "' is already a package registry");
    return 1;
  }
  fs::create_directories(dir / "packages", ec);
  Index index;
  index.Name = name;
  index.Updated = nowIso();
  if (!writeFile(dir / "registry.toml",
                 "# A Rune package registry: static files, served by `rune registry "
                 "server --serve`\n# or by any web server, or read straight off "
                 "a disk.\n\n[registry]\nname = " + tomlString(name) +
                     "\ncreated = " + tomlString(nowIso()) + "\n") ||
      !writeFile(dir / "index.toml", index.serialise())) {
    failLine("cannot write into " + dir.string());
    return 1;
  }
  okLine("Created registry '" + name + "' in " + fs::absolute(dir).lexically_normal().string());
  note("add packages with `rune registry --addPackage <project>`, serve it with "
       "`rune registry --serve`");
  return 0;
}

int registryAddPackage(const fs::path &dir, const fs::path &project, bool force,
                  bool verbose) {
  std::error_code ec;
  if (!fs::exists(dir / "index.toml", ec)) {
    failLine("'" + dir.string() + "' is not a package registry");
    note("run `rune registry init` there first, or say which with `--dir`");
    return 1;
  }
  Manifest m;
  std::string error;
  if (!loadManifest(project.string(), m, error, /*requireSources=*/false)) {
    failLine(error);
    return 1;
  }
  Version v;
  if (!Version::parse(m.Version, v)) {
    failLine("'" + m.Version + "' is not a version — the manifest needs "
             "`version = \"major.minor.patch\"`");
    return 1;
  }
  std::string text;
  Index index;
  if (!readFile(dir / "index.toml", text) || !index.parse(text, error)) {
    failLine("cannot read the registry's index: " + error);
    return 1;
  }
  for (const Release &r : index.Releases)
    if (r.Name == m.Name && r.V == v && !force) {
      failLine(m.Name + " v" + v.str() + " is already in this registry");
      note("bump the version in Rune.toml, or pass --force to replace it");
      return 1;
    }
  status("Packing", m.Name + " v" + v.str());
  std::string tar;
  if (!packPackage(project, tar, error)) {
    failLine(error);
    return 1;
  }
  Release r;
  r.Name = m.Name;
  r.V = v;
  r.Description = m.Description;
  r.Authors = m.Authors;
  r.License = m.License;
  r.Archive = "packages/" + m.Name + "/" + m.Name + "-" + v.str() + ".tar";
  r.Sha256 = sha256Hex(tar);
  r.Size = tar.size();
  r.Added = nowIso();
  for (const Dependency &d : m.Dependencies) {
    // A dependency that insists on a registry keeps the insistence: the
    // index says `registry::name`, and a client resolves it there.
    std::string name = (d.Registry.empty() ? "" : d.Registry + "::") + d.Name;
    if (!d.Path.empty())
      warnLine("dependency '" + d.Name + "' is a path dependency, which a "
               "package from a registry cannot follow; it is recorded as "
               "`" + name + " " + (d.Version.empty() ? "*" : d.Version) + "`");
    r.Dependencies.push_back({name, d.Version.empty() ? "*" : d.Version});
  }
  if (!writeFile(dir / r.Archive, tar)) {
    failLine("cannot write " + (dir / r.Archive).string());
    return 1;
  }
  index.Releases.erase(
      std::remove_if(index.Releases.begin(), index.Releases.end(),
                     [&](const Release &x) { return x.Name == r.Name && x.V == v; }),
      index.Releases.end());
  index.Releases.push_back(r);
  index.Updated = nowIso();
  if (!writeFile(dir / "index.toml", index.serialise())) {
    failLine("cannot write the index");
    return 1;
  }
  okLine("Added " + m.Name + " v" + v.str() + " (" + humanSize(tar.size()) +
         ", sha256 " + r.Sha256.substr(0, 12) + "…)");
  return 0;
}

int registryAddSource(const std::string &url, const std::string &alias) {
  std::vector<RegistrySource> list = loadRegistries();
  for (const RegistrySource &r : list)
    if (r.Url == url) {
      okLine("'" + url + "' is already configured as '" + r.Name + "'");
      return 0;
    }
  if (!alias.empty() && !isRegistryName(alias)) {
    failLine("'" + alias + "' cannot name a registry");
    note("a name is letters, digits, `_`, `-` and `.`: it has to fit in front "
         "of `::package`");
    return 2;
  }
  // Say hello, so a typo is caught now rather than at the first search —
  // and learn what the registry calls itself, which is its name here
  // unless an alias was asked for.
  std::string text, error;
  if (!fetch(joinUrl(url, "index.toml"), text, error)) {
    failLine("cannot reach a registry at '" + url + "': " + error);
    note("a registry has an index.toml at its root; `rune registry init` makes one");
    return 1;
  }
  Index index;
  if (!index.parse(text, error)) {
    failLine("'" + url + "' has an index this toolchain cannot read: " + error);
    return 1;
  }
  if (!isRegistryName(index.Name)) {
    failLine("the registry at '" + url + "' does not say what it is called");
    note("a registry must provide a name: `name = \"...\"` under `[registry]` "
         "in its index.toml, which `rune registry init` writes");
    return 1;
  }
  RegistrySource r;
  r.Url = url;
  r.Added = nowIso();
  r.Declared = index.Name;
  r.Name = alias.empty() ? index.Name : alias;
  if (const RegistrySource *taken = findRegistry(list, r.Name)) {
    failLine("a registry called '" + r.Name + "' is already configured, at " +
             taken->Url);
    note("give this one another name here with `--name <alias>`");
    return 1;
  }
  list.push_back(r);
  if (!saveRegistries(list, error)) {
    failLine(error);
    return 1;
  }
  writeFile(indexCacheFor(url), text);
  okLine("Added registry '" + r.Name + "' at " + url + " (" +
         std::to_string(index.Releases.size()) + " release" +
         (index.Releases.size() == 1 ? "" : "s") + ")");
  if (r.Name != r.Declared)
    note("it calls itself '" + r.Declared + "'; here it is `" + r.Name +
         "`, as in `rune add " + r.Name + "::<package>`");
  warnLine("this registry is not monitored: nothing here reviews what it "
           "serves, so read a package before you depend on it");
  return 0;
}

int registryListSources(bool verbose) {
  std::vector<RegistrySource> list = loadRegistries();
  if (list.empty()) {
    okLine("no registries are configured");
    note("add one with `rune registry add <url>`");
    return 0;
  }
  size_t width = 4;
  for (const RegistrySource &r : list)
    width = std::max(width, r.Name.size());
  for (const RegistrySource &r : list) {
    std::string line = std::string(c("\x1b[1m")) + r.Name + c("\x1b[0m") +
                       std::string(width - r.Name.size() + 2, ' ') + r.Url;
    // What the cached index says, when there is one; nothing is fetched.
    std::string text, error;
    Index index;
    if (readFile(indexCacheFor(r.Url), text) && index.parse(text, error))
      line += "  " + std::to_string(index.Releases.size()) + " release" +
              (index.Releases.size() == 1 ? "" : "s");
    else
      line += std::string("  ") + c("\x1b[2m") + "(index not fetched yet)" + c("\x1b[0m");
    if (r.Declared != r.Name)
      line += std::string("  ") + c("\x1b[2m") + "calls itself '" + r.Declared + "'" +
              c("\x1b[0m");
    plain(line);
    if (verbose)
      plain("    added " + r.Added);
  }
  return 0;
}

int registryRemoveSource(const std::string &nameOrUrl) {
  std::vector<RegistrySource> list = loadRegistries();
  const RegistrySource *found = findRegistry(list, nameOrUrl);
  if (!found) {
    failLine("no registry called '" + nameOrUrl + "' is configured");
    note("`rune registry list` shows them");
    return 1;
  }
  RegistrySource gone = *found;
  list.erase(std::remove_if(list.begin(), list.end(),
                            [&](const RegistrySource &r) { return r.Url == gone.Url; }),
             list.end());
  std::string error;
  if (!saveRegistries(list, error)) {
    failLine(error);
    return 1;
  }
  std::error_code ec;
  fs::remove(indexCacheFor(gone.Url), ec);
  okLine("Removed registry '" + gone.Name + "' (" + gone.Url + ")");
  // What came from it stays installed and stays pinned: a lock names the
  // registry by URL, so a build still knows where each package came from.
  size_t fromIt = 0;
  for (const Installed &i : listInstalled())
    if (i.Registry == gone.Url)
      ++fromIt;
  if (fromIt)
    note(std::to_string(fromIt) + " installed package version" +
         (fromIt == 1 ? " came" : "s came") + " from it; " +
         (fromIt == 1 ? "it stays" : "they stay") +
         " installed, but nothing new can be fetched from there");
  return 0;
}

} // namespace

int commandRegistry(const std::vector<std::string> &args, bool verbose) {
  auto usage = [&]() {
    plain("usage: rune registry init [dir] [--name N]   make a registry here, or in dir\n"
          "       rune registry new <dir> [--name N]    make a registry in a new dir\n"
          "       rune registry --serve [--port N] [--dir D]\n"
          "       rune registry --addPackage <project> [--dir D] [--force]\n"
          "       rune registry add <url> [--name N]    use a registry from this machine\n"
          "       rune registry list                    the registries this machine uses\n"
          "       rune registry remove <name>           stop using one");
    return 2;
  };
  if (args.empty())
    return usage();
  const std::string &sub = args[0];
  if (sub == "init" || sub == "new") {
    fs::path dir;
    std::string name;
    for (size_t i = 1; i < args.size(); ++i) {
      const std::string &a = args[i];
      if ((a == "--name" || a == "-n") && i + 1 < args.size()) name = args[++i];
      else if (!a.empty() && a[0] == '-') { failLine("unknown argument '" + a + "'"); return usage(); }
      else if (dir.empty()) dir = a;
      else { failLine("unexpected argument '" + a + "'"); return usage(); }
    }
    if (sub == "new" && dir.empty()) {
      failLine("`rune registry new` needs a directory");
      return 2;
    }
    if (dir.empty())
      dir = ".";
    std::error_code ec;
    fs::create_directories(dir, ec);
    // The registry's name is what every client will call it, unless they
    // alias it: the directory's name is the default, `--name` the choice.
    if (name.empty())
      name = fs::absolute(dir).lexically_normal().filename().string();
    if (name.empty() || name == ".")
      name = "registry";
    if (!isRegistryName(name)) {
      failLine("'" + name + "' cannot name a registry");
      note("a name is letters, digits, `_`, `-` and `.`: it has to fit in front "
           "of `::package`");
      return 2;
    }
    return registryInit(dir, name);
  }

  // Everything else is one action, named by a word (`add`, `list`,
  // `remove`) or a flag (`--serve`, `--addPackage`), with its options
  // anywhere after it. All of the arguments are read before anything is
  // done, so `--name` and `--dir` count wherever they are written.
  bool serve = false, force = false, list = false;
  int port = 7878;
  fs::path dir = ".";
  std::string addPackage, addUrl, removeName, name;
  for (size_t i = 0; i < args.size(); ++i) {
    const std::string &a = args[i];
    if (a == "--serve" || a == "serve") serve = true;
    else if (a == "--force") force = true;
    else if (a == "list" || a == "--list") list = true;
    else if ((a == "--port" || a == "-p") && i + 1 < args.size()) port = std::atoi(args[++i].c_str());
    else if ((a == "--dir" || a == "-d") && i + 1 < args.size()) dir = args[++i];
    else if (a == "--addPackage" || a == "--add-package" || a == "add-package") {
      if (i + 1 >= args.size()) { failLine("--addPackage needs a project directory"); return 2; }
      addPackage = args[++i];
    } else if ((a == "--name" || a == "-n") && i + 1 < args.size()) name = args[++i];
    else if (a == "add") {
      if (i + 1 >= args.size()) { failLine("`rune registry add` needs a URL"); return 2; }
      addUrl = args[++i];
    } else if (a == "remove" || a == "--remove") {
      if (i + 1 >= args.size()) { failLine("`rune registry remove` needs a registry's name"); return 2; }
      removeName = args[++i];
    } else {
      failLine("unknown `rune registry` argument '" + a + "'");
      return usage();
    }
  }
  if (!addUrl.empty())
    return registryAddSource(addUrl, name);
  if (list)
    return registryListSources(verbose);
  if (!removeName.empty())
    return registryRemoveSource(removeName);
  if (!addPackage.empty())
    return registryAddPackage(dir, addPackage, force, verbose);
  if (serve)
    return serveDirectory(dir, port, verbose);
  return usage();
}

//===----------------------------------------------------------------------===//
// rune search / desc / installed
//===----------------------------------------------------------------------===//

namespace {

/// Takes `--registry <name>` (or `-r`) out of `args`, leaving the rest in
/// `rest`. False, with the reason reported, when the name is not one of the
/// configured registries.
bool takeRegistryFlag(const std::vector<std::string> &args, std::string &registry,
                      std::vector<std::string> &rest) {
  registry.clear();
  rest.clear();
  for (size_t i = 0; i < args.size(); ++i) {
    const std::string &a = args[i];
    if ((a == "--registry" || a == "-r") && i + 1 < args.size()) {
      registry = args[++i];
    } else if (a.rfind("--registry=", 0) == 0) {
      registry = a.substr(11);
    } else {
      rest.push_back(a);
    }
  }
  if (!registry.empty() && !findRegistry(loadRegistries(), registry)) {
    failLine("no registry called '" + registry + "' is configured");
    note("`rune registry list` shows them");
    return false;
  }
  return true;
}

/// `registry::name` from the command line, with `--registry` as the
/// fallback. False when the two disagree, or the prefix names no registry.
bool qualify(const std::string &spec, const std::string &flag,
             std::string &registry, std::string &rest) {
  splitQualified(spec, registry, rest);
  if (!registry.empty() && !findRegistry(loadRegistries(), registry)) {
    failLine("no registry called '" + registry + "' is configured");
    note("`rune registry list` shows them");
    return false;
  }
  if (!registry.empty() && !flag.empty() && registry != flag) {
    failLine("'" + spec + "' names registry '" + registry +
             "', but --registry says '" + flag + "'");
    return false;
  }
  if (registry.empty())
    registry = flag;
  return true;
}

/// `[registry]`, dimmed, for a line that should say where something is
/// from — which is only worth saying when there is more than one place.
std::string registryTag(const std::vector<RegistrySource> &sources,
                        const std::string &name) {
  if (sources.size() < 2 || name.empty())
    return "";
  return std::string("  ") + c("\x1b[2m") + "[" + name + "]" + c("\x1b[0m");
}

} // namespace

int commandSearch(const std::vector<std::string> &args, bool verbose) {
  bool refresh = false;
  std::string flag, pattern;
  std::vector<std::string> rest;
  if (!takeRegistryFlag(args, flag, rest))
    return 2;
  for (const std::string &a : rest) {
    if (a == "--refresh") refresh = true;
    else if (pattern.empty()) pattern = a;
  }
  // `work::geo.*` searches one registry, exactly as `--registry work geo.*`.
  std::string registry, bare;
  if (!qualify(pattern, flag, registry, bare))
    return 2;
  pattern = bare;
  Index index;
  std::string error;
  if (!loadMergedIndex(index, refresh, error)) {
    failLine(error);
    if (loadRegistries().empty())
      note("add one with `rune registry add <url>`");
    return 1;
  }
  std::regex re;
  try {
    re = std::regex(pattern.empty() ? "." : pattern,
                    std::regex::ECMAScript | std::regex::icase);
  } catch (const std::regex_error &e) {
    failLine("'" + pattern + "' is not a regular expression: " + e.what());
    return 2;
  }
  std::vector<RegistrySource> sources = loadRegistries();
  std::set<std::string> names;
  for (const Release &r : index.Releases)
    if ((registry.empty() || r.RegistryName == registry) &&
        (std::regex_search(r.Name, re) || std::regex_search(r.Description, re)))
      names.insert(r.Name);
  if (names.empty()) {
    okLine("nothing matches '" + pattern + "' in " +
           (registry.empty()
                ? std::to_string(sources.size()) + " registr" + (sources.size() == 1 ? "y" : "ies")
                : "registry '" + registry + "'"));
    return 0;
  }
  size_t width = 4;
  for (const std::string &n : names)
    width = std::max(width, n.size());
  for (const std::string &n : names) {
    const Release *latest = index.versionsOf(n, registry).front();
    std::string line = n + std::string(width - n.size() + 2, ' ') + "v" + latest->V.str();
    line += std::string(std::max<size_t>(1, 10 - latest->V.str().size()), ' ');
    line += latest->Description.empty() ? "(no description)" : latest->Description;
    line += registryTag(sources, latest->RegistryName);
    plain(line);
  }
  return 0;
}

int commandDesc(const std::vector<std::string> &args, bool verbose) {
  std::string flag;
  std::vector<std::string> specs;
  if (!takeRegistryFlag(args, flag, specs))
    return 2;
  if (specs.empty()) {
    failLine("`rune desc` needs a package name");
    return 2;
  }
  Index index;
  std::string error;
  if (!loadMergedIndex(index, /*refresh=*/false, error)) {
    failLine(error);
    return 1;
  }
  std::vector<RegistrySource> sources = loadRegistries();
  int rc = 0;
  for (const std::string &spec : specs) {
    std::string registry, name;
    if (!qualify(spec, flag, registry, name))
      return 2;
    std::vector<const Release *> versions = index.versionsOf(name, registry);
    if (versions.empty()) {
      if (registry.empty()) {
        failLine("no registry has a package called '" + name + "'");
      } else {
        failLine("registry '" + registry + "' has no package called '" + name + "'");
        std::vector<std::string> elsewhere = index.registriesWith(name);
        if (!elsewhere.empty()) {
          std::string list;
          for (const std::string &r : elsewhere)
            list += (list.empty() ? "" : ", ") + r;
          note("it is in: " + list);
        }
      }
      rc = 1;
      continue;
    }
    const Release *latest = versions.front();
    plain(std::string(c("\x1b[1m")) + latest->Name + c("\x1b[0m") + " v" + latest->V.str());
    if (!latest->Description.empty()) plain("  " + latest->Description);
    if (!latest->Authors.empty()) {
      std::string who;
      for (const std::string &a : latest->Authors) who += (who.empty() ? "" : ", ") + a;
      plain("  authors:      " + who);
    }
    if (!latest->License.empty()) plain("  license:      " + latest->License);
    plain("  registry:     " + latest->RegistryName + " (" + latest->Registry + ")");
    // The versions this registry has; the others get a line of their own,
    // so `rune add other::name` is an obvious next step.
    std::string vs;
    for (const Release *r : versions)
      if (r->RegistryName == latest->RegistryName)
        vs += (vs.empty() ? "" : ", ") + r->V.str();
    plain("  versions:     " + vs);
    for (const std::string &other : index.registriesWith(name)) {
      if (other == latest->RegistryName)
        continue;
      std::string ovs;
      for (const Release *r : index.versionsOf(name, other))
        ovs += (ovs.empty() ? "" : ", ") + r->V.str();
      plain("  also in:      " + other + " (" + ovs + ") — `rune add " + other +
            "::" + name + "` takes it from there");
    }
    plain("  latest added: " + latest->Added + ", " + humanSize(latest->Size));
    if (!latest->Dependencies.empty()) {
      std::string deps;
      for (const auto &d : latest->Dependencies)
        deps += (deps.empty() ? "" : ", ") + d.first + " " + d.second;
      plain("  depends on:   " + deps);
    }
    std::string installed;
    for (const Installed &i : listInstalled())
      if (i.Name == name)
        installed += (installed.empty() ? "" : ", ") + i.V.str() + " (" +
                     std::to_string(i.Refs.size()) + " project" +
                     (i.Refs.size() == 1 ? "" : "s") + ")";
    plain("  installed:    " + (installed.empty() ? "no" : installed));
  }
  return rc;
}

int commandInstalled(const std::vector<std::string> &args, bool verbose) {
  std::string registry;
  std::vector<std::string> rest;
  if (!takeRegistryFlag(args, registry, rest))
    return 2;
  if (!rest.empty()) {
    failLine("unknown argument '" + rest.front() + "'");
    note("`rune installed [--registry <name>]` takes nothing else");
    return 2;
  }
  std::vector<RegistrySource> sources = loadRegistries();
  std::vector<Installed> all;
  for (Installed &i : listInstalled())
    if (registry.empty() || registryNameOf(sources, i.Registry) == registry)
      all.push_back(std::move(i));
  if (all.empty()) {
    if (registry.empty())
      okLine("nothing is installed under " + (runeHome() / "registry").string());
    else
      okLine("nothing from registry '" + registry + "' is installed");
    return 0;
  }
  size_t width = 4;
  for (const Installed &i : all)
    width = std::max(width, i.Name.size() + i.V.str().size() + 2);
  for (const Installed &i : all) {
    std::string label = i.Name + " v" + i.V.str();
    std::string line = label + std::string(width - label.size() + 2, ' ');
    // The registry column lines up only if this one is padded, and it is
    // only a column when there is more than one registry to name.
    std::string uses = i.Refs.empty()
                           ? "unused"
                           : std::to_string(i.Refs.size()) + " project" +
                                 (i.Refs.size() == 1 ? "" : "s");
    if (i.Refs.empty())
      line += std::string(c("\x1b[2m")) + uses + c("\x1b[0m");
    else
      line += uses;
    if (registry.empty() && sources.size() > 1) {
      line += std::string(uses.size() < 11 ? 11 - uses.size() : 0, ' ');
      line += registryTag(sources, registryNameOf(sources, i.Registry));
    }
    if (verbose)
      for (const std::string &r : i.Refs) line += "\n    " + r;
    plain(line);
  }
  if (registry.empty())
    warnUnreferenced();
  return 0;
}

//===----------------------------------------------------------------------===//
// rune add / remove / update / deps
//===----------------------------------------------------------------------===//

/// A `--config` value as TOML: a number or a boolean as written, anything
/// else quoted. `@Config` compares the text either way; this keeps the
/// manifest reading the way somebody would have typed it.
std::string tomlConfigValue(const std::string &v) {
  if (v == "true" || v == "false")
    return v;
  bool numeric = !v.empty();
  for (size_t i = 0; i < v.size(); ++i) {
    const char c = v[i];
    if (c == '-' && i == 0 && v.size() > 1)
      continue;
    if (c < '0' || c > '9')
      numeric = false;
  }
  return numeric ? v : tomlString(v);
}

int commandAdd(const std::string &projectDir, const std::vector<std::string> &args,
               bool verbose) {
  std::string flag;
  std::vector<std::string> rest;
  if (!takeRegistryFlag(args, flag, rest))
    return 2;
  // `--config key=value`: what this project chooses for the package's own
  // `[config]` keys. Written into the dependency's entry, so every build of
  // this project makes the same choice.
  std::vector<std::pair<std::string, std::string>> chosen;
  std::vector<std::string> specs;
  for (size_t i = 0; i < rest.size(); ++i) {
    std::string a = rest[i];
    if ((a == "--config" || a == "-c") && i + 1 < rest.size())
      a = "--config=" + rest[++i];
    if (a.rfind("--config=", 0) != 0) {
      specs.push_back(rest[i]);
      continue;
    }
    std::string pair = a.substr(9);
    size_t eq = pair.find('=');
    if (eq == std::string::npos || eq == 0) {
      failLine("`--config` takes `key=value`, as in `--config backend=vulkan`");
      return 2;
    }
    chosen.push_back({pair.substr(0, eq), pair.substr(eq + 1)});
  }
  if (!chosen.empty() && specs.size() != 1) {
    failLine("`--config` applies to one package at a time");
    note("run `rune add` once per package when each needs its own choices");
    return 2;
  }
  if (specs.empty()) {
    failLine("`rune add` needs a package name, optionally with a version: "
             "`rune add geometry@0.2`, or `rune add work::geometry` from one registry");
    return 2;
  }
  Manifest m;
  if (!loadProject(projectDir, m))
    return 1;
  Index index;
  std::string error;
  if (!loadMergedIndex(index, /*refresh=*/true, error)) {
    failLine(error);
    if (loadRegistries().empty())
      note("add one with `rune registry add <url>`");
    return 1;
  }
  // What the project already asks for stays; what it asks for now joins it.
  auto wants = registryWants(m);
  std::vector<Want> added;
  for (const std::string &spec : specs) {
    std::string registry, bare, name, req;
    if (!qualify(spec, flag, registry, bare))
      return 2;
    splitSpec(bare, name, req);
    if (index.versionsOf(name, registry).empty()) {
      if (registry.empty()) {
        failLine("no registry has a package called '" + name + "'");
        note("`rune search " + name + "` looks for something like it");
      } else {
        failLine("registry '" + registry + "' has no package called '" + name + "'");
        std::vector<std::string> elsewhere = index.registriesWith(name);
        if (!elsewhere.empty()) {
          std::string list;
          for (const std::string &r : elsewhere)
            list += (list.empty() ? "" : ", ") + r;
          note("it is in: " + list);
        }
      }
      return 1;
    }
    if (req.empty())
      req = index.versionsOf(name, registry).front()->V.str();
    Requirement check;
    if (!Requirement::parse(req, check)) {
      failLine("'" + req + "' is not a version requirement");
      return 2;
    }
    Want w{name, req, registry};
    bool replaced = false;
    for (Want &have : wants)
      if (have.Name == name) { have = w; replaced = true; }
    if (!replaced)
      wants.push_back(w);
    added.push_back(w);
  }
  std::vector<Locked> lock = loadLock(m.Root);
  std::vector<std::string> moving;
  for (const Want &a : added)
    moving.push_back(a.Name);
  std::vector<Locked> resolved;
  if (!resolveAndInstall(m.Root, wants, lock, moving, index, resolved, verbose,
                         error)) {
    failLine(error);
    return 1;
  }
  fs::path manifestPath = fs::path(m.Root) / "Rune.toml";
  for (const Want &a : added) {
    // `name = "req"`, or the table form when the registry is part of the
    // ask: a build elsewhere has to make the same choice.
    std::string configText;
    for (const auto &kv : chosen)
      configText += (configText.empty() ? "" : ", ") + kv.first + " = " +
                    tomlConfigValue(kv.second);
    std::string line;
    if (a.Registry.empty() && configText.empty()) {
      line = a.Name + " = " + tomlString(a.Req);
    } else {
      line = a.Name + " = { version = " + tomlString(a.Req);
      if (!a.Registry.empty())
        line += ", registry = " + tomlString(a.Registry);
      if (!configText.empty())
        line += ", config = { " + configText + " }";
      line += " }";
    }
    if (!editDependencies(manifestPath, a.Name, line, false, error)) {
      failLine(error);
      return 1;
    }
    for (const Locked &l : resolved)
      if (l.Name == a.Name)
        okLine("Added " + a.Name + " " + tomlString(a.Req) +
               (a.Registry.empty() ? "" : " from " + a.Registry) +
               " to Rune.toml (v" + l.V.str() + " installed)");
  }
  if (!saveLock(m.Root, resolved, error)) {
    failLine(error);
    return 1;
  }
  return 0;
}

int commandRemove(const std::string &projectDir, const std::vector<std::string> &args,
                  bool verbose) {
  // With nothing named: uninstall every version no project references.
  if (args.empty()) {
    size_t removed = 0;
    std::error_code ec;
    for (const Installed &i : listInstalled()) {
      if (!i.Refs.empty())
        continue;
      fs::remove_all(i.Dir, ec);
      // An empty package directory goes with its last version.
      fs::path parent = i.Dir.parent_path();
      if (fs::is_directory(parent, ec) && fs::is_empty(parent, ec))
        fs::remove(parent, ec);
      okLine("Uninstalled " + i.Name + " v" + i.V.str());
      ++removed;
    }
    if (!removed)
      okLine("every installed package is used by some project; nothing to remove");
    return 0;
  }
  Manifest m;
  if (!loadProject(projectDir, m))
    return 1;
  std::vector<Locked> lock = loadLock(m.Root);
  std::string key = projectKey(m.Root);
  fs::path manifestPath = fs::path(m.Root) / "Rune.toml";
  std::string error;
  for (const std::string &spec : args) {
    // `work::geometry` is accepted for symmetry with `add`; the registry
    // only has to agree with what the manifest says.
    std::string registry, name;
    splitQualified(spec, registry, name);
    bool had = false;
    for (const Dependency &d : m.Dependencies)
      if (d.Name == name) {
        had = true;
        if (!registry.empty() && d.Registry != registry) {
          failLine("'" + name + "' is a dependency of " + m.Name +
                   (d.Registry.empty() ? ", from no particular registry"
                                       : " from registry '" + d.Registry + "'") +
                   ", not from '" + registry + "'");
          had = false;
        }
      }
    if (!had) {
      if (registry.empty())
        failLine("'" + name + "' is not a dependency of " + m.Name);
      continue;
    }
    if (!editDependencies(manifestPath, name, "", true, error)) {
      failLine(error);
      return 1;
    }
    okLine("Removed " + name + " from Rune.toml");
  }
  // Re-resolve what is left, so a transitive dependency only the removed
  // package needed is unpinned and unreferenced too.
  Manifest after;
  if (!loadProject(m.Root, after))
    return 1;
  std::vector<Locked> keep;
  Index index;
  auto wants = registryWants(after);
  if (!wants.empty()) {
    if (!loadMergedIndex(index, /*refresh=*/false, error)) {
      failLine(error);
      return 1;
    }
    if (!resolveAndInstall(m.Root, wants, lock, {}, index, keep, verbose, error)) {
      failLine(error);
      return 1;
    }
  }
  for (const Locked &l : lock) {
    bool kept = false;
    for (const Locked &k : keep)
      if (k.Name == l.Name && k.V == l.V)
        kept = true;
    if (!kept)
      dropReference(installDir(l.Name, l.V), key);
  }
  if (!saveLock(m.Root, keep, error)) {
    failLine(error);
    return 1;
  }
  warnUnreferenced();
  return 0;
}

int commandUpdate(const std::string &projectDir, const std::vector<std::string> &args,
                  bool verbose) {
  std::string registry;
  std::vector<std::string> specs;
  if (!takeRegistryFlag(args, registry, specs))
    return 2;
  Manifest m;
  if (!loadProject(projectDir, m))
    return 1;
  auto wants = registryWants(m);
  if (wants.empty()) {
    okLine(m.Name + " has no registry dependencies");
    return 0;
  }
  Index index;
  std::string error;
  if (!loadMergedIndex(index, /*refresh=*/true, error)) {
    failLine(error);
    return 1;
  }
  std::vector<Locked> lock = loadLock(m.Root);
  std::vector<RegistrySource> sources = loadRegistries();
  // Naming packages holds everything else where it is; naming none lets
  // every dependency move to the newest version its requirement allows —
  // or, with `--registry`, every dependency that came from that registry.
  std::vector<std::string> moving;
  if (specs.empty()) {
    for (const Locked &l : lock)
      if (registry.empty() || registryNameOf(sources, l.Registry) == registry)
        moving.push_back(l.Name);
    if (moving.empty() && !registry.empty()) {
      okLine("nothing " + m.Name + " depends on came from registry '" + registry + "'");
      return 0;
    }
  } else {
    for (const std::string &spec : specs) {
      std::string from, name;
      if (!qualify(spec, registry, from, name))
        return 2;
      bool asked = false;
      for (const Want &w : wants)
        if (w.Name == name)
          asked = true;
      if (!asked) {
        failLine("'" + name + "' is not a registry dependency of " + m.Name);
        return 1;
      }
      moving.push_back(name);
    }
  }
  std::vector<Locked> resolved;
  if (!resolveAndInstall(m.Root, wants, lock, moving, index, resolved, verbose,
                         error)) {
    failLine(error);
    return 1;
  }
  std::string key = projectKey(m.Root);
  size_t moved = 0;
  for (const Locked &r : resolved) {
    const Locked *was = nullptr;
    for (const Locked &l : lock)
      if (l.Name == r.Name)
        was = &l;
    if (was && was->V == r.V)
      continue;
    ++moved;
    okLine(r.Name + ": " + (was ? "v" + was->V.str() : std::string("(new)")) +
           " -> v" + r.V.str());
  }
  for (const Locked &l : lock) {
    bool stillThere = false;
    for (const Locked &r : resolved)
      if (r.Name == l.Name && r.V == l.V)
        stillThere = true;
    if (!stillThere)
      dropReference(installDir(l.Name, l.V), key);
  }
  if (!saveLock(m.Root, resolved, error)) {
    failLine(error);
    return 1;
  }
  if (!moved)
    okLine("everything is already at the newest version its requirement allows");
  else
    warnUnreferenced();
  return 0;
}

namespace {

void printDeps(const Manifest &m, const std::string &label, const std::string &tag,
               const std::vector<Locked> &lock,
               const std::vector<RegistrySource> &sources, const std::string &prefix,
               std::set<std::string> &onPath, bool last, bool root) {
  std::string line;
  if (!root)
    line = prefix + (last ? "└─ " : "├─ ");
  line += std::string(c("\x1b[1m")) + label + c("\x1b[0m") + " v" + m.Version +
          c("\x1b[2m") + tag + c("\x1b[0m");
  plain(line);
  std::string childPrefix = root ? "" : prefix + (last ? "   " : "│  ");
  std::error_code ec;
  for (size_t i = 0; i < m.Dependencies.size(); ++i) {
    const Dependency &d = m.Dependencies[i];
    bool isLast = i + 1 == m.Dependencies.size();
    fs::path dir;
    std::string tag;
    if (!d.Path.empty()) {
      dir = fs::absolute(fs::path(m.Root) / d.Path).lexically_normal();
      tag = " (path: " + d.Path + ")";
    } else {
      std::string from = d.Registry;
      bool stale = false;
      for (const Locked &l : lock)
        if (l.Name == d.Name) {
          std::string why;
          if (!pinSatisfies(d, l, sources, why)) {
            // The manifest moved on from what the lock pinned: say so,
            // rather than draw a tree the next build will not agree with.
            plain(childPrefix + (isLast ? "└─ " : "├─ ") + d.Name + " (" +
                  (d.Version.empty() ? "*" : d.Version) + ") " + c("\x1b[33m") +
                  "(stale: " + why + "; `rune build` resolves it again)" + c("\x1b[0m"));
            stale = true;
            break;
          }
          dir = installDir(l.Name, l.V);
          if (from.empty())
            from = registryNameOf(sources, l.Registry);
        }
      if (stale)
        continue;
      tag = " (" + (d.Version.empty() ? "*" : d.Version) + ")";
      // Where it comes from, when that is a choice: a manifest that named
      // the registry, or more than one to choose from.
      if (!d.Registry.empty() || sources.size() > 1)
        tag += " [" + (from.empty() ? "?" : from) + "]";
    }
    if (dir.empty() || !fs::exists(dir / "Rune.toml", ec)) {
      plain(childPrefix + (isLast ? "└─ " : "├─ ") + d.Name + tag + " " +
            c("\x1b[2m") + "(not resolved)" + c("\x1b[0m"));
      continue;
    }
    std::string key = projectKey(dir);
    if (onPath.count(key)) {
      plain(childPrefix + (isLast ? "└─ " : "├─ ") + d.Name + tag + " (cycle)");
      continue;
    }
    Manifest child;
    std::string error;
    if (!loadManifest(dir.string(), child, error, false)) {
      plain(childPrefix + (isLast ? "└─ " : "├─ ") + d.Name + tag + " (unreadable)");
      continue;
    }
    onPath.insert(key);
    printDeps(child, d.Name, tag, lock, sources, childPrefix, onPath, isLast, false);
    onPath.erase(key);
  }
}

} // namespace

int commandDeps(const std::string &projectDir, const std::vector<std::string> &args,
                bool verbose) {
  std::string registry;
  std::vector<std::string> rest;
  if (!takeRegistryFlag(args, registry, rest))
    return 2;
  if (!rest.empty()) {
    failLine("unknown argument '" + rest.front() + "'");
    note("`rune deps [--registry <name>]` takes nothing else");
    return 2;
  }
  Manifest m;
  if (!loadProject(projectDir, m))
    return 1;
  std::vector<Locked> lock = loadLock(m.Root);
  std::vector<RegistrySource> sources = loadRegistries();
  if (!registry.empty()) {
    // A flat list rather than the tree: every pinned package that came
    // from the registry, which is the question `--registry` asks.
    size_t shown = 0;
    for (const Locked &l : lock) {
      if (registryNameOf(sources, l.Registry) != registry)
        continue;
      plain(std::string(c("\x1b[1m")) + l.Name + c("\x1b[0m") + " v" + l.V.str());
      ++shown;
    }
    if (!shown)
      okLine("nothing " + m.Name + " depends on came from registry '" + registry + "'");
    return 0;
  }
  std::set<std::string> onPath{projectKey(m.Root)};
  printDeps(m, m.Name, "", lock, sources, "", onPath, true, true);
  return 0;
}

} // namespace rune::pm
