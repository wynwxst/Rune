//===- Cargo.cpp - Rust crates as Rune dependencies -------------*- C++ -*-===//
//
// See Cargo.h. The reading of Rust here is deliberately shallow: tokens,
// attributes, and the shape of a handful of item kinds. It does not resolve
// `use`, expand macros or evaluate `cfg`, and it does not need to — an item
// exported over the C ABI has to be written out in full, with C-compatible
// types, which is exactly what a token-level reading can follow.
//
//===----------------------------------------------------------------------===//
#include "Cargo.h"

#include "Toml.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace rune::pm {

namespace {

//===--- Tokens --------------------------------------------------------------//

enum class TK { Ident, Punct, Str, Num, Lifetime, Doc, End };

struct RTok {
  TK K = TK::End;
  std::string Text;
  bool is(const char *s) const {
    return (K == TK::Ident || K == TK::Punct) && Text == s;
  }
};

/// Rust source as tokens. Comments are dropped except `///` doc lines;
/// strings keep their contents unescaped only as far as the literals a
/// constant may hold need.
std::vector<RTok> lexRust(const std::string &s) {
  std::vector<RTok> out;
  size_t i = 0, n = s.size();
  auto identStart = [](char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_' ||
           static_cast<unsigned char>(c) >= 0x80;
  };
  auto identChar = [&](char c) {
    return identStart(c) || std::isdigit(static_cast<unsigned char>(c));
  };
  while (i < n) {
    char c = s[i];
    if (std::isspace(static_cast<unsigned char>(c))) {
      ++i;
      continue;
    }
    if (c == '/' && i + 1 < n && s[i + 1] == '/') {
      size_t end = s.find('\n', i);
      if (end == std::string::npos)
        end = n;
      // `///` is an outer doc comment; `////` is an ordinary one.
      if (i + 2 < n && s[i + 2] == '/' && !(i + 3 < n && s[i + 3] == '/')) {
        std::string text = s.substr(i + 3, end - i - 3);
        if (!text.empty() && text[0] == ' ')
          text.erase(0, 1);
        while (!text.empty() && (text.back() == '\r' || text.back() == ' '))
          text.pop_back();
        out.push_back({TK::Doc, text});
      }
      i = end;
      continue;
    }
    if (c == '/' && i + 1 < n && s[i + 1] == '*') {
      int depth = 0;
      while (i < n) {
        if (s[i] == '/' && i + 1 < n && s[i + 1] == '*') {
          ++depth;
          i += 2;
        } else if (s[i] == '*' && i + 1 < n && s[i + 1] == '/') {
          i += 2;
          if (--depth == 0)
            break;
        } else {
          ++i;
        }
      }
      continue;
    }
    // Raw strings: r"..", r#".."#, br"..".
    {
      size_t j = i;
      if (s[j] == 'b' && j + 1 < n && s[j + 1] == 'r')
        ++j;
      if (s[j] == 'r' && j + 1 < n && (s[j + 1] == '"' || s[j + 1] == '#')) {
        size_t k = j + 1, hashes = 0;
        while (k < n && s[k] == '#') {
          ++hashes;
          ++k;
        }
        if (k < n && s[k] == '"') {
          std::string close = "\"" + std::string(hashes, '#');
          size_t end = s.find(close, k + 1);
          if (end == std::string::npos)
            end = n;
          out.push_back({TK::Str, s.substr(k + 1, end - k - 1)});
          i = std::min(n, end + close.size());
          continue;
        }
      }
    }
    if (c == '"' || (c == 'b' && i + 1 < n && s[i + 1] == '"')) {
      size_t k = c == 'b' ? i + 2 : i + 1;
      std::string text;
      while (k < n && s[k] != '"') {
        if (s[k] == '\\' && k + 1 < n) {
          text += s[k];
          text += s[k + 1];
          k += 2;
          continue;
        }
        text += s[k++];
      }
      out.push_back({TK::Str, text});
      i = k + 1;
      continue;
    }
    if (c == '\'') {
      // A character literal, or a lifetime: `'a'` against `'a`. What
      // decides is whether a `'` closes the run of identifier characters —
      // judged by the run, not by the next byte, which for `'α'` is the
      // second byte of one UTF-8 character.
      if (i + 1 < n && identStart(s[i + 1])) {
        size_t k = i + 1;
        while (k < n && identChar(s[k]))
          ++k;
        if (k >= n || s[k] != '\'') {
          out.push_back({TK::Lifetime, s.substr(i, k - i)});
          i = k;
          continue;
        }
      }
      size_t k = i + 1;
      while (k < n && s[k] != '\'') {
        if (s[k] == '\\')
          ++k;
        ++k;
      }
      out.push_back({TK::Num, s.substr(i, k + 1 - i)});
      i = k + 1;
      continue;
    }
    if (identStart(c)) {
      size_t k = i;
      if (c == 'r' && i + 1 < n && s[i + 1] == '#' && i + 2 < n &&
          identStart(s[i + 2]))
        k += 2, i += 2; // r#ident
      while (k < n && identChar(s[k]))
        ++k;
      out.push_back({TK::Ident, s.substr(i, k - i)});
      i = k;
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(c))) {
      size_t k = i;
      while (k < n && (identChar(s[k]) || s[k] == '.' ||
                       ((s[k] == '+' || s[k] == '-') &&
                        (s[k - 1] == 'e' || s[k - 1] == 'E') &&
                        !(s[i] == '0' && i + 1 < n &&
                          (s[i + 1] == 'x' || s[i + 1] == 'X'))))) {
        // `1..2` is a range, not a number.
        if (s[k] == '.' && k + 1 < n && s[k + 1] == '.')
          break;
        // `x.0.method()` — a dot followed by a letter ends the number.
        if (s[k] == '.' && k + 1 < n && identStart(s[k + 1]))
          break;
        ++k;
      }
      out.push_back({TK::Num, s.substr(i, k - i)});
      i = k;
      continue;
    }
    static const char *const multi[] = {"::", "->", "=>", "==", "!=", "<=",
                                        ">=", "&&", "||", ".."};
    bool matched = false;
    for (const char *m : multi)
      if (s.compare(i, 2, m) == 0) {
        out.push_back({TK::Punct, m});
        i += 2;
        matched = true;
        break;
      }
    if (matched)
      continue;
    out.push_back({TK::Punct, std::string(1, c)});
    ++i;
  }
  // Several ends, so that looking a few tokens ahead from the last real one
  // — `t[j + 2]` after a qualifier — never leaves the vector.
  for (int pad = 0; pad < 8; ++pad)
    out.push_back({TK::End, ""});
  return out;
}

