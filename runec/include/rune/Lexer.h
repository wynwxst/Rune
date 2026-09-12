//===- Lexer.h - Rune tokenizer --------------------------------*- C++ -*-===//
//
// Produces the full token vector for one source file up front. Newlines are
// only materialised where they can act as statement terminators: the lexer
// tracks bracket nesting and the previous token so that
//
//     let total = a +
//                 b
//
// stays a single statement while
//
//     let a = 1
//     let b = 2
//
// does not.
//===----------------------------------------------------------------------===//
#ifndef RUNE_LEXER_H
#define RUNE_LEXER_H

#include "rune/Diagnostics.h"
#include "rune/Token.h"

#include <vector>

namespace rune {

class Lexer {
public:
  Lexer(const SourceManager &sm, DiagnosticEngine &diags, unsigned fileID);

  /// Tokenises the whole buffer. The result always ends with EndOfFile.
  std::vector<Token> tokenize();

public:
  /// `///` lines seen since the last declaration took them. The parser drains
  /// this when it starts a declaration, so the comment lands on the thing it
  /// was written above.
  std::string PendingDoc;
  bool SawDocThisRun = false;
  /// Takes the pending text and clears it.
  std::string takeDoc() {
    std::string d;
    d.swap(PendingDoc);
    SawDocThisRun = false;
    return d;
  }

private:
  const SourceManager &SM;
  DiagnosticEngine &Diags;
  const SourceFile &File;
  const char *Buf;
  size_t Len;
  size_t Pos = 0;
  /// Nesting depth of () and []; inside them newlines are pure whitespace.
  unsigned GroupDepth = 0;
  /// Set when a physical line break was crossed without emitting a Newline
  /// token. The parser still accepts it as the end of a statement, which is
  /// what lets a line legitimately end on an operator-like token (`import
  /// a::b::*`) without a semicolon.
  bool CrossedNewline = false;

  char peek(size_t n = 0) const { return Pos + n < Len ? Buf[Pos + n] : '\0'; }
  bool eof() const { return Pos >= Len; }
  char advance() { return Buf[Pos++]; }
  bool match(char c) {
    if (peek() == c) { ++Pos; return true; }
    return false;
  }

  SourceLoc locAt(size_t off) const { return SM.locForFileOffset(File.ID, static_cast<uint32_t>(off)); }
  SourceRange rangeFrom(size_t start) const {
    return SourceRange(locAt(start), locAt(Pos));
  }

  /// Skips spaces, comments and insignificant newlines. Returns true if a
  /// significant newline should be emitted before the next token.
  bool skipTrivia(bool prevCanEnd, size_t &newlineStart);

  Token lexIdentifierOrKeyword();
  Token lexNumber();
  Token lexString(bool raw);
  Token lexChar();
  Token lexPunctuation();

  /// Consumes an escape sequence after the backslash; appends to `out`.
  void lexEscape(std::string &out, size_t escStart);
  /// Reads a `i32` / `f64` style suffix directly after a numeric literal.
  std::string lexNumericSuffix();
};

} // namespace rune

#endif
