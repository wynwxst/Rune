#include "rune/Lexer.h"

#include <cctype>
#include <cstdlib>

namespace rune {

namespace {
bool isIdentStart(char c) {
  return std::isalpha(static_cast<unsigned char>(c)) || c == '_' ||
         static_cast<unsigned char>(c) >= 0x80; // allow UTF-8 identifiers
}
bool isIdentCont(char c) {
  return isIdentStart(c) || std::isdigit(static_cast<unsigned char>(c));
}
bool isDigit(char c) { return c >= '0' && c <= '9'; }

int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

/// Encodes a Unicode scalar as UTF-8 into `out`.
void appendUTF8(std::string &out, uint32_t cp) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

bool isKnownNumericSuffix(const std::string &s) {
  static const char *kSuffixes[] = {"i8",  "i16", "i32",   "i64",   "u8",
                                    "u16", "u32", "u64",   "f32",   "f64",
                                    "isize", "usize"};
  for (const char *k : kSuffixes)
    if (s == k)
      return true;
  return false;
}
} // namespace

Lexer::Lexer(const SourceManager &sm, DiagnosticEngine &diags, unsigned fileID)
    : SM(sm), Diags(diags), File(sm.file(fileID)), Buf(sm.file(fileID).Buffer.data()),
      Len(sm.file(fileID).Buffer.size()) {}

//===----------------------------------------------------------------------===//
// Trivia
//===----------------------------------------------------------------------===//

bool Lexer::skipTrivia(bool prevCanEnd, size_t &newlineStart) {
  while (!eof()) {
    char c = peek();
    if (c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f') {
      ++Pos;
      continue;
    }
    if (c == '\n') {
      if (GroupDepth > 0 || !prevCanEnd) {
        CrossedNewline = true;
        ++Pos;
        continue;
      }
      // A line beginning with `.` continues the previous expression, which is
      // what makes fluent chains work:
      //     builder()
      //         .with(x)
      size_t probe = Pos + 1;
      while (probe < Len) {
        char p = Buf[probe];
        if (p == ' ' || p == '\t' || p == '\r' || p == '\n') { ++probe; continue; }
        if (p == '/' && probe + 1 < Len && Buf[probe + 1] == '/') {
          while (probe < Len && Buf[probe] != '\n') ++probe;
          continue;
        }
        if (p == '/' && probe + 1 < Len && Buf[probe + 1] == '*') {
          int depth = 0;
          while (probe < Len) {
            if (Buf[probe] == '/' && probe + 1 < Len && Buf[probe + 1] == '*') {
              depth++; probe += 2; continue;
            }
            if (Buf[probe] == '*' && probe + 1 < Len && Buf[probe + 1] == '/') {
              depth--; probe += 2;
              if (depth == 0) break;
              continue;
            }
            ++probe;
          }
          continue;
        }
        break;
      }
      if (probe < Len && Buf[probe] == '.' &&
          !(probe + 1 < Len && Buf[probe + 1] == '.')) {
        CrossedNewline = true;
        ++Pos;
        continue;
      }
      newlineStart = Pos;
      ++Pos;
      return true;
    }
    if (c == '/' && peek(1) == '/') {
      // `///` is documentation rather than a remark: the lines are kept and
      // handed to whichever declaration comes next. Anything else is skipped.
      const bool isDoc = peek(2) == '/' && peek(3) != '/';
      size_t textStart = Pos + 3;
      while (!eof() && peek() != '\n')
        ++Pos;
      if (isDoc) {
        std::string line(Buf + textStart, Pos - textStart);
        // One leading space is the convention, not part of the text.
        if (!line.empty() && line.front() == ' ')
          line.erase(line.begin());
        if (!PendingDoc.empty())
          PendingDoc += "\n";
        PendingDoc += line;
        SawDocThisRun = true;
      }
      continue;
    }
    if (c == '/' && peek(1) == '*') {
      size_t start = Pos;
      int depth = 0;
      while (!eof()) {
        if (peek() == '/' && peek(1) == '*') { depth++; Pos += 2; continue; }
        if (peek() == '*' && peek(1) == '/') {
          depth--; Pos += 2;
          if (depth == 0) break;
          continue;
        }
        ++Pos;
      }
      if (depth != 0)
        Diags.error(SourceRange(locAt(start), locAt(start + 2)),
                    "unterminated block comment")
            .note("block comments nest, so every `/*` needs a matching `*/`")
            .code(1);
      continue;
    }
    break;
  }
  return false;
}

//===----------------------------------------------------------------------===//
// Literals
//===----------------------------------------------------------------------===//

std::string Lexer::lexNumericSuffix() {
  if (!isIdentStart(peek()))
    return {};
  size_t start = Pos;
  while (isIdentCont(peek()))
    ++Pos;
  std::string s(Buf + start, Pos - start);
  if (!isKnownNumericSuffix(s)) {
    Diags.error(SourceRange(locAt(start), locAt(Pos)),
                "unknown numeric suffix '{}'", s)
        .note("valid suffixes are i8/i16/i32/i64, u8/u16/u32/u64, isize, "
              "usize, f32 and f64")
        .code(2);
    return {};
  }
  return s;
}

Token Lexer::lexNumber() {
  size_t start = Pos;
  Token t;
  t.Kind = Tok::IntLiteral;

  auto digitsWithSeparators = [&](int base) {
    std::string digits;
    while (!eof()) {
      char c = peek();
      if (c == '_') { ++Pos; continue; }
      int v = hexVal(c);
      if (v < 0 || v >= base)
        break;
      digits.push_back(c);
      ++Pos;
    }
    return digits;
  };

  if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
    Pos += 2;
    std::string d = digitsWithSeparators(16);
    if (d.empty())
      Diags.error(rangeFrom(start), "hexadecimal literal has no digits").code(3);
    t.IntValue = strtoull(d.c_str(), nullptr, 16);
  } else if (peek() == '0' && (peek(1) == 'b' || peek(1) == 'B')) {
    Pos += 2;
    std::string d = digitsWithSeparators(2);
    if (d.empty())
      Diags.error(rangeFrom(start), "binary literal has no digits").code(3);
    t.IntValue = strtoull(d.c_str(), nullptr, 2);
  } else if (peek() == '0' && (peek(1) == 'o' || peek(1) == 'O')) {
    Pos += 2;
    std::string d = digitsWithSeparators(8);
    if (d.empty())
      Diags.error(rangeFrom(start), "octal literal has no digits").code(3);
    t.IntValue = strtoull(d.c_str(), nullptr, 8);
  } else {
    std::string text = digitsWithSeparators(10);
    bool isFloat = false;
    // `1..5` is a range, and `tuple.0.1` is two field accesses, so a `.` only
    // starts a fraction when a digit follows it.
    if (peek() == '.' && isDigit(peek(1))) {
      isFloat = true;
      text.push_back('.');
      ++Pos;
      text += digitsWithSeparators(10);
    }
    if (peek() == 'e' || peek() == 'E') {
      size_t save = Pos;
      std::string exp;
      exp.push_back(peek());
      ++Pos;
      if (peek() == '+' || peek() == '-') { exp.push_back(peek()); ++Pos; }
      std::string d = digitsWithSeparators(10);
      if (d.empty()) {
        Pos = save; // not an exponent after all (e.g. `1e` followed by ident)
      } else {
        isFloat = true;
        text += exp + d;
      }
    }
    if (isFloat) {
      t.Kind = Tok::FloatLiteral;
      t.FloatValue = strtod(text.c_str(), nullptr);
    } else {
      t.IntValue = strtoull(text.c_str(), nullptr, 10);
    }
  }