/// The index just past the group opened at `i`, whose closing bracket is the
/// partner of `toks[i]`.
size_t skipGroup(const std::vector<RTok> &toks, size_t i) {
  const std::string open = toks[i].Text;
  const char *close = open == "(" ? ")" : open == "[" ? "]" : "}";
  int depth = 0;
  for (; toks[i].K != TK::End; ++i) {
    if (toks[i].K != TK::Punct)
      continue;
    if (toks[i].Text == open)
      ++depth;
    else if (toks[i].Text == close && --depth == 0)
      return i + 1;
  }
  return i;
}

/// Splits [b, e) at commas that are not inside brackets of any kind.
std::vector<std::pair<size_t, size_t>>
splitTop(const std::vector<RTok> &toks, size_t b, size_t e) {
  std::vector<std::pair<size_t, size_t>> parts;
  int depth = 0;
  size_t start = b;
  for (size_t i = b; i < e; ++i) {
    const RTok &t = toks[i];
    if (t.K != TK::Punct)
      continue;
    if (t.Text == "(" || t.Text == "[" || t.Text == "{" || t.Text == "<")
      ++depth;
    else if (t.Text == ")" || t.Text == "]" || t.Text == "}" || t.Text == ">")
      --depth;
    else if (t.Text == "," && depth == 0) {
      if (i > start)
        parts.push_back({start, i});
      start = i + 1;
    }
  }
  if (e > start)
    parts.push_back({start, e});
  return parts;
}

//===--- What the crate declares --------------------------------------------//

struct Attr {
  std::vector<RTok> Toks; ///< what is inside `#[...]`
  bool has(const char *ident) const {
    for (const RTok &t : Toks)
      if (t.K == TK::Ident && t.Text == ident)
        return true;
    return false;
  }
  /// `#[export_name = "x"]`'s string.
  std::string exportName() const {
    if (Toks.size() >= 3 && Toks[0].Text == "export_name" &&
        Toks[1].Text == "=" && Toks[2].K == TK::Str)
      return Toks[2].Text;
    return "";
  }
  /// The type a `#[repr(...)]` names, or "".
  std::string repr() const {
    if (Toks.empty() || Toks[0].Text != "repr")
      return "";
    std::string out;
    for (size_t i = 1; i < Toks.size(); ++i)
      if (Toks[i].K == TK::Ident) {
        if (Toks[i].Text == "C" && out.empty())
          out = "C";
        else if (Toks[i].Text != "align" && Toks[i].Text != "packed" &&
                 Toks[i].Text != "C")
          out = Toks[i].Text;
      }
    return out;
  }
};

using Range = std::pair<size_t, size_t>; ///< [first, last) into the tokens

struct FnItem {
  std::string Name, Symbol;
  std::vector<std::pair<std::string, Range>> Params;
  Range Ret{0, 0};
  bool Unsafe = false;
  std::vector<std::string> Docs;
  std::string File;
};

struct StructItem {
  std::string Name;
  bool TopLevel = false; ///< in the crate root, outside any `mod` or `impl`
  std::vector<std::pair<std::string, Range>> Fields;
  std::vector<std::vector<std::string>> FieldDocs;
  std::vector<std::string> Docs;
  bool Tuple = false, Generic = false;
};

struct EnumItem {
  std::string Name, Repr;
  bool TopLevel = false;
  std::vector<std::pair<std::string, std::string>> Variants; ///< name, value
  std::vector<std::string> Docs;
  bool Fieldless = true;
};

struct ConstItem {
  std::string Name;
  bool TopLevel = false;
  Range Type{0, 0}, Value{0, 0};
  std::vector<std::string> Docs;
};

struct Crate {
  std::vector<std::vector<RTok>> Files; ///< kept alive for the ranges
  std::vector<std::pair<size_t, FnItem>> Fns;
  std::vector<std::pair<size_t, StructItem>> Structs;
  std::vector<std::pair<size_t, EnumItem>> Enums;
  std::vector<std::pair<size_t, ConstItem>> Consts;
  std::vector<std::string> Notes;
};

