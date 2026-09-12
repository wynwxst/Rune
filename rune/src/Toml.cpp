#include "Toml.h"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace rune {

const TomlValue *TomlValue::get(const std::string &dottedPath) const {
  const TomlValue *cur = this;
  size_t start = 0;
  while (start <= dottedPath.size()) {
    size_t dot = dottedPath.find('.', start);
    std::string key = dottedPath.substr(
        start, dot == std::string::npos ? std::string::npos : dot - start);
    cur = cur->find(key);
    if (!cur)
      return nullptr;
    if (dot == std::string::npos)
      return cur;
    start = dot + 1;
  }
  return cur;
}

namespace {

struct Parser {
  const std::string &Text;
  size_t Pos = 0;
  unsigned Line = 1;
  std::string Error;
  unsigned ErrorLine = 0;

  explicit Parser(const std::string &t) : Text(t) {}

  void fail(const std::string &msg) {
    if (Error.empty()) {
      Error = msg;
      ErrorLine = Line;
    }
  }

  bool eof() const { return Pos >= Text.size(); }
  char peek() const { return eof() ? '\0' : Text[Pos]; }
  char advance() {
    char c = Text[Pos++];
    if (c == '\n')
      ++Line;
    return c;
  }

  void skipSpaceAndComments(bool acrossLines) {
    for (;;) {
      while (!eof() && (peek() == ' ' || peek() == '\t' || peek() == '\r'))
        advance();
      if (!eof() && peek() == '#') {
        while (!eof() && peek() != '\n')
          advance();
      }
      if (acrossLines && !eof() && peek() == '\n') {
        advance();
        continue;
      }
      return;
    }
  }

  std::string parseBareKey() {
    std::string s;
    while (!eof()) {
      char c = peek();
      if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')
        s.push_back(advance());
      else
        break;
    }
    return s;
  }

  std::string parseQuoted() {
    char quote = advance(); // " or '
    std::string s;
    while (!eof() && peek() != quote) {
      char c = advance();
      if (c == '\\' && quote == '"' && !eof()) {
        char e = advance();
        switch (e) {
        case 'n': s.push_back('\n'); break;
        case 't': s.push_back('\t'); break;
        case 'r': s.push_back('\r'); break;
        case '\\': s.push_back('\\'); break;
        case '"': s.push_back('"'); break;
        default: s.push_back(e); break;
        }
        continue;
      }
      s.push_back(c);
    }
    if (eof())
      fail("unterminated string");
    else
      advance(); // closing quote
    return s;
  }

  std::string parseKey() {
    if (peek() == '"' || peek() == '\'')
      return parseQuoted();
    std::string k = parseBareKey();
    if (k.empty())
      fail("expected a key");
    return k;
  }

  /// Dotted key path, e.g. `a.b.c`.
  std::vector<std::string> parseKeyPath() {
    std::vector<std::string> path;
    for (;;) {
      skipSpaceAndComments(false);
      path.push_back(parseKey());
      skipSpaceAndComments(false);
      if (peek() == '.') {
        advance();
        continue;
      }
      return path;
    }
  }

