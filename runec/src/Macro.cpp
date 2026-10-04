//===- Macro.cpp - Collecting and expanding declarative macros ------------===//

#include "rune/Macro.h"

#include <cctype>

#include <algorithm>

namespace rune {
namespace {

bool isOpen(Tok k) {
  return k == Tok::LParen || k == Tok::LBracket || k == Tok::LBrace;
}
bool isClose(Tok k) {
  return k == Tok::RParen || k == Tok::RBracket || k == Tok::RBrace;
}
Tok closerFor(Tok open) {
  switch (open) {
  case Tok::LParen: return Tok::RParen;
  case Tok::LBracket: return Tok::RBracket;
  default: return Tok::RBrace;
  }
}

/// The index just past the group opening at `i`, which must be an opener.
size_t skipGroup(const std::vector<Token> &t, size_t i) {
  int depth = 0;
  for (; i < t.size(); ++i) {
    if (isOpen(t[i].Kind))
      ++depth;
    else if (isClose(t[i].Kind)) {
      if (--depth == 0)
        return i + 1;
    }
  }
  return t.size();
}

/// Newlines are statement terminators in Rune, so they are real tokens. A
/// pattern should not have to spell them, and a body should not inherit the
/// ones that happened to be in the definition's layout.
bool isLayout(const Token &t) { return t.Kind == Tok::Newline; }

std::vector<Token> withoutLayout(const std::vector<Token> &in) {
  std::vector<Token> out;
  out.reserve(in.size());
  for (const Token &t : in)
    if (!isLayout(t))
      out.push_back(t);
  return out;
}

const char *fragmentName(FragmentKind k) {
  switch (k) {
  case FragmentKind::Expr: return "expr";
  case FragmentKind::Ident: return "ident";
  case FragmentKind::Type: return "ty";
  case FragmentKind::Literal: return "literal";
  case FragmentKind::Block: return "block";
  case FragmentKind::Tokens: return "tt";
  }
  return "?";
}

bool fragmentFromName(const std::string &s, FragmentKind &out) {
  if (s == "expr") { out = FragmentKind::Expr; return true; }
  if (s == "ident") { out = FragmentKind::Ident; return true; }
  if (s == "ty" || s == "type") { out = FragmentKind::Type; return true; }
  if (s == "literal") { out = FragmentKind::Literal; return true; }
  if (s == "block") { out = FragmentKind::Block; return true; }
  if (s == "tt") { out = FragmentKind::Tokens; return true; }
  return false;
}

bool isLiteralToken(Tok k) {
  return k == Tok::IntLiteral || k == Tok::FloatLiteral ||
         k == Tok::StringLiteral || k == Tok::CharLiteral ||
         k == Tok::KwTrue || k == Tok::KwFalse || k == Tok::KwNil;
}

/// True when an expansion is one expression rather than a run of statements —
/// no statement separator at the top level of it.
///
/// This decides how the expansion is spliced. One expression goes in wrapped
/// in parentheses, so it composes like the single value it is: without that,
/// `twice!(3) + 1` would expand to `(3) + (3) + 1` and quietly mean something
/// else. Several statements are spliced as they are, since parenthesising
/// them would not parse.
///
/// It is asked of the *expanded* tokens, not the rule body: a repetition
/// hides its separator inside `$( )`, and only expansion brings it out to
/// where it counts.
bool isSingleExpression(const std::vector<Token> &body) {
  int depth = 0;
  for (const Token &t : body) {
    if (isOpen(t.Kind))
      ++depth;
    else if (isClose(t.Kind))
      --depth;
    else if (depth == 0 && (t.Kind == Tok::Newline || t.Kind == Tok::Semi))
      return false;
  }
  return true;
}

/// What one `$name` captured. A repetition captures a list of them.
struct Binding {
  std::vector<Token> Tokens;
  std::vector<std::vector<Token>> Repeats;
  bool IsRepeat = false;
};

using Bindings = std::map<std::string, Binding>;

//===----------------------------------------------------------------------===//
// Matching
//===----------------------------------------------------------------------===//

/// The tokens a type is made of. Anything else inside a would-be `<...>`
/// means it was not one.
bool isTypeToken(Tok k) {
  switch (k) {
  case Tok::Identifier:
  case Tok::ColonColon:
  case Tok::Comma:
  case Tok::Colon:
  case Tok::LBracket:
  case Tok::RBracket:
  case Tok::LParen:
  case Tok::RParen:
  case Tok::IntLiteral:
  case Tok::Star:
  case Tok::Amp:
  case Tok::Question:
  case Tok::Arrow:
  case Tok::At:
  case Tok::KwVar:
  case Tok::KwSelfType:
  case Tok::Lt:
  case Tok::Gt:
    return true;
  default:
    return false;
  }
}

/// A value can follow a comparison's `>`, but never a generic list's.
bool startsValue(Tok k) {
  switch (k) {
  case Tok::Identifier:
  case Tok::IntLiteral:
  case Tok::FloatLiteral:
  case Tok::StringLiteral:
  case Tok::CharLiteral:
  case Tok::KwTrue:
  case Tok::KwFalse:
    return true;
  default:
    return false;
  }
}

/// If `t[i]` is a `<` opening a list of type arguments, the index just past
/// its `>`; otherwise `npos`.
///
/// `<` is both "less than" and "here come some types", and nothing at the
/// token level tells them apart — the parser decides it later, with more to go
/// on. Two things separate them well enough to split a macro's arguments on
/// the right commas: a type list contains only the tokens types are made of,
/// and what follows its `>` is never the start of a value. `Map<String, i64>()`
/// passes both; `a < b, c > d` fails the second.
size_t angleEnd(const std::vector<Token> &t, size_t i, size_t limit) {
  if (i >= limit || t[i].Kind != Tok::Lt)
    return std::string::npos;
  int angle = 0, bracket = 0;
  for (size_t k = i; k < limit; ++k) {
    const Tok c = t[k].Kind;
    if (c == Tok::LParen || c == Tok::LBracket)
      ++bracket;
    else if (c == Tok::RParen || c == Tok::RBracket) {
      if (--bracket < 0)
        return std::string::npos;
    } else if (bracket == 0 && c == Tok::Lt)
      ++angle;
    else if (bracket == 0 && c == Tok::Gt) {
      if (--angle == 0) {
        size_t after = k + 1;
        // A line break inside a type list would be odd; past its end it just
        // means the statement ended, which is a perfectly good follow.
        if (after < limit && startsValue(t[after].Kind))
          return std::string::npos;
        return after;
      }
      continue;
    }
    if (!isTypeToken(c))
      return std::string::npos;
  }
  return std::string::npos;
}


class Matcher {
public:
  Matcher(const std::vector<Token> &pat, const std::vector<Token> &in)
      : Pat(pat), In(in) {}