/// Reads one file's items into `crate`. `root` says it is the crate root,
/// whose top level — outside every `mod` and `impl` — is the crate's API.
void scanFile(Crate &crate, size_t fileIndex, const std::string &label,
              bool root) {
  int depth = 0;
  const std::vector<RTok> &t = crate.Files[fileIndex];
  std::vector<Attr> attrs;
  std::vector<std::string> docs;
  // The first of the padding ends. Every jump below lands at most a token
  // past an end, which is clamped back here, so a file that ends in the
  // middle of an item — or one this reading has lost its place in — stops
  // rather than reading past its tokens.
  size_t last = 0;
  while (last < t.size() && t[last].K != TK::End)
    ++last;
  size_t i = 0;
  while ((i = std::min(i, last)) < last) {
    if (t[i].K == TK::Doc) {
      docs.push_back(t[i].Text);
      ++i;
      continue;
    }
    if (t[i].is("#") && (t[i + 1].is("[") ||
                         (t[i + 1].is("!") && t[i + 2].is("[")))) {
      const bool inner = t[i + 1].is("!");
      size_t open = inner ? i + 2 : i + 1;
      size_t end = skipGroup(t, open);
      if (!inner) {
        Attr a;
        // `#[unsafe(no_mangle)]` (edition 2024) is `no_mangle` too.
        for (size_t k = open + 1; k + 1 < end; ++k)
          a.Toks.push_back(t[k]);
        if (!a.Toks.empty() && a.Toks[0].Text == "unsafe" &&
            a.Toks.size() >= 3 && a.Toks[1].Text == "(")
          a.Toks = std::vector<RTok>(a.Toks.begin() + 2, a.Toks.end() - 1);
        attrs.push_back(std::move(a));
      }
      i = end;
      continue;
    }

    auto anyAttr = [&](auto pred) {
      return std::any_of(attrs.begin(), attrs.end(), pred);
    };
    const bool testOnly = anyAttr([](const Attr &a) {
      return !a.Toks.empty() && a.Toks[0].Text == "cfg" && a.has("test");
    });

    // Qualifiers before an item.
    size_t j = i;
    bool isExtern = false, isUnsafe = false;
    std::string abi;
    for (;;) {
      if (t[j].is("pub")) {
        ++j;
        if (t[j].is("("))
          j = skipGroup(t, j);
      } else if (t[j].is("unsafe") || t[j].is("async") ||
                 t[j].is("default")) {
        isUnsafe = isUnsafe || t[j].is("unsafe");
        ++j;
      } else if (t[j].is("const") &&
                 (t[j + 1].is("fn") || t[j + 1].is("unsafe") ||
                  t[j + 1].is("extern"))) {
        ++j;
      } else if (t[j].is("extern") && !t[j + 1].is("crate")) {
        isExtern = true;
        ++j;
        abi = "C";
        if (t[j].K == TK::Str) {
          abi = t[j].Text;
          ++j;
        }
      } else {
        break;
      }
    }

    // `extern "C" { ... }`: functions the crate imports, not ones it exports.
    if (isExtern && t[j].is("{")) {
      i = skipGroup(t, j);
      attrs.clear();
      docs.clear();
      continue;
    }

    if (t[j].is("mod") && testOnly) {
      size_t k = j + 1;
      while (t[k].K != TK::End && !t[k].is("{") && !t[k].is(";"))
        ++k;
      i = t[k].is("{") ? skipGroup(t, k) : k + 1;
      attrs.clear();
      docs.clear();
      continue;
    }

    if (t[j].is("fn") && t[j + 1].K == TK::Ident) {
      FnItem f;
      f.Name = t[j + 1].Text;
      f.Unsafe = isUnsafe;
      f.Docs = docs;
      f.File = label;
      std::string exported;
      bool noMangle = false;
      for (const Attr &a : attrs) {
        if (!a.Toks.empty() && a.Toks[0].Text == "no_mangle")
          noMangle = true;
        if (!a.exportName().empty())
          exported = a.exportName();
      }
      size_t k = j + 2;
      bool generic = t[k].is("<");
      if (generic)
        while (t[k].K != TK::End && !t[k].is("("))
          ++k;
      size_t paramsEnd = t[k].is("(") ? skipGroup(t, k) : k;
      size_t after = paramsEnd;
      if (t[after].is("->")) {
        size_t r = after + 1;
        int depth = 0;
        while (t[r].K != TK::End) {
          if (t[r].is("(") || t[r].is("[") || t[r].is("<"))
            ++depth;
          else if (t[r].is(")") || t[r].is("]") || t[r].is(">"))
            --depth;
          if (depth == 0 && (t[r].is("{") || t[r].is(";") || t[r].is("where")))
            break;
          ++r;
        }
        f.Ret = {after + 1, r};
        after = r;
      }
      while (t[after].K != TK::End && !t[after].is("{") && !t[after].is(";"))
        ++after;
      i = t[after].is("{") ? skipGroup(t, after) : after + 1;

      const bool exportedOverC =
          (noMangle || !exported.empty()) && isExtern && !testOnly;
      if (exportedOverC) {
        f.Symbol = exported.empty() ? f.Name : exported;
        if (abi != "C" && abi != "C-unwind" && abi != "system")
          crate.Notes.push_back(f.Name + ": the \"" + abi +
                                "\" ABI is not one Rune calls");
        else if (generic)
          crate.Notes.push_back(f.Name + ": a generic function has no single "
                                         "symbol to call");
        else {
          bool ok = true;
          for (auto [pb, pe] : splitTop(t, k + 1, paramsEnd - 1)) {
            size_t colon = pb;
            while (colon < pe && !t[colon].is(":"))
              ++colon;
            if (colon == pe) { // `self`, `&self`
              ok = false;
              break;
            }
            std::string name;
            for (size_t q = pb; q < colon; ++q)
              if (t[q].K == TK::Ident && t[q].Text != "mut")
                name = t[q].Text;
            f.Params.push_back({name, {colon + 1, pe}});
          }
          if (ok)
            crate.Fns.push_back({fileIndex, std::move(f)});
          else
            crate.Notes.push_back(f.Name + ": a method takes `self`, which "
                                           "has no C type");
        }
      }
      attrs.clear();
      docs.clear();
      continue;
    }

    const bool reprC = anyAttr([](const Attr &a) { return !a.repr().empty(); });
    const bool topLevel = root && depth == 0;

    if ((t[j].is("struct") || t[j].is("union")) && t[j + 1].K == TK::Ident) {
      StructItem s;
      s.Name = t[j + 1].Text;
      s.Docs = docs;
      s.TopLevel = topLevel;
      // Only `repr(C)` lays a struct out the way C does. `transparent` is
      // its one field's layout, and `packed` or `align` change what Rune
      // would compute.
      std::string layoutNote;
      for (const Attr &a : attrs) {
        if (a.repr() == "transparent")
          layoutNote = "`#[repr(transparent)]` is its field's layout, not a struct's";
        else if (!a.repr().empty() && (a.has("packed") || a.has("align")))
          layoutNote = "`packed` and `align` change a layout Rune would compute";
      }
      size_t k = j + 2;
      if (t[k].is("<")) {
        s.Generic = true;
        while (t[k].K != TK::End && !t[k].is("{") && !t[k].is("(") &&
               !t[k].is(";"))
          ++k;
      }
      size_t end = k + 1;
      if (t[k].is("{")) {
        end = skipGroup(t, k);
        for (auto [fb, fe] : splitTop(t, k + 1, end - 1)) {
          // Docs, attributes and visibility come first.
          size_t q = fb;
          std::vector<std::string> fieldDocs;
          while (q < fe && (t[q].is("#") || t[q].K == TK::Doc)) {
            if (t[q].K == TK::Doc)
              fieldDocs.push_back(t[q++].Text);
            else
              q = skipGroup(t, q + 1);
          }
          if (t[q].is("pub")) {
            ++q;
            if (t[q].is("("))
              q = skipGroup(t, q);
          }
          if (q + 1 < fe && t[q].K == TK::Ident && t[q + 1].is(":")) {
            s.Fields.push_back({t[q].Text, {q + 2, fe}});
            s.FieldDocs.push_back(std::move(fieldDocs));
          }
        }
      } else if (t[k].is("(")) {
        s.Tuple = true;
        end = skipGroup(t, k);
        while (t[end].K != TK::End && !t[end].is(";"))
          ++end;
        ++end;
      }
      i = end;
      if (t[j].is("union") && reprC)
        crate.Notes.push_back(s.Name + ": a union is not bound; reach it "
                                       "through a pointer");
      else if (reprC && !testOnly && !layoutNote.empty() && s.TopLevel)
        crate.Notes.push_back(s.Name + ": " + layoutNote);
      else if (reprC && !testOnly && layoutNote.empty())
        crate.Structs.push_back({fileIndex, std::move(s)});
      attrs.clear();
      docs.clear();
      continue;
    }

    if (t[j].is("enum") && t[j + 1].K == TK::Ident) {
      EnumItem e;
      e.Name = t[j + 1].Text;
      e.Docs = docs;
      e.TopLevel = topLevel;
      for (const Attr &a : attrs)
        if (!a.repr().empty())
          e.Repr = a.repr();
      size_t k = j + 2;
      while (t[k].K != TK::End && !t[k].is("{"))
        ++k;
      size_t end = skipGroup(t, k);
      long long next = 0;
      for (auto [vb, ve] : splitTop(t, k + 1, end - 1)) {
        size_t q = vb;
        while (q < ve && (t[q].is("#") || t[q].K == TK::Doc))
          q = t[q].K == TK::Doc ? q + 1 : skipGroup(t, q + 1);
        if (q >= ve || t[q].K != TK::Ident)
          continue;
        std::string name = t[q].Text;
        if (q + 1 < ve && (t[q + 1].is("(") || t[q + 1].is("{")))
          e.Fieldless = false;
        std::string value = std::to_string(next);
        if (q + 1 < ve && t[q + 1].is("=")) {
          std::string lit;
          bool neg = false;
          size_t v = q + 2;
          if (t[v].is("-")) {
            neg = true;
            ++v;
          }
          if (v < ve && t[v].K == TK::Num && v + 1 == ve) {
            lit = t[v].Text;
            lit.erase(std::remove(lit.begin(), lit.end(), '_'), lit.end());
            try {
              long long parsed = std::stoll(lit, nullptr, 0);
              next = neg ? -parsed : parsed;
              value = std::to_string(next);
            } catch (...) {
              e.Fieldless = false;
            }
          } else {
            e.Fieldless = false; // a value only Rust can work out
          }
        }
        e.Variants.push_back({name, value});
        ++next;
      }
      i = end;
      if (!e.Repr.empty() && !testOnly) {
        if (e.Fieldless)
          crate.Enums.push_back({fileIndex, std::move(e)});
        else
          crate.Notes.push_back(e.Name + ": an enum with data, or a "
                                         "discriminant that is not a literal, "
                                         "is not bound");
      }
      attrs.clear();
      docs.clear();
      continue;
    }

    if (t[j].is("const") && t[j + 1].K == TK::Ident && t[j + 2].is(":") &&
        t[i].is("pub")) {
      ConstItem c;
      c.Name = t[j + 1].Text;
      c.Docs = docs;
      c.TopLevel = topLevel;
      size_t k = j + 3;
      while (t[k].K != TK::End && !t[k].is("="))
        ++k;
      c.Type = {j + 3, k};
      size_t v = k + 1;
      while (t[v].K != TK::End && !t[v].is(";"))
        ++v;
      c.Value = {k + 1, v};
      i = v + 1;
      if (!testOnly)
        crate.Consts.push_back({fileIndex, std::move(c)});
      attrs.clear();
      docs.clear();
      continue;
    }

    if (t[j].is("static") && !attrs.empty() &&
        anyAttr([](const Attr &a) {
          return !a.Toks.empty() && a.Toks[0].Text == "no_mangle";
        }))
      crate.Notes.push_back(t[j + 1].Text + ": an exported static is not "
                                            "bound; export a function that "
                                            "returns it");

    // Anything else: the attributes and docs gathered were not ours. The
    // braces walked through rather than skipped are a `mod`'s or an
    // `impl`'s, and they are what decides the top level.
    attrs.clear();
    docs.clear();
    if (i == j) {
      if (t[i].is("{"))
        ++depth;
      else if (t[i].is("}") && depth > 0)
        --depth;
      ++i;
    } else {
      i = j;
    }
  }
}

