//===- Toml.h - A small TOML reader for Rune.toml --------------*- C++ -*-===//
//
// Covers the subset a manifest needs: comments, tables, arrays of tables,
// strings, integers, floats, booleans, arrays and inline tables. Anything
// outside that is reported with a line number rather than silently ignored.
//
//===----------------------------------------------------------------------===//
#ifndef RUNE_TOML_H
#define RUNE_TOML_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace rune {

struct TomlValue {
  enum class Kind { String, Integer, Float, Boolean, Array, Table };

  Kind K = Kind::String;
  std::string Str;
  int64_t Int = 0;
  double Flt = 0;
  bool Bool = false;
  std::vector<TomlValue> Arr;
  std::map<std::string, TomlValue> Tbl;

  bool isString() const { return K == Kind::String; }
  bool isTable() const { return K == Kind::Table; }
  bool isArray() const { return K == Kind::Array; }

  /// Table lookup; returns null when absent or when this is not a table.
  const TomlValue *find(const std::string &key) const {
    if (K != Kind::Table)
      return nullptr;
    auto it = Tbl.find(key);
    return it == Tbl.end() ? nullptr : &it->second;
  }
  /// Dotted lookup: `get("build.safety")`.
  const TomlValue *get(const std::string &dottedPath) const;

  std::string stringOr(const std::string &fallback) const {
    return K == Kind::String ? Str : fallback;
  }
  int64_t intOr(int64_t fallback) const {
    return K == Kind::Integer ? Int : fallback;
  }
  bool boolOr(bool fallback) const { return K == Kind::Boolean ? Bool : fallback; }
};

struct TomlDocument {
  TomlValue Root;
  std::string Error;   ///< empty when parsing succeeded
  unsigned ErrorLine = 0;

  bool ok() const { return Error.empty(); }
  const TomlValue *get(const std::string &dottedPath) const {
    return Root.get(dottedPath);
  }
};

TomlDocument parseToml(const std::string &text);

} // namespace rune

#endif
