//===- Token.h - Rune token kinds -------------------------------*- C++ -*-===//
#ifndef RUNE_TOKEN_H
#define RUNE_TOKEN_H

#include "rune/Source.h"

#include <cstdint>
#include <string>

namespace rune {

enum class Tok : uint8_t {
#define TOK(Name, Spelling) Name,
#include "rune/TokenKinds.def"
#undef TOK
  NUM_TOKENS
};

/// Printable spelling, e.g. `->` or `fn`. Literal kinds return a category name
/// such as `<integer literal>` so diagnostics read naturally.
const char *tokenSpelling(Tok k);

/// Short name used in "expected X, got Y" messages.
const char *tokenName(Tok k);

bool isKeyword(Tok k);

/// True if a statement may legally end on this token; drives newline inference.
bool canEndStatement(Tok k);

/// Maps an identifier to its keyword kind, or Tok::Identifier.
Tok keywordKind(const std::string &text);

struct Token {
  Tok Kind = Tok::Unknown;
  SourceRange Range;

  /// Identifiers, string literals and number suffixes.
  std::string Text;
  /// Decoded value for integer / character literals.
  uint64_t IntValue = 0;
  /// Decoded value for float literals.
  double FloatValue = 0.0;
  /// `i32` in `10i32`, `f32` in `1.5f32`; empty when unsuffixed.
  std::string Suffix;
  /// `///` lines written immediately above this token. Only the first token
  /// of a declaration carries them, which is where the parser reads them.
  std::string Doc;
  /// True when a newline (or the start of file) precedes this token; used to
  /// give better recovery hints.
  bool AtLineStart = false;
  /// Set by the lexer: whether whitespace (or a comment) came right before
  /// this token in the text it was read from. Tokens the compiler makes
  /// itself leave `SpacingKnown` false. Printing tokens back — a macro's
  /// expansion in a note, `stringify!` — keeps the spacing that was written.
  bool SpacingKnown = false;
  bool SpaceBefore = false;

  bool is(Tok k) const { return Kind == k; }
  bool isNot(Tok k) const { return Kind != k; }
  template <typename... Ts> bool isAny(Tok k, Ts... rest) const {
    return is(k) || (... || is(rest));
  }
  SourceLoc loc() const { return Range.begin(); }
};

} // namespace rune

#endif