  t.Suffix = lexNumericSuffix();
  if (!t.Suffix.empty() && t.Kind == Tok::IntLiteral &&
      (t.Suffix == "f32" || t.Suffix == "f64")) {
    t.Kind = Tok::FloatLiteral;
    t.FloatValue = static_cast<double>(t.IntValue);
  }
  t.Range = rangeFrom(start);
  t.Text = std::string(Buf + start, Pos - start);
  return t;
}

void Lexer::lexEscape(std::string &out, size_t escStart) {
  char c = eof() ? '\0' : advance();
  switch (c) {
  case 'n': out.push_back('\n'); return;
  case 't': out.push_back('\t'); return;
  case 'r': out.push_back('\r'); return;
  case '0': out.push_back('\0'); return;
  case '\\': out.push_back('\\'); return;
  case '"': out.push_back('"'); return;
  case '\'': out.push_back('\''); return;
  case 'e': out.push_back('\x1b'); return;
  case 'x': {
    int hi = hexVal(peek()), lo = hexVal(peek(1));
    if (hi < 0 || lo < 0) {
      Diags.error(SourceRange(locAt(escStart), locAt(Pos)),
                  "`\\x` needs exactly two hexadecimal digits")
          .code(4);
      return;
    }
    Pos += 2;
    out.push_back(static_cast<char>(hi * 16 + lo));
    return;
  }
  case 'u': {
    if (peek() != '{') {
      Diags.error(SourceRange(locAt(escStart), locAt(Pos)),
                  "`\\u` must be followed by `{...}`")
          .note("write the scalar in braces, e.g. `\\u{1F600}`")
          .code(4);
      return;
    }
    ++Pos;
    uint32_t cp = 0;
    bool any = false;
    while (!eof() && peek() != '}') {
      int v = hexVal(advance());
      if (v < 0) {
        Diags.error(SourceRange(locAt(escStart), locAt(Pos)),
                    "invalid hexadecimal digit in `\\u{...}`")
            .code(4);
        return;
      }
      cp = cp * 16 + static_cast<uint32_t>(v);
      any = true;
    }
    if (!match('}') || !any) {
      Diags.error(SourceRange(locAt(escStart), locAt(Pos)),
                  "unterminated `\\u{...}` escape")
          .code(4);
      return;
    }
    if (cp > 0x10FFFF) {
      Diags.error(SourceRange(locAt(escStart), locAt(Pos)),
                  "`\\u{{{}}}` is out of range for a Unicode scalar", cp)
          .note("the largest scalar value is \\u{10FFFF}")
          .code(4);
      return;
    }
    appendUTF8(out, cp);
    return;
  }
  default:
    Diags.error(SourceRange(locAt(escStart), locAt(Pos)),
                "unknown escape sequence '\\{}'", c)
        .note("supported escapes: \\n \\t \\r \\0 \\\\ \\\" \\' \\e \\xNN "
              "\\u{...}")
        .code(4);
    out.push_back(c);
  }
}