  bool run(Bindings &out) {
    size_t p = 0, i = 0;
    if (!matchRange(p, Pat.size(), i, In.size(), out))
      return false;
    return i == In.size();
  }

private:
  const std::vector<Token> &Pat;
  const std::vector<Token> &In;

  /// Consumes one argument for a fragment of `kind`, starting at `i`.
  ///
  /// `stop` is the token that ends the fragment at depth zero — the separator
  /// the pattern expects next, or `Unknown` when the fragment runs to the end.
  bool takeFragment(FragmentKind kind, size_t &i, size_t end, Tok stop,
                    std::vector<Token> &out) {
    if (i >= end)
      return false;
    switch (kind) {
    case FragmentKind::Ident:
      if (In[i].Kind != Tok::Identifier)
        return false;
      out.push_back(In[i++]);
      return true;
    case FragmentKind::Literal:
      if (!isLiteralToken(In[i].Kind))
        return false;
      out.push_back(In[i++]);
      return true;
    case FragmentKind::Block:
      if (In[i].Kind != Tok::LBrace)
        return false;
      // fallthrough to the token-tree rule, which takes the whole group
      [[fallthrough]];
    case FragmentKind::Tokens: {
      if (isOpen(In[i].Kind)) {
        size_t stopAt = skipGroup(In, i);
        for (size_t k = i; k < stopAt && k < end; ++k)
          out.push_back(In[k]);
        i = std::min(stopAt, end);
        return true;
      }
      out.push_back(In[i++]);
      return true;
    }
    case FragmentKind::Expr:
    case FragmentKind::Type: {
      // Everything up to the next `stop` that is not inside brackets. A
      // top-level `,` or `;` always ends it too, whatever the pattern says
      // next: neither can occur inside one expression, so a fragment that
      // swallowed one would eat the argument after it as well.
      int depth = 0;
      size_t start = i;
      while (i < end) {
        Tok k = In[i].Kind;
        // `Map<String, i64>` is one type, and the comma inside it is not an
        // argument separator. Taken whole so nothing looks at its insides.
        if (depth == 0) {
          size_t past = angleEnd(In, i, end);
          if (past != std::string::npos) {
            while (i < past)
              out.push_back(In[i++]);
            continue;
          }
        }
        if (isOpen(k))
          ++depth;
        else if (isClose(k)) {
          if (depth == 0)
            break;
          --depth;
        } else if (depth == 0 &&
                   (k == Tok::Comma || k == Tok::Semi ||
                    (stop != Tok::Unknown && k == stop))) {
          break;
        }
        out.push_back(In[i++]);
      }
      return i > start;
    }
    }
    return false;
  }