  TomlValue parseValue() {
    skipSpaceAndComments(false);
    if (eof()) {
      fail("expected a value");
      return {};
    }
    char c = peek();
    TomlValue v;

    if (c == '"' || c == '\'') {
      v.K = TomlValue::Kind::String;
      v.Str = parseQuoted();
      return v;
    }
    if (c == '[') {
      advance();
      v.K = TomlValue::Kind::Array;
      for (;;) {
        skipSpaceAndComments(true);
        if (eof()) {
          fail("unterminated array");
          return v;
        }
        if (peek() == ']') {
          advance();
          return v;
        }
        v.Arr.push_back(parseValue());
        skipSpaceAndComments(true);
        if (peek() == ',') {
          advance();
          continue;
        }
        skipSpaceAndComments(true);
        if (peek() == ']') {
          advance();
          return v;
        }
        fail("expected ',' or ']' in array");
        return v;
      }
    }
    if (c == '{') {
      advance();
      v.K = TomlValue::Kind::Table;
      for (;;) {
        skipSpaceAndComments(true);
        if (eof()) {
          fail("unterminated inline table");
          return v;
        }
        if (peek() == '}') {
          advance();
          return v;
        }
        std::string key = parseKey();
        skipSpaceAndComments(false);
        if (peek() != '=') {
          fail("expected '=' in inline table");
          return v;
        }
        advance();
        v.Tbl[key] = parseValue();
        skipSpaceAndComments(false);
        if (peek() == ',') {
          advance();
          continue;
        }
        if (peek() == '}') {
          advance();
          return v;
        }
        fail("expected ',' or '}' in inline table");
        return v;
      }
    }

    // Bare scalar: true/false, integer or float.
    std::string token;
    while (!eof()) {
      char d = peek();
      if (d == ',' || d == ']' || d == '}' || d == '\n' || d == '#')
        break;
      token.push_back(advance());
    }
    while (!token.empty() && (token.back() == ' ' || token.back() == '\t' ||
                              token.back() == '\r'))
      token.pop_back();

    if (token == "true" || token == "false") {
      v.K = TomlValue::Kind::Boolean;
      v.Bool = token == "true";
      return v;
    }
    if (token.find('.') != std::string::npos ||
        token.find('e') != std::string::npos ||
        token.find('E') != std::string::npos) {
      char *end = nullptr;
      double d = strtod(token.c_str(), &end);
      if (end && *end == '\0') {
        v.K = TomlValue::Kind::Float;
        v.Flt = d;
        return v;
      }
    }
    {
      char *end = nullptr;
      long long n = strtoll(token.c_str(), &end, 10);
      if (end && *end == '\0' && !token.empty()) {
        v.K = TomlValue::Kind::Integer;
        v.Int = n;
        return v;
      }
    }
    // Anything else is treated as an unquoted string rather than rejected, so
    // a manifest with a stray bare word still loads.
    v.K = TomlValue::Kind::String;
    v.Str = token;
    return v;
  }

  /// Walks (creating as needed) to the table named by `path`.
  TomlValue *descend(TomlValue &root, const std::vector<std::string> &path,
                     bool arrayOfTables) {
    TomlValue *cur = &root;
    for (size_t i = 0; i < path.size(); ++i) {
      bool last = i + 1 == path.size();
      auto &slot = cur->Tbl[path[i]];
      if (last && arrayOfTables) {
        if (slot.K != TomlValue::Kind::Array) {
          slot.K = TomlValue::Kind::Array;
          slot.Arr.clear();
        }
        TomlValue entry;
        entry.K = TomlValue::Kind::Table;
        slot.Arr.push_back(std::move(entry));
        return &slot.Arr.back();
      }
      if (slot.K == TomlValue::Kind::Array && !slot.Arr.empty()) {
        // Continue into the most recent [[table]] entry.
        cur = &slot.Arr.back();
        continue;
      }
      if (slot.K != TomlValue::Kind::Table) {
        slot.K = TomlValue::Kind::Table;
        slot.Tbl.clear();
      }
      cur = &slot;
    }
    return cur;
  }

  TomlDocument run() {
    TomlDocument doc;
    doc.Root.K = TomlValue::Kind::Table;
    TomlValue *current = &doc.Root;

    while (!eof() && Error.empty()) {
      skipSpaceAndComments(false);
      if (eof())
        break;
      if (peek() == '\n') {
        advance();
        continue;
      }
      if (peek() == '[') {
        advance();
        bool arrayOfTables = false;
        if (peek() == '[') {
          advance();
          arrayOfTables = true;
        }
        std::vector<std::string> path = parseKeyPath();
        skipSpaceAndComments(false);
        if (peek() == ']')
          advance();
        else
          fail("expected ']' after a table header");
        if (arrayOfTables) {
          if (peek() == ']')
            advance();
          else
            fail("expected ']]' after an array-of-tables header");
        }
        current = descend(doc.Root, path, arrayOfTables);
        continue;
      }

      std::vector<std::string> path = parseKeyPath();
      skipSpaceAndComments(false);
      if (peek() != '=') {
        fail("expected '=' after a key");
        break;
      }
      advance();
      TomlValue value = parseValue();
      TomlValue *target = current;
      for (size_t i = 0; i + 1 < path.size(); ++i) {
        auto &slot = target->Tbl[path[i]];
        if (slot.K != TomlValue::Kind::Table) {
          slot.K = TomlValue::Kind::Table;
          slot.Tbl.clear();
        }
        target = &slot;
      }
      if (!path.empty())
        target->Tbl[path.back()] = std::move(value);
    }

    doc.Error = Error;
    doc.ErrorLine = ErrorLine;
    return doc;
  }
};

} // namespace

TomlDocument parseToml(const std::string &text) {
  Parser p(text);
  return p.run();
}

} // namespace rune