/// Removes the indentation a block string shares with the code around it, and
/// the blank first and last lines that come from putting the delimiters on
/// their own. What is left is the text as it was written.
static std::string stripBlockIndent(const std::string &raw) {
  std::vector<std::string> lines;
  std::string cur;
  for (char c : raw) {
    if (c == '\n') { lines.push_back(cur); cur.clear(); }
    else cur.push_back(c);
  }
  lines.push_back(cur);

  // A newline straight after the opening delimiter, and one before the
  // closing delimiter, are part of the formatting rather than the text.
  if (!lines.empty() && lines.front().find_first_not_of(" \t") ==
                            std::string::npos)
    lines.erase(lines.begin());
  if (!lines.empty() && lines.back().find_first_not_of(" \t") ==
                            std::string::npos)
    lines.pop_back();

  size_t common = std::string::npos;
  for (const std::string &l : lines) {
    size_t first = l.find_first_not_of(" \t");
    if (first == std::string::npos)
      continue;                      // a blank line sets no floor
    common = std::min(common, first);
  }
  if (common == std::string::npos)
    common = 0;

  std::string out;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i) out.push_back('\n');
    const std::string &l = lines[i];
    out += l.size() > common ? l.substr(common) : std::string();
  }
  return out;
}

Token Lexer::lexString(bool raw) {
  size_t start = Pos;
  Token t;
  t.Kind = Tok::StringLiteral;

  if (raw) {
    ++Pos; // consume 'r'
    unsigned hashes = 0;
    while (peek() == '#') { ++hashes; ++Pos; }
    if (!match('"')) {
      Diags.error(rangeFrom(start), "raw string literal must start with `\"`")
          .note("write raw strings as r\"...\" or r#\"...\"#")
          .code(5);
      t.Range = rangeFrom(start);
      return t;
    }
    std::string value;
    bool closed = false;
    while (!eof()) {
      if (peek() == '"') {
        size_t probe = Pos + 1;
        unsigned seen = 0;
        while (probe < Len && Buf[probe] == '#' && seen < hashes) { ++seen; ++probe; }
        if (seen == hashes) { Pos = probe; closed = true; break; }
      }
      value.push_back(advance());
    }
    if (!closed)
      Diags.error(rangeFrom(start), "unterminated raw string literal").code(5);
    t.Text = std::move(value);
    t.Range = rangeFrom(start);
    return t;
  }

  // `"""` opens a string that runs to the matching `"""`, newlines and all.
  // The common indentation of its lines is removed, so a block quoted inside
  // an indented declaration reads at the margin rather than carrying the
  // code's shape into the text.
  if (peek() == '"' && Pos + 2 < Len && Buf[Pos + 1] == '"' &&
      Buf[Pos + 2] == '"') {
    Pos += 3;
    std::string raw;
    bool done = false;
    while (!eof()) {
      if (peek() == '"' && Pos + 2 < Len && Buf[Pos + 1] == '"' &&
          Buf[Pos + 2] == '"') {
        Pos += 3;
        done = true;
        break;
      }
      if (peek() == '\\') {
        size_t escStart = Pos;
        ++Pos;
        lexEscape(raw, escStart);
        continue;
      }
      raw.push_back(advance());
    }
    if (!done)
      Diags.error(rangeFrom(start), "unterminated multi-line string literal")
          .note("a `\"\"\"` string ends at the next `\"\"\"`")
          .code(5);
    t.Text = stripBlockIndent(raw);
    t.Range = rangeFrom(start);
    return t;
  }

  ++Pos; // consume the opening quote
  std::string value;
  bool closed = false;
  while (!eof()) {
    char c = peek();
    if (c == '"') { ++Pos; closed = true; break; }
    if (c == '\n') break; // unterminated; stop at end of line for recovery
    if (c == '\\') {
      size_t escStart = Pos;
      ++Pos;
      lexEscape(value, escStart);
      continue;
    }
    value.push_back(advance());
  }
  if (!closed)
    Diags.error(rangeFrom(start), "unterminated string literal")
        .note("string literals cannot span lines; use `\\n` or concatenation")
        .code(5);
  t.Text = std::move(value);
  t.Range = rangeFrom(start);
  return t;
}