  /// Matches pattern `[p, pEnd)` against input `[i, iEnd)`.
  bool matchRange(size_t p, size_t pEnd, size_t &i, size_t iEnd,
                  Bindings &out) {
    while (p < pEnd) {
      const Token &pt = Pat[p];

      // `$( ... ) sep *` — a repetition.
      if (pt.Kind == Tok::Dollar && p + 1 < pEnd &&
          Pat[p + 1].Kind == Tok::LParen) {
        size_t bodyEnd = skipGroup(Pat, p + 1);   // just past the `)`
        size_t innerStart = p + 2, innerEnd = bodyEnd - 1;
        Tok sep = Tok::Unknown;
        size_t after = bodyEnd;
        if (after < pEnd && Pat[after].Kind != Tok::Star &&
            Pat[after].Kind != Tok::Plus) {
          sep = Pat[after].Kind;
          ++after;
        }
        const bool onePlus = after < pEnd && Pat[after].Kind == Tok::Plus;
        if (after < pEnd &&
            (Pat[after].Kind == Tok::Star || Pat[after].Kind == Tok::Plus))
          ++after;

        // What comes after the repetition tells us when to stop.
        Tok follow = after < pEnd ? Pat[after].Kind : Tok::Unknown;

        std::vector<std::string> names;
        for (size_t k = innerStart; k + 1 < innerEnd; ++k)
          if (Pat[k].Kind == Tok::Dollar && Pat[k + 1].Kind == Tok::Identifier)
            names.push_back(Pat[k + 1].Text);
        for (const std::string &n : names) {
          out[n].IsRepeat = true;
          out[n].Repeats.clear();
        }

        unsigned rounds = 0;
        while (i < iEnd) {
          if (follow != Tok::Unknown && In[i].Kind == follow)
            break;
          Bindings round;
          size_t save = i;
          if (!matchRange(innerStart, innerEnd, i, iEnd, round)) {
            i = save;
            break;
          }
          for (const std::string &n : names)
            out[n].Repeats.push_back(round.count(n) ? round[n].Tokens
                                                    : std::vector<Token>{});
          ++rounds;
          if (sep != Tok::Unknown) {
            if (i < iEnd && In[i].Kind == sep)
              ++i;
            else
              break;
          }
        }
        if (onePlus && rounds == 0)
          return false;
        p = after;
        continue;
      }

      // `$name: kind` — a fragment.
      if (pt.Kind == Tok::Dollar && p + 1 < pEnd &&
          Pat[p + 1].Kind == Tok::Identifier) {
        const std::string name = Pat[p + 1].Text;
        FragmentKind kind = FragmentKind::Tokens;
        size_t next = p + 2;
        if (next < pEnd && Pat[next].Kind == Tok::Colon &&
            next + 1 < pEnd) {
          std::string kindName = Pat[next + 1].Kind == Tok::Identifier
                                     ? Pat[next + 1].Text
                                     : tokenSpelling(Pat[next + 1].Kind);
          if (!fragmentFromName(kindName, kind))
            return false;
          next += 2;
        }
        Tok stop = next < pEnd && Pat[next].Kind != Tok::Dollar
                       ? Pat[next].Kind
                       : Tok::Unknown;
        std::vector<Token> got;
        if (!takeFragment(kind, i, iEnd, stop, got))
          return false;
        out[name].Tokens = std::move(got);
        p = next;
        continue;
      }

      // A literal token in the pattern has to be there in the input.
      if (i >= iEnd || In[i].Kind != pt.Kind)
        return false;
      if (pt.Kind == Tok::Identifier && In[i].Text != pt.Text)
        return false;
      ++p;
      ++i;
    }
    return true;
  }
};

//===----------------------------------------------------------------------===//
// Substitution
//===----------------------------------------------------------------------===//

/// Writes `body` into `out`, replacing `$name` with what it captured.
void substitute(const std::vector<Token> &body, const Bindings &binds,
                SourceRange at, std::vector<Token> &out) {
  for (size_t i = 0; i < body.size(); ++i) {
    const Token &t = body[i];

    // `$( ... ) sep *` — emit once per captured round.
    if (t.Kind == Tok::Dollar && i + 1 < body.size() &&
        body[i + 1].Kind == Tok::LParen) {
      size_t bodyEnd = skipGroup(body, i + 1);
      std::vector<Token> inner(body.begin() + static_cast<long>(i) + 2,
                               body.begin() + static_cast<long>(bodyEnd) - 1);
      size_t after = bodyEnd;
      Tok sep = Tok::Unknown;
      if (after < body.size() && body[after].Kind != Tok::Star &&
          body[after].Kind != Tok::Plus) {
        sep = body[after].Kind;
        ++after;
      }
      if (after < body.size() &&
          (body[after].Kind == Tok::Star || body[after].Kind == Tok::Plus))
        ++after;

      // How many rounds: whatever the fragments inside this repetition hold.
      size_t rounds = 0;
      for (size_t k = 0; k + 1 < inner.size(); ++k)
        if (inner[k].Kind == Tok::Dollar &&
            inner[k + 1].Kind == Tok::Identifier) {
          auto it = binds.find(inner[k + 1].Text);
          if (it != binds.end() && it->second.IsRepeat)
            rounds = std::max(rounds, it->second.Repeats.size());
        }

      for (size_t r = 0; r < rounds; ++r) {
        Bindings round;
        for (const auto &b : binds) {
          if (!b.second.IsRepeat) {
            round[b.first] = b.second;
          } else if (r < b.second.Repeats.size()) {
            Binding one;
            one.Tokens = b.second.Repeats[r];
            round[b.first] = one;
          }
        }
        substitute(inner, round, at, out);
        if (sep != Tok::Unknown && r + 1 < rounds) {
          Token s;
          s.Kind = sep;
          s.Range = at;
          out.push_back(s);
        }
      }
      i = after - 1;
      continue;
    }

    if (t.Kind == Tok::Dollar && i + 1 < body.size() &&
        body[i + 1].Kind == Tok::Identifier) {
      auto it = binds.find(body[i + 1].Text);
      if (it != binds.end()) {
        for (Token c : it->second.Tokens) {
          // Errors inside an expansion point at the invocation, which is the
          // only place the reader wrote anything.
          c.Range = at;
          out.push_back(c);
        }
        ++i;
        continue;
      }
    }

    Token c = t;
    c.Range = at;
    out.push_back(c);
  }
}

/// "it is private to <module>", built where a note needs it.
std::string fmtModuleNote(const std::string &module) {
  return "it is private to '" + module + "'";
}

//===----------------------------------------------------------------------===//
// stringify!
//===----------------------------------------------------------------------===//

/// The source text a run of tokens stands for, near enough to read back.
///
/// Spacing is what was written wherever the lexer recorded it, so
/// `std::reflect::fieldName<Point>(0)` comes back without spaces in it and
/// `x > 0` with them; tokens the compiler made itself are spaced by rule.
std::string spellTokens(const std::vector<Token> &t) {
  std::string out;
  for (size_t i = 0; i < t.size(); ++i) {
    const Token &tok = t[i];
    // A line break separates; it has no spelling of its own, and the name the
    // lexer gives it ("end of statement") would read as source that is there.
    if (isLayout(tok)) {
      if (!out.empty() && out.back() != ' ' && out.back() != ';')
        out += ";";
      continue;
    }
    std::string piece;
    switch (tok.Kind) {
    case Tok::Identifier:
      piece = tok.Text;
      break;
    case Tok::StringLiteral: {
      piece = "\"";
      for (char c : tok.Text) {
        if (c == '"' || c == '\\')
          piece += '\\';
        if (c == '\n') { piece += "\\n"; continue; }
        piece += c;
      }
      piece += "\"";
      break;
    }
    case Tok::IntLiteral:
    case Tok::FloatLiteral:
    case Tok::CharLiteral:
      piece = tok.Text.empty() ? std::string(tokenSpelling(tok.Kind)) : tok.Text;
      break;
    default:
      piece = tokenSpelling(tok.Kind);
      break;
    }
    if (!out.empty()) {
      const Tok prev = t[i - 1].Kind;
      // No space before a closer, a separator, or an opener that belongs to
      // the name in front of it; none after one that opens something. The
      // rest reads better spaced.
      bool tight = tok.Kind == Tok::RParen || tok.Kind == Tok::RBracket ||
                   tok.Kind == Tok::Comma || tok.Kind == Tok::Semi ||
                   tok.Kind == Tok::Dot || tok.Kind == Tok::ColonColon ||
                   tok.Kind == Tok::LParen || tok.Kind == Tok::LBracket;
      // `sum!(...)` is one thing; `a && !b` is not. What tells them apart is
      // whether a name comes first.
      if (tok.Kind == Tok::Bang && prev == Tok::Identifier)
        tight = true;
      const bool afterTight = prev == Tok::LParen || prev == Tok::LBracket ||
                              prev == Tok::Dot || prev == Tok::ColonColon ||
                              prev == Tok::Bang || prev == Tok::Backtick;
      if (!tight && !afterTight && (!tok.SpacingKnown || tok.SpaceBefore))
        out += " ";
    }
    out += piece;
  }
  return out;
}

//===----------------------------------------------------------------------===//
// format!
//===----------------------------------------------------------------------===//

/// Emits the tokens a `format!` stands for.
///
/// A macro body cannot do this: the placeholders live *inside* a string
/// literal, and by the time a body could look the literal is one token with no
/// structure. So the compiler reads it, and writes out the concatenation the
/// user would otherwise have written by hand.
struct FormatWriter {
  std::vector<Token> &Out;
  SourceRange At;