//===--- Types ---------------------------------------------------------------//

const std::set<std::string> &runeKeywords() {
  static const std::set<std::string> words = {
      "fn", "var", "let", "mut", "global", "class", "struct", "enum", "mark",
      "bind", "to", "extend", "super", "self", "Self", "import", "pub", "as",
      "into", "is", "if", "elif", "else", "while", "loop", "for", "in",
      "match", "return", "break", "continue", "extern", "where", "defer",
      "unsafe", "operator", "dyn", "type", "weak", "uniq", "move", "macro",
      "async", "await", "true", "false", "nil"};
  return words;
}

/// What a Rust type is on the Rune side.
struct TypeMapper {
  const std::vector<RTok> &T;
  const std::set<std::string> &Structs;
  const std::map<std::string, std::string> &Enums; ///< name -> repr
  std::set<std::string> &Opaque;
  bool Windows;
  /// Every struct, enum and opaque type the mapped types named, when set:
  /// what has to be declared for them to mean anything.
  std::set<std::string> *Used = nullptr;
  /// Set when the type carries an address — what makes a call one the
  /// compiler cannot vouch for.
  bool SawPointer = false;

  std::string primitive(const std::string &n) const {
    static const std::map<std::string, std::string> prims = {
        {"i8", "i8"},       {"i16", "i16"},     {"i32", "i32"},
        {"i64", "i64"},     {"u8", "u8"},       {"u16", "u16"},
        {"u32", "u32"},     {"u64", "u64"},     {"isize", "isize"},
        {"usize", "usize"}, {"f32", "f32"},     {"f64", "f64"},
        {"bool", "bool"},   {"char", "u32"},    {"c_char", "i8"},
        {"c_schar", "i8"},  {"c_uchar", "u8"},  {"c_short", "i16"},
        {"c_ushort", "u16"}, {"c_int", "i32"},  {"c_uint", "u32"},
        {"c_longlong", "i64"}, {"c_ulonglong", "u64"}, {"c_float", "f32"},
        {"c_double", "f64"}, {"size_t", "usize"}, {"ssize_t", "isize"},
        {"c_size_t", "usize"}, {"c_ssize_t", "isize"}, {"intptr_t", "isize"},
        {"uintptr_t", "usize"}};
    auto it = prims.find(n);
    if (it != prims.end())
      return it->second;
    if (n == "c_long")
      return Windows ? "i32" : "isize";
    if (n == "c_ulong")
      return Windows ? "u32" : "usize";
    return "";
  }