Token Lexer::lexChar() {
  size_t start = Pos;
  ++Pos; // consume '
  Token t;
  t.Kind = Tok::CharLiteral;

  std::string decoded;
  if (peek() == '\\') {
    size_t escStart = Pos;
    ++Pos;
    lexEscape(decoded, escStart);
  } else if (!eof() && peek() != '\'') {
    // Copy one whole UTF-8 sequence.
    unsigned char lead = static_cast<unsigned char>(peek());
    unsigned extra = lead < 0x80 ? 0 : lead < 0xE0 ? 1 : lead < 0xF0 ? 2 : 3;
    decoded.push_back(advance());
    for (unsigned i = 0; i < extra && !eof(); ++i)
      decoded.push_back(advance());
  }

  if (!match('\'')) {
    Diags.error(rangeFrom(start), "unterminated character literal")
        .note("a Character holds exactly one Unicode scalar; use \"...\" for "
              "text")
        .code(6);
  }

  // Decode back to a scalar value.
  uint32_t cp = 0;
  if (!decoded.empty()) {
    unsigned char lead = static_cast<unsigned char>(decoded[0]);
    if (lead < 0x80) {
      cp = lead;
    } else if (lead < 0xE0 && decoded.size() >= 2) {
      cp = ((lead & 0x1Fu) << 6) | (decoded[1] & 0x3Fu);
    } else if (lead < 0xF0 && decoded.size() >= 3) {
      cp = ((lead & 0x0Fu) << 12) | ((decoded[1] & 0x3Fu) << 6) |
           (decoded[2] & 0x3Fu);
    } else if (decoded.size() >= 4) {
      cp = ((lead & 0x07u) << 18) | ((decoded[1] & 0x3Fu) << 12) |
           ((decoded[2] & 0x3Fu) << 6) | (decoded[3] & 0x3Fu);
    }
  }
  t.IntValue = cp;
  t.Text = decoded;
  t.Range = rangeFrom(start);
  return t;
}