  void punct(Tok k) {
    Token t;
    t.Kind = k;
    t.Range = At;
    Out.push_back(t);
  }
  void ident(const std::string &name) {
    Token t;
    t.Kind = Tok::Identifier;
    t.Text = name;
    t.Range = At;
    Out.push_back(t);
  }
  void string(const std::string &value) {
    Token t;
    t.Kind = Tok::StringLiteral;
    t.Text = value;
    t.Range = At;
    Out.push_back(t);
  }
  void integer(int64_t value) {
    Token t;
    t.Kind = Tok::IntLiteral;
    t.Text = std::to_string(value);
    t.IntValue = static_cast<uint64_t>(value);
    t.Range = At;
    Out.push_back(t);
  }
  void character(char c) {
    Token t;
    t.Kind = Tok::CharLiteral;
    t.Text = std::string(1, c);
    t.IntValue = static_cast<uint64_t>(static_cast<unsigned char>(c));
    t.Range = At;
    Out.push_back(t);
  }
  void boolean(bool b) { punct(b ? Tok::KwTrue : Tok::KwFalse); }
  /// `std::fmt::<name>` — spelled in full so `format!` needs no import.
  void fmtPath(const char *name) {
    ident("std");
    punct(Tok::ColonColon);
    ident("fmt");
    punct(Tok::ColonColon);
    ident(name);
  }
  void keyword(Tok k) { punct(k); }
  void tokens(const std::vector<Token> &ts) {
    for (const Token &t : ts) {
      Token copy = t;
      copy.Range = At;
      Out.push_back(copy);
    }
  }
};

/// The name a `format!` argument is bound to when it has to be evaluated
/// before the pieces are built. The `$` cannot appear in a name the user
/// wrote, so these can never shadow one.
std::string argName(size_t index) {
  return "$fmt" + std::to_string(index);
}

std::vector<Token> nameTokens(const std::string &name, SourceRange at) {
  Token t;
  t.Kind = Tok::Identifier;
  t.Text = name;
  t.Range = at;
  return {t};
}

/// One `{...}` taken apart.
struct FormatSpec {
  char Fill = ' ';
  char Align = 0;      ///< '<', '^', '>' or 0 for "not asked for"
  bool Plus = false;
  bool Alternate = false;  ///< `#`, the `0x` in `{:#x}`
  int64_t Width = 0;
  int64_t Precision = -1;
  char Kind = 0;       ///< 'x', 'X', 'b', 'o' or 0
};

bool parseFormatSpec(const std::string &spec, FormatSpec &out,
                     std::string &error) {
  size_t p = 0;
  auto isAlign = [](char c) { return c == '<' || c == '^' || c == '>'; };
  // A fill character is only a fill when an alignment follows it; otherwise
  // `{:0}` would read `0` as a fill rather than as zero-padding.
  if (spec.size() >= 2 && isAlign(spec[1])) {
    out.Fill = spec[0];
    out.Align = spec[1];
    p = 2;
  } else if (!spec.empty() && isAlign(spec[0])) {
    out.Align = spec[0];
    p = 1;
  }
  if (p < spec.size() && spec[p] == '+') {
    out.Plus = true;
    ++p;
  }
  if (p < spec.size() && spec[p] == '#') {
    out.Alternate = true;
    ++p;
  }
  if (p < spec.size() && spec[p] == '0') {
    out.Fill = '0';
    if (!out.Align)
      out.Align = '>';
    ++p;
  }
  while (p < spec.size() && spec[p] >= '0' && spec[p] <= '9') {
    out.Width = out.Width * 10 + (spec[p] - '0');
    if (out.Width > 100000) {
      error = "the width is absurd";
      return false;
    }
    ++p;
  }
  if (p < spec.size() && spec[p] == '.') {
    ++p;
    if (p >= spec.size() || spec[p] < '0' || spec[p] > '9') {
      error = "`.` needs the number of places after it, as in `{:.2}`";
      return false;
    }
    out.Precision = 0;
    while (p < spec.size() && spec[p] >= '0' && spec[p] <= '9') {
      out.Precision = out.Precision * 10 + (spec[p] - '0');
      if (out.Precision > 30) {
        error = "at most 30 places after the point";
        return false;
      }
      ++p;
    }
  }
  if (p < spec.size()) {
    char k = spec[p];
    if (k != 'x' && k != 'X' && k != 'b' && k != 'o') {
      error = std::string("'") + k +
              "' is not a format kind; the kinds are `x`, `X`, `b` and `o`";
      return false;
    }
    out.Kind = k;
    ++p;
  }
  if (p != spec.size()) {
    error = "trailing characters after the format";
    return false;
  }
  if (out.Kind && out.Precision >= 0) {
    error = "a number of places and a radix cannot both apply";
    return false;
  }
  return true;
}

/// Splits `args` on the commas between arguments, ignoring those nested in a
/// group. The format string is `parts[0]`.
std::vector<std::vector<Token>> splitArguments(const std::vector<Token> &args) {
  std::vector<std::vector<Token>> parts;
  std::vector<Token> current;
  int depth = 0;
  for (size_t i = 0; i < args.size(); ++i) {
    // A generic argument list is one token tree, commas and all.
    if (depth == 0) {
      size_t past = angleEnd(args, i, args.size());
      if (past != std::string::npos) {
        while (i < past)
          current.push_back(args[i++]);
        --i;
        continue;
      }
    }
    const Token &t = args[i];
    if (isOpen(t.Kind))
      ++depth;
    else if (isClose(t.Kind))
      --depth;
    if (depth == 0 && t.Kind == Tok::Comma) {
      parts.push_back(current);
      current.clear();
      continue;
    }
    current.push_back(t);
  }
  if (!current.empty() || !parts.empty())
    parts.push_back(current);
  return parts;
}

/// Expands `format!(...)` into `out`. Returns false when something was
/// reported.
bool expandFormat(const std::vector<Token> &args, SourceRange at,
                  DiagnosticEngine &diags, std::vector<Token> &out) {
  std::vector<std::vector<Token>> parts = splitArguments(args);
  if (parts.empty() || parts[0].empty()) {
    diags.error(at, "`format!` needs a format string")
        .note("for example: format!(\"{} of {}\", got, want)")
        .code(505);
    return false;
  }
  if (parts[0].size() != 1 || parts[0][0].Kind != Tok::StringLiteral) {
    diags.error(parts[0][0].Range.isValid() ? parts[0][0].Range : at,
                "the first argument to `format!` has to be a string literal")
        .note("the placeholders are read at compile time, so the string has "
              "to be there to read")
        .note("to print a string you already have, write `format!(\"{}\", s)`")
        .code(505);
    return false;
  }

  const std::string fmt = parts[0][0].Text;
  const size_t argCount = parts.size() - 1;
  std::vector<bool> used(argCount, false);
  size_t nextArg = 0;

  // How each argument is reached. When the placeholders use every argument
  // exactly once and in the order they were written, the arguments can be
  // spliced where they are used. Otherwise they are bound to temporaries
  // first, so each is evaluated once, in the order it was written — which is
  // what `{0} {0}` and `{1} {0}` would otherwise quietly break.
  std::vector<size_t> order;
  {
    size_t peek = 0;
    for (size_t i = 0; i < fmt.size(); ++i) {
      if (fmt[i] == '{' && i + 1 < fmt.size() && fmt[i + 1] == '{') { ++i; continue; }
      if (fmt[i] == '}' && i + 1 < fmt.size() && fmt[i + 1] == '}') { ++i; continue; }
      if (fmt[i] != '{') continue;
      size_t close = fmt.find('}', i + 1);
      if (close == std::string::npos) break;
      std::string which = fmt.substr(i + 1, close - i - 1);
      size_t colon = which.find(':');
      if (colon != std::string::npos) which = which.substr(0, colon);
      i = close;
      if (which.empty()) {
        order.push_back(peek++);
      } else if (which.find_first_not_of("0123456789") == std::string::npos &&
                 !which.empty()) {
        order.push_back(static_cast<size_t>(std::stoull(which)));
      }
    }
  }
  bool inOrder = order.size() == argCount;
  for (size_t i = 0; inOrder && i < order.size(); ++i)
    inOrder = order[i] == i;
  const bool bindFirst = !inOrder && argCount > 0;

  FormatWriter w{out, at};
  // Pieces are joined with `+`, so the whole expansion is one expression and
  // splices anywhere one belongs.
  if (bindFirst) {
    w.punct(Tok::LBrace);
    for (size_t i = 0; i < argCount; ++i) {
      w.punct(Tok::KwLet);
      w.ident(argName(i));
      w.punct(Tok::Eq);
      w.tokens(parts[i + 1]);
      w.punct(Tok::Semi);
    }
  }
  w.punct(Tok::LParen);
  bool wrotePiece = false;
  auto join = [&] {
    if (wrotePiece)
      w.punct(Tok::Plus);
    wrotePiece = true;
  };

  std::string literal;
  auto flushLiteral = [&] {
    if (literal.empty())
      return;
    join();
    w.string(literal);
    literal.clear();
  };

  for (size_t i = 0; i < fmt.size(); ++i) {
    if (fmt[i] == '}') {
      if (i + 1 < fmt.size() && fmt[i + 1] == '}') {
        literal += '}';
        ++i;
        continue;
      }
      diags.error(at, "a `}}` in a format string has no `{{` to close")
          .note("write `}}}}` for a literal `}}`")
          .code(505);
      return false;
    }
    if (fmt[i] != '{') {
      literal += fmt[i];
      continue;
    }
    if (i + 1 < fmt.size() && fmt[i + 1] == '{') {
      literal += '{';
      ++i;
      continue;
    }
    size_t close = fmt.find('}', i + 1);
    if (close == std::string::npos) {
      diags.error(at, "a `{{` in a format string is never closed")
          .note("write `{{{{` for a literal `{{`")
          .code(505);
      return false;
    }
    const std::string inner = fmt.substr(i + 1, close - i - 1);
    i = close;

    std::string which = inner;
    std::string specText;
    size_t colon = inner.find(':');
    if (colon != std::string::npos) {
      which = inner.substr(0, colon);
      specText = inner.substr(colon + 1);
    }

    FormatSpec spec;
    std::string error;
    if (!parseFormatSpec(specText, spec, error)) {
      diags.error(at, "`{{{}}}` is not a format I understand", inner)
          .note(error.c_str())
          .note("the shape is `{{[argument][:[[fill]align][+][#][0][width]"
                "[.places][kind]]}}`")
          .code(505);
      return false;
    }

    // What is being formatted: the next argument, one named by position, or a
    // variable named in the placeholder itself.
    std::vector<Token> argTokens;
    bool named = false;
    if (which.empty()) {
      if (nextArg >= argCount) {
        diags.error(at,
                    "the format string needs at least {} arguments, but {} "
                    "were given", nextArg + 1, argCount)
            .note("every `{{}}` takes the next argument in turn")
            .note("write `{{{{}}}}` for a literal `{{}}`")
            .code(505);
        return false;
      }
      argTokens = bindFirst ? nameTokens(argName(nextArg), at)
                            : parts[nextArg + 1];
      used[nextArg] = true;
      ++nextArg;
    } else if (which.find_first_not_of("0123456789") == std::string::npos) {
      size_t index = static_cast<size_t>(std::stoull(which));
      if (index >= argCount) {
        diags.error(at, "`{{{}}}` asks for argument {} of {}", inner, index,
                    argCount)
            .note("arguments are counted from 0, after the format string")
            .code(505);
        return false;
      }
      argTokens = bindFirst ? nameTokens(argName(index), at)
                            : parts[index + 1];
      used[index] = true;
    } else {
      // `{name}` is the variable `name`, taken from where the macro was used.
      bool ok = !which.empty() &&
                (std::isalpha(static_cast<unsigned char>(which[0])) ||
                 which[0] == '_');
      for (char c : which)
        ok = ok && (std::isalnum(static_cast<unsigned char>(c)) || c == '_');
      if (!ok) {
        diags.error(at, "`{{{}}}` is neither a number nor a name", inner)
            .note("a placeholder holds an argument's position, a variable's "
                  "name, or nothing at all")
            .code(505);
        return false;
      }
      Token t;
      t.Kind = Tok::Identifier;
      t.Text = which;
      t.Range = at;
      argTokens.push_back(t);
      named = true;
    }
    (void)named;
    if (argTokens.empty()) {
      diags.error(at, "`format!` was given an empty argument").code(505);
      return false;
    }

    flushLiteral();
    join();

    // Built from the inside out: render, then sign, then width.
    if (spec.Width)
      w.fmtPath("pad"), w.punct(Tok::LParen);
    if (spec.Plus)
      w.fmtPath("plus"), w.punct(Tok::LParen);
    if (spec.Kind) {
      const int64_t base = spec.Kind == 'b' ? 2 : spec.Kind == 'o' ? 8 : 16;
      w.fmtPath("radix");
      w.punct(Tok::LParen);
      w.tokens(argTokens);
      w.punct(Tok::Comma);
      w.integer(base);
      w.punct(Tok::Comma);
      w.boolean(spec.Kind == 'X');
      w.punct(Tok::Comma);
      w.boolean(spec.Alternate);
      w.punct(Tok::RParen);
    } else if (spec.Precision >= 0) {
      w.fmtPath("fixed");
      w.punct(Tok::LParen);
      w.tokens(argTokens);
      w.punct(Tok::Comma);
      w.integer(spec.Precision);
      w.punct(Tok::RParen);
    } else {
      w.fmtPath("show");
      w.punct(Tok::LParen);
      w.tokens(argTokens);
      w.punct(Tok::RParen);
    }
    if (spec.Plus)
      w.punct(Tok::RParen);
    if (spec.Width) {
      w.punct(Tok::Comma);
      w.integer(spec.Width);
      w.punct(Tok::Comma);
      w.character(spec.Align ? spec.Align : '>');
      w.punct(Tok::Comma);
      w.character(spec.Fill);
      w.punct(Tok::RParen);
    }
  }
  flushLiteral();

  // A format string with nothing in it still stands for a String.
  if (!wrotePiece)
    w.string("");
  w.punct(Tok::RParen);
  if (bindFirst)
    w.punct(Tok::RBrace);

  for (size_t i = 0; i < argCount; ++i)
    if (!used[i]) {
      diags.error(parts[i + 1].empty() ? at : parts[i + 1][0].Range,
                  "argument {} is never used by the format string", i)
          .note("every argument needs a `{{}}` to put it in")
          .code(505);
      return false;
    }
  return true;
}

//===----------------------------------------------------------------------===//
// Collecting definitions
//===----------------------------------------------------------------------===//

/// Reads `macro name { (pat) => { body } ... }` starting at `i`, which points
/// at `macro`. Returns the index just past the closing brace.
size_t readDefinition(const std::vector<Token> &t, size_t i,
                      DiagnosticEngine &diags,
                      std::map<std::string, MacroDef> &out, bool record,
                      const std::string &module, bool isPublic) {
  const SourceRange at = t[i].Range;
  size_t j = i + 1;
  while (j < t.size() && isLayout(t[j]))
    ++j;
  if (j >= t.size() || t[j].Kind != Tok::Identifier) {
    diags.error(at, "a macro needs a name").code(120);
    return j;
  }
  MacroDef def;
  def.Name = t[j].Text;
  def.Range = at;
  def.IsPublic = isPublic;
  def.Module = module;
  // The `///` lines belong to whichever token follows them: `macro`, or the
  // `pub` in front of it, which is one token earlier.
  def.Doc = t[i].Doc;
  if (def.Doc.empty() && i > 0)
    def.Doc = t[i - 1].Doc;
  ++j;
  while (j < t.size() && isLayout(t[j]))
    ++j;
  if (j >= t.size() || t[j].Kind != Tok::LBrace) {
    diags.error(t[j < t.size() ? j : t.size() - 1].Range,
                "a macro's rules go in `{ ... }`")
        .note("write `macro name { (pattern) => { expansion } }`")
        .code(120);
    return j;
  }
  const size_t defEnd = skipGroup(t, j);
  size_t k = j + 1;                       // just inside the outer brace

  while (k + 1 < defEnd) {
    while (k < defEnd && isLayout(t[k]))
      ++k;
    if (k + 1 >= defEnd)
      break;
    if (t[k].Kind != Tok::LParen) {
      diags.error(t[k].Range, "a macro rule starts with its pattern in `( )`")
          .note("write `(pattern) => { expansion }`")
          .code(120);
      break;
    }
    size_t patEnd = skipGroup(t, k);
    MacroRule rule;
    rule.Range = t[k].Range;
    rule.Pattern = withoutLayout(
        std::vector<Token>(t.begin() + static_cast<long>(k) + 1,
                           t.begin() + static_cast<long>(patEnd) - 1));
    k = patEnd;
    while (k < defEnd && isLayout(t[k]))
      ++k;
    // `=>`, which lexes as `=` `>` or as one token depending on the lexer.
    if (k < defEnd && t[k].Kind == Tok::FatArrow) {
      ++k;
    } else if (k + 1 < defEnd && t[k].Kind == Tok::Eq &&
               t[k + 1].Kind == Tok::Gt) {
      k += 2;
    } else {
      diags.error(t[k < defEnd ? k : defEnd - 1].Range,
                  "a macro rule needs `=>` between its pattern and expansion")
          .code(120);
      break;
    }
    while (k < defEnd && isLayout(t[k]))
      ++k;
    if (k >= defEnd || t[k].Kind != Tok::LBrace) {
      diags.error(t[k < defEnd ? k : defEnd - 1].Range,
                  "a macro rule's expansion goes in `{ ... }`")
          .code(120);
      break;
    }
    size_t bodyEnd = skipGroup(t, k);
    rule.Body = std::vector<Token>(t.begin() + static_cast<long>(k) + 1,
                                   t.begin() + static_cast<long>(bodyEnd) - 1);
    k = bodyEnd;
    def.Rules.push_back(std::move(rule));
    while (k < defEnd && (isLayout(t[k]) || t[k].Kind == Tok::Comma))
      ++k;
  }

  if (!record)
    return defEnd;
  if (def.Rules.empty())
    diags.error(at, "macro '{}' has no rules", def.Name).code(120);
  auto existing = out.find(def.Name);
  if (existing != out.end()) {
    // Two private macros of one name in different files never meet, so they
    // are not a clash. Anything else is.
    const bool clashes = def.IsPublic || existing->second.IsPublic ||
                         existing->second.Module == def.Module;
    if (clashes) {
      auto d = diags.error(at, "macro '{}' is declared more than once",
                           def.Name);
      if (existing->second.Module != def.Module)
        d.note("one of them is `pub`, so both are in scope at once — make the "
               "one that is only used in its own file private");
      d.related(existing->second.Range, "the earlier declaration",
                "a macro name is taken for as far as it is visible");
      d.code(120);
      return defEnd;
    }
  }
  out[def.Name] = std::move(def);
  return defEnd;
}

} // namespace

//===----------------------------------------------------------------------===//

void collectMacros(std::vector<Token> &toks, DiagnosticEngine &diags,
                   MacroTable &into, bool record, const std::string &module) {
  bool sawDefinition = false;
  for (const Token &t : toks)
    if (t.Kind == Tok::KwMacro) {
      sawDefinition = true;
      break;
    }
  if (!sawDefinition)
    return;

  std::vector<Token> kept;
  kept.reserve(toks.size());
  for (size_t i = 0; i < toks.size();) {
    // `pub macro` — the `pub` belongs to the macro, so it goes with it.
    if (toks[i].Kind == Tok::KwPub) {
      size_t j = i + 1;
      while (j < toks.size() && isLayout(toks[j]))
        ++j;
      if (j < toks.size() && toks[j].Kind == Tok::KwMacro) {
        i = readDefinition(toks, j, diags, into, record, module,
                           /*isPublic=*/true);
        continue;
      }
    }
    // `#macro` is the *attribute* that marks a procedural macro, not the
    // start of a declarative one. It is taken out by `collectProcMacros`.
    if (toks[i].Kind == Tok::KwMacro &&
        !(i > 0 && (toks[i - 1].Kind == Tok::At ||
                    toks[i - 1].Kind == Tok::Hash))) {
      i = readDefinition(toks, i, diags, into, record, module,
                         /*isPublic=*/false);
      continue;
    }
    kept.push_back(toks[i++]);
  }
  toks.swap(kept);
}

bool expandMacros(std::vector<Token> &toks, DiagnosticEngine &diags,
                  const MacroTable &macros, const std::string &module,
                  unsigned depthLimit, const MacroPackage *procs) {
  // Pass two: expand, repeatedly, so a macro may expand into another.
  bool ok = true;
  for (unsigned round = 0; round < depthLimit; ++round) {
    bool expandedAny = false;
    std::vector<Token> out;
    out.reserve(toks.size());

    for (size_t i = 0; i < toks.size();) {
      const bool isCall = toks[i].Kind == Tok::Identifier &&
                          i + 2 < toks.size() && toks[i + 1].Kind == Tok::Bang &&
                          isOpen(toks[i + 2].Kind);
      if (!isCall) {
        out.push_back(toks[i++]);
        continue;
      }

      const std::string name = toks[i].Text;
      const SourceRange at = toks[i].Range;
      // A call never closed — a string left open inside it swallows the
      // rest of the file — is not expanded: its arguments would take the end
      // of the file with them, and the parser would have no end to stop at.
      {
        size_t end = skipGroup(toks, i + 2);
        if (end == 0 || !isClose(toks[end - 1].Kind)) {
          diags.error(at, "`{}!(` is never closed", name)
              .note("everything to the end of the file was read as its "
                    "arguments; an unterminated string inside it does that")
              .code(127);
          ok = false;
          for (; i < toks.size(); ++i)
            out.push_back(toks[i]);
          break;
        }
      }
      // `stringify!(...)` is the compiler's own: the argument tokens, as the
      // text they were written as. Nothing else can produce it, because the
      // spelling is gone by the time a macro body could look.
      // `format!(...)` is the compiler's own too: the placeholders live
      // inside a string literal, which no macro body can see into.
      if (name == "format") {
        size_t argsEnd = skipGroup(toks, i + 2);
        std::vector<Token> args = withoutLayout(std::vector<Token>(
            toks.begin() + static_cast<long>(i) + 3,
            toks.begin() + static_cast<long>(argsEnd) - 1));
        std::vector<Token> expanded;
        if (!expandFormat(args, at, diags, expanded)) {
          ok = false;
        } else {
          out.insert(out.end(), expanded.begin(), expanded.end());
          diags.noteExpansion(at, spellTokens(withoutLayout(std::vector<Token>(
                                      toks.begin() + static_cast<long>(i),
                                      toks.begin() + static_cast<long>(argsEnd)))),
                              spellTokens(expanded));
        }
        expandedAny = true;
        i = argsEnd;
        continue;
      }

      if (name == "stringify") {
        size_t argsEnd = skipGroup(toks, i + 2);
        std::vector<Token> args = withoutLayout(std::vector<Token>(
            toks.begin() + static_cast<long>(i) + 3,
            toks.begin() + static_cast<long>(argsEnd) - 1));
        Token lit;
        lit.Kind = Tok::StringLiteral;
        lit.Text = spellTokens(args);
        lit.Range = at;
        out.push_back(lit);
        expandedAny = true;
        i = argsEnd;
        continue;
      }

      // A procedural macro — a `#macro fn` — is run rather than matched. It
      // is looked for first, so a name is one kind of macro or the other and
      // never quietly both.
      if (procs) {
        auto pit = procs->Macros.find(name);
        if (pit != procs->Macros.end()) {
          size_t argsEnd = skipGroup(toks, i + 2);
          // The brackets themselves are kept, so a macro sees what is inside
          // them as the group it was written as.
          std::vector<Token> args(
              toks.begin() + static_cast<long>(i) + 3,
              toks.begin() + static_cast<long>(argsEnd) - 1);
          std::vector<Token> produced;
          if (!runProcMacro(pit->second, procs->Program, args, at, diags,
                            produced)) {
            ok = false;
          } else {
            out.insert(out.end(), produced.begin(), produced.end());
            diags.noteExpansion(
                at,
                spellTokens(withoutLayout(std::vector<Token>(
                    toks.begin() + static_cast<long>(i),
                    toks.begin() + static_cast<long>(argsEnd)))),
                spellTokens(produced));
          }
          expandedAny = true;
          i = argsEnd;
          continue;
        }
      }

      auto mit = macros.find(name);
      // A macro that is not `pub` is only in scope in the file it was written
      // in. Saying so here is far clearer than letting the grammar trip over
      // the `!` further along.
      if (mit != macros.end() && !mit->second.IsPublic &&
          mit->second.Module != module) {
        auto d = diags.error(at, "macro '{}' is not visible here", name);
        d.note(mit->second.Module.empty()
                   ? std::string("it is private to the file it is written in")
                         .c_str()
                   : fmtModuleNote(mit->second.Module).c_str());
        d.note("mark it `pub macro` to use it from another module");
        d.related(mit->second.Range, "declared here",
                  "a macro without `pub` is private to its own file");
        d.code(123);
        ok = false;
        i = skipGroup(toks, i + 2);
        continue;
      }
      if (mit == macros.end()) {
        // `name!(...)` is a macro invocation and nothing else — `!` is prefix
        // negation, which cannot follow a name — so this is a call to one
        // that does not exist.
        auto d = diags.error(at, "no macro named '{}'", name);
        std::string names;
        for (const auto &m : macros)
          if (m.second.IsPublic || m.second.Module == module)
            names += (names.empty() ? "" : ", ") + m.first;
        if (procs)
          for (const auto &m : procs->Macros)
            names += (names.empty() ? "" : ", ") + m.first;
        if (!names.empty())
          d.note(("in scope here: " + names).c_str());
        d.note("a macro is declared with `macro name { (pattern) => { ... } }`, "
               "or written as code with `#macro fn name(...)`");
        d.code(123);
        ok = false;
        i = skipGroup(toks, i + 2);
        continue;
      }

      size_t argsEnd = skipGroup(toks, i + 2);
      std::vector<Token> args = withoutLayout(std::vector<Token>(
          toks.begin() + static_cast<long>(i) + 3,
          toks.begin() + static_cast<long>(argsEnd) - 1));

      bool matched = false;
      for (const MacroRule &rule : mit->second.Rules) {
        Bindings binds;
        Matcher m(rule.Pattern, args);
        if (!m.run(binds))
          continue;
        std::vector<Token> expanded;
        substitute(rule.Body, binds, at, expanded);
        // The line breaks around a rule's body are its layout in the
        // definition, not part of what it stands for. Left in, a body that is
        // one `{ ... }` block would look like several statements and be
        // spliced bare, so `vec!(1, 2).length()` would not parse.
        while (!expanded.empty() && isLayout(expanded.front()))
          expanded.erase(expanded.begin());
        while (!expanded.empty() && isLayout(expanded.back()))
          expanded.pop_back();
        if (isSingleExpression(expanded)) {
          Token open;
          open.Kind = Tok::LParen;
          open.Range = at;
          out.push_back(open);
          out.insert(out.end(), expanded.begin(), expanded.end());
          Token close;
          close.Kind = Tok::RParen;
          close.Range = at;
          out.push_back(close);
        } else {
          out.insert(out.end(), expanded.begin(), expanded.end());
        }
        // Keep what this stood for, so an error landing inside it can show
        // the code the compiler actually saw.
        {
          std::vector<Token> call(toks.begin() + static_cast<long>(i),
                                  toks.begin() + static_cast<long>(argsEnd));
          diags.noteExpansion(at, spellTokens(withoutLayout(call)),
                              spellTokens(expanded));
        }
        matched = true;
        expandedAny = true;
        break;
      }
      if (!matched) {
        auto d = diags.error(at, "no rule of macro '{}' matches these arguments",
                             name);
        d.note(mit->second.Rules.size() == 1
                   ? "the macro has one rule; the arguments have to fit it"
                   : "none of the macro's rules fit these arguments");
        d.related(mit->second.Range, "declared here",
                  "each rule is a pattern the arguments must match");
        d.code(121);
        ok = false;
      }
      i = argsEnd;
    }

    toks.swap(out);
    if (!expandedAny)
      break;
    if (round + 1 == depthLimit) {
      diags.error(toks.empty() ? SourceRange() : toks.front().Range,
                  "macro expansion did not settle after {} rounds", depthLimit)
          .note("a macro that expands to a call of itself never finishes")
          .code(122);
      ok = false;
    }
  }
  return ok;
}


bool expandMacros(std::vector<Token> &toks, DiagnosticEngine &diags,
                  unsigned depthLimit) {
  MacroTable local;
  collectMacros(toks, diags, local);
  return expandMacros(toks, diags, local, "", depthLimit);
}

} // namespace rune