  /// The type in [b, e). Empty with `why` set when it has no Rune spelling.
  /// `behindPointer`: a name nothing declares may stand for an opaque type.
  std::string map(size_t b, size_t e, std::string &why,
                  bool behindPointer = false) {
    while (b < e && T[b].K == TK::Lifetime)
      ++b;
    if (b >= e) {
      why = "an empty type";
      return "";
    }
    // `()`: no value. `!`: no return at all.
    if (T[b].is("(") && b + 1 < e && T[b + 1].is(")") && b + 2 == e)
      return "()";
    if (T[b].is("!") && b + 1 == e)
      return "Never";
    if (T[b].is("*") && b + 1 < e) {
      const bool mut = T[b + 1].is("mut");
      SawPointer = true;
      return pointer(b + 2, e, mut, why);
    }
    if (T[b].is("&")) {
      size_t k = b + 1;
      while (k < e && T[k].K == TK::Lifetime)
        ++k;
      bool mut = k < e && T[k].is("mut");
      if (mut)
        ++k;
      SawPointer = true;
      return pointer(k, e, mut, why);
    }
    if (T[b].is("[")) {
      // `[T; N]`, in a struct.
      size_t semi = b + 1;
      while (semi < e && !T[semi].is(";"))
        ++semi;
      if (semi + 2 < e && T[semi + 1].K == TK::Num && T[e - 1].is("]")) {
        std::string inner = map(b + 1, semi, why);
        if (inner.empty())
          return "";
        std::string count = T[semi + 1].Text;
        count.erase(std::remove(count.begin(), count.end(), '_'), count.end());
        return "[" + count + ":" + inner + "]";
      }
      why = "a slice has no C layout; pass a pointer and a length";
      return "";
    }
    // A function pointer: `[unsafe] [extern "C"] fn(A) -> R`.
    {
      size_t k = b;
      if (T[k].is("unsafe"))
        ++k;
      bool ext = false;
      if (T[k].is("extern")) {
        ext = true;
        ++k;
        if (T[k].K == TK::Str)
          ++k;
      }
      if (T[k].is("fn")) {
        if (!ext) {
          why = "a Rust-ABI function pointer cannot be called from Rune; "
                "make it `extern \"C\" fn`";
          return "";
        }
        return fnPointer(k + 1, e, why);
      }
    }
    // A path: `a::b::Name`, maybe with arguments.
    size_t k = b;
    std::string last;
    while (k < e) {
      if (T[k].K == TK::Ident) {
        last = T[k].Text;
        ++k;
      } else if (T[k].is("::")) {
        ++k;
      } else {
        break;
      }
    }
    if (k < e && T[k].is("<") && T[e - 1].is(">")) {
      // The wrappers that are pointers in the C ABI.
      if (last == "Option" || last == "NonNull" || last == "Box") {
        std::string inner;
        if (last == "Option") {
          // `Option<&T>`, `Option<NonNull<T>>`, `Option<extern fn>`,
          // `Option<Box<T>>`: all a pointer that may be null.
          inner = map(k + 1, e - 1, why, behindPointer);
          return inner;
        }
        SawPointer = true;
        return pointer(k + 1, e - 1, /*mut=*/true, why);
      }
      why = "'" + last + "<...>' has no C layout";
      return "";
    }
    if (k != e || last.empty()) {
      why = "a type this reader does not follow";
      return "";
    }
    std::string prim = primitive(last);
    if (!prim.empty())
      return prim;
    if (Structs.count(last) || Enums.count(last)) {
      if (Used)
        Used->insert(last);
      return last;
    }
    if (behindPointer && !runeKeywords().count(last)) {
      Opaque.insert(last);
      return last;
    }
    if (last == "String" || last == "Vec" || last == "str")
      why = "'" + last + "' has no C layout; pass a pointer and a length";
    else
      why = "'" + last + "' is not `#[repr(C)]` and is passed by value";
    return "";
  }

  std::string pointer(size_t b, size_t e, bool mut, std::string &why) {
    // `*const c_char` is a C string; `c_void` is "some bytes".
    size_t k = b;
    std::string last;
    while (k < e && (T[k].K == TK::Ident || T[k].is("::"))) {
      if (T[k].K == TK::Ident)
        last = T[k].Text;
      ++k;
    }
    // `char *` either way: the ABI is the same, and a string the crate hands
    // back to be freed reads as one on this side too.
    if (k == e && (last == "c_char" || last == "c_schar"))
      return "CString";
    if (k == e && last == "c_void")
      return mut ? "*var u8" : "*u8";
    std::string inner = map(b, e, why, /*behindPointer=*/true);
    if (inner.empty())
      return "";
    if (inner == "()")
      return mut ? "*var u8" : "*u8";
    return (mut ? "*var " : "*") + inner;
  }

  std::string fnPointer(size_t b, size_t e, std::string &why) {
    if (b >= e || !T[b].is("("))
      return "";
    size_t close = skipGroup(T, b);
    std::string out = "@cfunction(";
    bool first = true;
    for (auto [pb, pe] : splitTop(T, b + 1, close - 1)) {
      // A parameter may be named: `fn(len: usize)`.
      size_t colon = pb;
      int depth = 0;
      for (size_t q = pb; q < pe; ++q) {
        if (T[q].is("(") || T[q].is("<"))
          ++depth;
        if (T[q].is(")") || T[q].is(">"))
          --depth;
        if (depth == 0 && T[q].is(":") ) {
          colon = q + 1;
          break;
        }
      }
      std::string p = map(colon, pe, why, true);
      if (p.empty() || p == "()")
        return "";
      out += (first ? "" : ", ") + p;
      first = false;
    }
    out += ")";
    if (close < e && T[close].is("->")) {
      std::string r = map(close + 1, e, why, true);
      if (r.empty())
        return "";
      if (r != "()")
        out += " -> " + r;
    }
    return out;
  }
};

/// A Rune name for a Rust one: a keyword gets a trailing underscore.
std::string runeName(const std::string &n) {
  return runeKeywords().count(n) ? n + "_" : n;
}