Token Lexer::lexIdentifierOrKeyword() {
  size_t start = Pos;
  while (isIdentCont(peek()))
    ++Pos;
  Token t;
  t.Text = std::string(Buf + start, Pos - start);
  t.Kind = t.Text == "_" ? Tok::Underscore : keywordKind(t.Text);
  t.Range = rangeFrom(start);
  return t;
}

Token Lexer::lexPunctuation() {
  size_t start = Pos;
  char c = advance();
  Tok k = Tok::Unknown;

  auto two = [&](char next, Tok ifMatch, Tok otherwise) {
    return match(next) ? ifMatch : otherwise;
  };

  switch (c) {
  case '(': k = Tok::LParen; ++GroupDepth; break;
  case ')': k = Tok::RParen; if (GroupDepth) --GroupDepth; break;
  case '[': k = Tok::LBracket; ++GroupDepth; break;
  case ']': k = Tok::RBracket; if (GroupDepth) --GroupDepth; break;
  case '{': k = Tok::LBrace; break;
  case '}': k = Tok::RBrace; break;
  case ',': k = Tok::Comma; break;
  case ';': k = Tok::Semi; break;
  case '@': k = Tok::At; break;
  case '#': k = Tok::Hash; break;
  case '$': k = Tok::Dollar; break;
  case '`': k = Tok::Backtick; break;
  case '~': k = Tok::Tilde; break;
  case '?': k = two('?', Tok::QuestionQuestion, Tok::Question); break;
  case ':': k = two(':', Tok::ColonColon, Tok::Colon); break;
  case '.':
    if (match('.'))
      k = match('.') ? Tok::Ellipsis : (match('=') ? Tok::DotDotEq : Tok::DotDot);
    else
      k = Tok::Dot;
    break;
  case '+': k = two('=', Tok::PlusEq, Tok::Plus); break;
  case '-':
    k = match('=') ? Tok::MinusEq : (match('>') ? Tok::Arrow : Tok::Minus);
    break;
  case '*': k = two('=', Tok::StarEq, Tok::Star); break;
  case '/': k = two('=', Tok::SlashEq, Tok::Slash); break;
  case '%': k = two('=', Tok::PercentEq, Tok::Percent); break;
  case '^': k = two('=', Tok::CaretEq, Tok::Caret); break;
  case '!': k = two('=', Tok::NotEq, Tok::Bang); break;
  case '=':
    k = match('=') ? Tok::EqEq : (match('>') ? Tok::FatArrow : Tok::Eq);
    break;
  case '&':
    k = match('&') ? Tok::AmpAmp : (match('=') ? Tok::AmpEq : Tok::Amp);
    break;
  case '|':
    k = match('|') ? Tok::PipePipe : (match('=') ? Tok::PipeEq : Tok::Pipe);
    break;
  case '<':
    if (match('<'))
      k = match('=') ? Tok::ShlEq : Tok::Shl;
    else
      k = match('=') ? Tok::LtEq : Tok::Lt;
    break;
  case '>':
    // `>>` is produced here but the parser splits it back apart when it is
    // closing two nested generic argument lists (`Map<K, Vec<V>>`).
    if (match('>'))
      k = match('=') ? Tok::ShrEq : Tok::Shr;
    else
      k = match('=') ? Tok::GtEq : Tok::Gt;
    break;
  default:
    k = Tok::Unknown;
    break;
  }

  Token t;
  t.Kind = k;
  t.Range = rangeFrom(start);
  t.Text = std::string(Buf + start, Pos - start);
  if (k == Tok::Unknown)
    Diags.error(t.Range, "unexpected character '{}' in source", t.Text)
        .note("Rune source must be valid UTF-8 text")
        .code(7);
  return t;
}