/// The value a field of `type` starts at, or "" when it has none worth
/// writing (a nested struct is built by whoever needs it).
std::string zeroOf(const std::string &type) {
  if (type == "bool")
    return "false";
  if (type == "f32" || type == "f64")
    return "0.0";
  if (type[0] == '*' || type[0] == '@' || type == "CString")
    return "0 as " + type;
  static const std::set<std::string> ints = {"i8", "i16", "i32", "i64", "u8",
                                             "u16", "u32", "u64", "isize",
                                             "usize"};
  if (ints.count(type))
    return "0";
  return "";
}

/// A constant's literal value, in Rune, or "".
std::string literalOf(const std::vector<RTok> &t, size_t b, size_t e,
                      const std::string &type) {
  bool neg = false;
  if (b < e && t[b].is("-")) {
    neg = true;
    ++b;
  }
  if (b + 1 != e)
    return "";
  const RTok &v = t[b];
  if (v.K == TK::Ident && (v.Text == "true" || v.Text == "false") &&
      type == "bool" && !neg)
    return v.Text;
  if (v.K == TK::Str && type == "CString") {
    // Only text that reads the same in both languages.
    for (char c : v.Text)
      if (c == '\\' || c == '{' || c == '}' || c == '"')
        return "";
    return "\"" + v.Text + "\"";
  }
  if (v.K != TK::Num || v.Text[0] == '\'')
    return "";
  std::string text = v.Text;
  text.erase(std::remove(text.begin(), text.end(), '_'), text.end());
  // Drop a type suffix: `10u32`, `1.5f64`.
  static const char *const suffixes[] = {"i8",  "i16", "i32",   "i64",
                                         "u8",  "u16", "u32",   "u64",
                                         "f32", "f64", "isize", "usize"};
  const bool hex = text.size() > 2 && text[0] == '0' &&
                   (text[1] == 'x' || text[1] == 'X');
  for (const char *s : suffixes) {
    std::string suf = s;
    if (hex && suf[0] != 'i' && suf[0] != 'u')
      continue;
    if (text.size() > suf.size() &&
        text.compare(text.size() - suf.size(), suf.size(), suf) == 0) {
      text.erase(text.size() - suf.size());
      break;
    }
  }
  if (type == "f32" || type == "f64") {
    if (text.find('.') == std::string::npos && text.find('e') == std::string::npos)
      text += ".0";
  } else if (text.find('.') != std::string::npos) {
    return "";
  }
  // Octal and binary are read here; Rune is given decimal.
  if (text.size() > 2 && text[0] == '0' &&
      (text[1] == 'o' || text[1] == 'b')) {
    try {
      text = std::to_string(std::stoull(text.substr(2), nullptr,
                                        text[1] == 'o' ? 8 : 2));
    } catch (...) {
      return "";
    }
  }
  return (neg ? "-" : "") + text;
}

void docLines(std::ostringstream &os, const std::vector<std::string> &docs,
              const std::string &indent) {
  for (const std::string &d : docs)
    os << indent << "///" << (d.empty() ? "" : " ") << d << "\n";
}

/// The crate root: `[lib] path`, or `src/lib.rs`.
fs::path crateRootFile(const std::string &crateDir) {
  std::ifstream in(fs::path(crateDir) / "Cargo.toml", std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  TomlDocument doc = parseToml(text.str());
  if (doc.ok())
    if (const TomlValue *p = doc.get("lib.path"))
      if (p->isString())
        return (fs::path(crateDir) / p->Str).lexically_normal();
  return (fs::path(crateDir) / "src" / "lib.rs").lexically_normal();
}

} // namespace