//===----------------------------------------------------------------------===//
// Driver
//===----------------------------------------------------------------------===//

std::vector<Token> Lexer::tokenize() {
  std::vector<Token> tokens;
  // Rune source runs at about one token per five bytes; reserving for that
  // spares the copies a growing vector of tokens would otherwise make.
  tokens.reserve(Len / 5 + 16);
  bool atLineStart = true;

  for (;;) {
    bool prevCanEnd = !tokens.empty() && canEndStatement(tokens.back().Kind);
    size_t nlStart = 0;
    if (skipTrivia(prevCanEnd, nlStart)) {
      Token nl;
      nl.Kind = Tok::Newline;
      nl.Range = SourceRange(locAt(nlStart), locAt(nlStart + 1));
      tokens.push_back(std::move(nl));
      atLineStart = true;
      continue;
    }
    if (eof())
      break;

    char c = peek();
    Token t;
    // Whether anything separated this token from the one before: what lets
    // the tokens be printed back as they were written.
    const bool spaced = Pos > 0 && (Buf[Pos - 1] == ' ' || Buf[Pos - 1] == '\t' ||
                                    Buf[Pos - 1] == '\n' || Buf[Pos - 1] == '\r' ||
                                    (Buf[Pos - 1] == '/' && Pos > 1 &&
                                     Buf[Pos - 2] == '*'));
    // Whatever `///` lines were just read belong to this token — and so to
    // the declaration it begins.
    std::string doc = takeDoc();
    if (c == 'r' && (peek(1) == '"' || (peek(1) == '#' && peek(2) == '"'))) {
      t = lexString(/*raw=*/true);
    } else if (isIdentStart(c)) {
      t = lexIdentifierOrKeyword();
    } else if (isDigit(c)) {
      // After a `.` a number is always a tuple index, never a fraction.
      bool afterDot = !tokens.empty() && tokens.back().Kind == Tok::Dot;
      if (afterDot) {
        size_t start = Pos;
        uint64_t v = 0;
        while (isDigit(peek()))
          v = v * 10 + static_cast<uint64_t>(advance() - '0');
        t.Kind = Tok::IntLiteral;
        t.IntValue = v;
        t.Range = rangeFrom(start);
        t.Text = std::string(Buf + start, Pos - start);
      } else {
        t = lexNumber();
      }
    } else if (c == '"') {
      t = lexString(/*raw=*/false);
    } else if (c == '\'') {
      t = lexChar();
    } else {
      t = lexPunctuation();
    }

    t.AtLineStart = atLineStart || CrossedNewline;
    t.SpacingKnown = true;
    t.SpaceBefore = spaced;
    atLineStart = false;
    CrossedNewline = false;
    t.Doc = std::move(doc);
    tokens.push_back(std::move(t));
  }

  Token eofTok;
  eofTok.Kind = Tok::EndOfFile;
  eofTok.Range = SourceRange(locAt(Len), locAt(Len));
  eofTok.AtLineStart = atLineStart;
  tokens.push_back(std::move(eofTok));
  return tokens;
}

} // namespace rune