RustBindings generateRustBindings(const std::string &crateDir,
                                  const std::string &moduleName,
                                  bool windows) {
  RustBindings out;
  Crate crate;
  std::error_code ec;
  const fs::path rootFile = crateRootFile(crateDir);
  std::vector<std::string> files;
  for (auto it = fs::recursive_directory_iterator(fs::path(crateDir) / "src", ec);
       !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
    if (it->is_regular_file() && it->path().extension() == ".rs")
      files.push_back(it->path().string());
  if (fs::exists(rootFile, ec) &&
      std::find(files.begin(), files.end(), rootFile.string()) == files.end())
    files.push_back(rootFile.string());
  std::sort(files.begin(), files.end());
  for (const std::string &f : files) {
    std::ifstream in(f, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    crate.Files.push_back(lexRust(text.str()));
    scanFile(crate, crate.Files.size() - 1,
             fs::relative(f, crateDir, ec).string(),
             fs::path(f).lexically_normal() == rootFile);
  }

  std::vector<std::string> &skipped = out.Skipped;
  for (const std::string &n : crate.Notes)
    skipped.push_back(n);

  // 1. A Rune module is one namespace and a crate is many: a name two Rust
  //    modules both declare cannot be told apart here, so neither is bound.
  std::map<std::string, int> declared;
  for (const auto &[file, s] : crate.Structs)
    ++declared[s.Name];
  for (const auto &[file, e] : crate.Enums)
    ++declared[e.Name];
  std::set<std::string> ambiguous;
  for (const auto &[name, count] : declared)
    if (count > 1) {
      ambiguous.insert(name);
      skipped.push_back(name + ": declared " + std::to_string(count) +
                        " times in different modules; there is no telling "
                        "which one is meant");
    }

  // 2. Enums of a width Rune has.
  std::map<std::string, std::string> enumNames; // name -> its integer type
  std::map<std::string, const EnumItem *> enums;
  for (const auto &[file, e] : crate.Enums) {
    if (ambiguous.count(e.Name))
      continue;
    std::set<std::string> none;
    std::set<std::string> noStructs;
    std::map<std::string, std::string> noEnums;
    TypeMapper probe{crate.Files[file], noStructs, noEnums, none, windows};
    std::string mapped = probe.primitive(e.Repr == "C" ? "i32" : e.Repr);
    if (mapped.empty()) {
      skipped.push_back(e.Name + ": `#[repr(" + e.Repr + ")]` is not a width "
                                 "Rune has");
      continue;
    }
    enumNames[e.Name] = mapped;
    enums[e.Name] = &e;
  }

  // 3. Structs with a C layout. One that cannot be bound takes with it every
  //    struct holding it by value, so the set is narrowed until it settles.
  //    `struct Handle { _private: [u8; 0] }` is the idiom for "opaque".
  std::set<std::string> bindable, idiomOpaque, topLevelOpaque;
  std::map<std::string, std::pair<size_t, const StructItem *>> structs;
  for (const auto &[file, s] : crate.Structs) {
    if (ambiguous.count(s.Name))
      continue;
    if (s.Generic || s.Tuple) {
      if (s.TopLevel)
        skipped.push_back(s.Name + (s.Generic ? ": a generic struct has no "
                                                "one layout"
                                              : ": a tuple struct is not bound"));
      continue;
    }
    bool opaqueIdiom = true;
    const std::vector<RTok> &t = crate.Files[file];
    for (const auto &[name, range] : s.Fields) {
      bool zeroArray = t[range.first].is("[") && range.second >= 2 &&
                       t[range.second - 2].K == TK::Num &&
                       t[range.second - 2].Text == "0";
      if (name.empty() || name[0] != '_' || !zeroArray)
        opaqueIdiom = false;
    }
    if (opaqueIdiom) {
      idiomOpaque.insert(s.Name);
      if (s.TopLevel)
        topLevelOpaque.insert(s.Name);
      continue;
    }
    bindable.insert(s.Name);
    structs[s.Name] = {file, &s};
  }
  std::map<std::string, std::string> whyNot;
  for (bool changed = true; changed;) {
    changed = false;
    for (const auto &[name, at] : structs) {
      if (!bindable.count(name))
        continue;
      std::set<std::string> scratch;
      TypeMapper tm{crate.Files[at.first], bindable, enumNames, scratch, windows};
      for (const auto &[field, range] : at.second->Fields) {
        std::string why;
        std::string ty = tm.map(range.first, range.second, why);
        if (ty.empty() || ty == "()") {
          whyNot[name] = name + "." + field + ": " + (why.empty() ? "no value" : why);
          bindable.erase(name);
          changed = true;
          break;
        }
      }
    }
  }

  // 4. Functions. Safe Rust functions that take no addresses get a safe
  //    Rune wrapper; the rest are declared as C functions are, for `unsafe`.
  //    What they name is what has to be declared.
  std::set<std::string> used, opaqueUsed;
  std::ostringstream raw, wrappers;
  for (const auto &[file, f] : crate.Fns) {
    std::set<std::string> named, opaqueNamed;
    TypeMapper tm{crate.Files[file], bindable, enumNames, opaqueNamed, windows};
    tm.Used = &named;
    std::vector<std::pair<std::string, std::string>> params;
    std::string why;
    bool ok = true;
    size_t index = 0;
    for (const auto &[name, range] : f.Params) {
      std::string ty = tm.map(range.first, range.second, why);
      if (ty.empty() || ty == "()") {
        ok = false;
        break;
      }
      std::string n = name.empty() || name == "_" ? "arg" + std::to_string(index)
                                                  : runeName(name);
      params.push_back({n, ty});
      ++index;
    }
    std::string ret;
    if (ok && f.Ret.second > f.Ret.first) {
      bool tookPointer = tm.SawPointer;
      ret = tm.map(f.Ret.first, f.Ret.second, why);
      // A returned address is the callee's to vouch for, not the caller's.
      tm.SawPointer = tookPointer;
      if (ret.empty())
        ok = false;
      if (ret == "()")
        ret.clear();
    }
    if (!ok) {
      // A struct the function needs may have been left out for a reason of
      // its own; say that one.
      for (const auto &[name, reason] : whyNot)
        if (why.find("'" + name + "'") != std::string::npos)
          why += " (" + reason + ")";
      skipped.push_back(f.Name + ": " + why);
      continue;
    }
    const std::string &symbol = f.Symbol;
    const bool symbolWritable =
        !runeKeywords().count(symbol) &&
        std::all_of(symbol.begin(), symbol.end(), [](char c) {
          return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        });
    if (!symbolWritable) {
      skipped.push_back(f.Name + ": its symbol '" + symbol +
                        "' is not a name Rune can declare");
      continue;
    }
    used.insert(named.begin(), named.end());
    opaqueUsed.insert(opaqueNamed.begin(), opaqueNamed.end());
    std::string sig = "(";
    for (size_t i = 0; i < params.size(); ++i)
      sig += (i ? ", " : "") + params[i].first + ": " + params[i].second;
    sig += ")";
    if (!ret.empty())
      sig += " -> " + ret;
    const std::string name = runeName(f.Name);
    if (!f.Unsafe && !tm.SawPointer) {
      raw << "    #as(\"__rust_" << f.Name << "\")\n    fn " << symbol << sig
          << "\n";
      docLines(wrappers, f.Docs, "");
      wrappers << "#safe(\"a safe function in the Rust crate, taking no "
                  "addresses\")\npub fn "
               << name << sig << " {\n    __rust_" << f.Name << "(";
      for (size_t i = 0; i < params.size(); ++i)
        wrappers << (i ? ", " : "") << params[i].first;
      wrappers << ")\n}\n\n";
    } else {
      docLines(raw, f.Docs, "    ");
      if (symbol != name)
        raw << "    #as(\"" << name << "\")\n";
      raw << "    fn " << symbol << sig << "\n";
    }
    ++out.Functions;
  }

  // 5. The types to declare: what the functions name, what the crate root
  //    declares at its top level, and whatever those hold by value or point
  //    at, followed through their fields.
  for (const auto &[name, at] : structs)
    if (at.second->TopLevel && bindable.count(name))
      used.insert(name);
  for (const auto &[name, e] : enums)
    if (e->TopLevel)
      used.insert(name);
  opaqueUsed.insert(topLevelOpaque.begin(), topLevelOpaque.end());
  for (const auto &[name, reason] : whyNot)
    if (structs.at(name).second->TopLevel)
      skipped.push_back(reason);
  std::map<std::string, std::string> structBodies;
  for (std::vector<std::string> work(used.begin(), used.end()); !work.empty();) {
    const std::string name = work.back();
    work.pop_back();
    auto it = structs.find(name);
    if (it == structs.end() || !bindable.count(name) || structBodies.count(name))
      continue;
    const StructItem &s = *it->second.second;
    std::set<std::string> named;
    TypeMapper tm{crate.Files[it->second.first], bindable, enumNames, opaqueUsed,
                  windows};
    tm.Used = &named;
    std::ostringstream body;
    for (size_t f = 0; f < s.Fields.size(); ++f) {
      const auto &[field, range] = s.Fields[f];
      std::string why;
      std::string ty = tm.map(range.first, range.second, why);
      docLines(body, s.FieldDocs[f], "    ");
      std::string zero = zeroOf(ty);
      body << "    pub " << runeName(field) << ": " << ty
           << (zero.empty() ? "" : " = " + zero) << "\n";
    }
    structBodies[name] = body.str();
    for (const std::string &n : named)
      if (used.insert(n).second || !structBodies.count(n))
        work.push_back(n);
  }

  // 6. Written out: opaque handles, enums, structs, constants, functions.
  std::ostringstream os;
  os << "// Rune bindings for the Rust crate at " << crateDir << ".\n"
     << "// Written by `rune build` from what the crate exports over the C "
        "ABI —\n"
     << "// regenerate rather than edit. A function the crate marks safe and "
        "that\n"
     << "// takes no address is safe here too; the rest are called as C "
        "functions\n"
     << "// are, from `unsafe`.\n\n";
  bool anyOpaque = false;
  for (const std::string &o : opaqueUsed) {
    if (bindable.count(o) || enumNames.count(o) || ambiguous.count(o))
      continue;
    os << "/// Opaque on the Rust side: reached only through a pointer.\n"
       << "pub type " << runeName(o) << " = u8\n";
    anyOpaque = true;
  }
  if (anyOpaque)
    os << "\n";
  for (const auto &[file, e] : crate.Enums) {
    auto known = enums.find(e.Name);
    if (!used.count(e.Name) || known == enums.end() || known->second != &e)
      continue;
    docLines(os, e.Docs, "");
    os << "pub type " << runeName(e.Name) << " = " << enumNames.at(e.Name) << "\n";
    for (const auto &[v, value] : e.Variants)
      os << "pub global " << e.Name << "_" << v << ": " << runeName(e.Name)
         << " = " << value << "\n";
    os << "\n";
  }
  for (const auto &[file, s] : crate.Structs) {
    auto it = structBodies.find(s.Name);
    auto known = structs.find(s.Name);
    if (it == structBodies.end() || known == structs.end() ||
        known->second.second != &s)
      continue;
    docLines(os, s.Docs, "");
    os << "#Convention(\"C\")\npub struct " << runeName(s.Name) << " {\n"
       << it->second << "}\n\n";
  }

  // Constants: the root's own, with literal values.
  std::set<std::string> constNames;
  std::ostringstream consts;
  for (const auto &[file, c] : crate.Consts) {
    if (!c.TopLevel || !constNames.insert(c.Name).second)
      continue;
    std::set<std::string> scratch;
    TypeMapper tm{crate.Files[file], bindable, enumNames, scratch, windows};
    std::string why;
    const std::vector<RTok> &t = crate.Files[file];
    std::string ty;
    // `&str` / `&'static str` constants are C strings on this side.
    size_t b = c.Type.first;
    if (t[b].is("&")) {
      size_t k = b + 1;
      while (t[k].K == TK::Lifetime)
        ++k;
      if (t[k].is("str") && k + 1 == c.Type.second)
        ty = "CString";
    }
    if (ty.empty())
      ty = tm.map(c.Type.first, c.Type.second, why);
    if (ty.empty() || ty[0] == '*' || ty[0] == '@' || ty[0] == '[' ||
        bindable.count(ty))
      continue; // not a value Rune can be handed as a literal
    if (enumNames.count(ty) && !used.count(ty))
      continue;
    std::string value = literalOf(t, c.Value.first, c.Value.second,
                                  enumNames.count(ty) ? enumNames.at(ty) : ty);
    if (value.empty())
      continue; // computed in Rust; only Rust knows it
    docLines(consts, c.Docs, "");
    consts << "pub global " << runeName(c.Name) << ": " << ty << " = " << value
           << "\n";
  }
  if (!consts.str().empty())
    os << consts.str() << "\n";
  const std::string rawText = raw.str();
  if (!rawText.empty())
    os << "pub extern \"C\" {\n" << rawText << "}\n\n";
  os << wrappers.str();
  if (!skipped.empty()) {
    os << "// Not bound:\n";
    for (const std::string &s : skipped)
      os << "//   " << s << "\n";
  }
  (void)moduleName;
  out.Source = os.str();
  return out;
}

std::string cargoLibraryName(const std::string &crateDir, std::string &error) {
  std::ifstream in(fs::path(crateDir) / "Cargo.toml", std::ios::binary);
  if (!in) {
    error = "no Cargo.toml in " + crateDir;
    return "";
  }
  std::ostringstream text;
  text << in.rdbuf();
  TomlDocument doc = parseToml(text.str());
  if (!doc.ok()) {
    error = "Cargo.toml: " + doc.Error;
    return "";
  }
  std::string name;
  if (const TomlValue *v = doc.get("lib.name"))
    name = v->stringOr("");
  if (name.empty())
    if (const TomlValue *v = doc.get("package.name"))
      name = v->stringOr("");
  if (name.empty()) {
    error = "Cargo.toml names no package";
    return "";
  }
  std::replace(name.begin(), name.end(), '-', '_');
  return name;
}

std::string rustTargetFor(const std::string &llvmTriple) {
  std::string t = llvmTriple;
  std::string arch = t.substr(0, t.find('-'));
  if (arch == "arm64")
    arch = "aarch64";
  auto has = [&](const char *s) { return t.find(s) != std::string::npos; };
  if (has("apple-macos") || has("apple-darwin"))
    return arch + "-apple-darwin";
  if (has("apple-ios"))
    return arch + (has("simulator") ? "-apple-ios-sim" : "-apple-ios");
  if (has("mingw") || has("windows-gnu"))
    return (arch == "i386" || arch == "i586" ? std::string("i686") : arch) +
           "-pc-windows-gnu";
  if (has("windows-msvc"))
    return arch + "-pc-windows-msvc";
  if (has("wasm32-wasi") || has("wasm32-unknown-wasi"))
    return has("threads") ? "wasm32-wasip1-threads" : "wasm32-wasip1";
  if (has("linux-musl"))
    return arch + "-unknown-linux-musl";
  if (has("linux"))
    return arch + "-unknown-linux-gnu" + (has("gnueabihf") ? "" : "");
  return t;
}

std::vector<std::string> nativeStaticLibs(const std::string &rustcOutput) {
  std::vector<std::string> out;
  const std::string key = "native-static-libs:";
  size_t at = rustcOutput.find(key);
  if (at == std::string::npos)
    return out;
  size_t end = rustcOutput.find('\n', at);
  std::istringstream words(rustcOutput.substr(at + key.size(), end - at - key.size()));
  for (std::string w; words >> w;)
    out.push_back(w);
  return out;
}

} // namespace rune::pm
