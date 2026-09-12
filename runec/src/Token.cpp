#include "rune/Token.h"

#include <unordered_map>

namespace rune {

static const char *const kSpellings[] = {
#define TOK(Name, Spelling) Spelling,
#include "rune/TokenKinds.def"
#undef TOK
};

static const char *const kNames[] = {
#define TOK(Name, Spelling) #Name,
#include "rune/TokenKinds.def"
#undef TOK
};

const char *tokenSpelling(Tok k) {
  unsigned i = static_cast<unsigned>(k);
  return i < static_cast<unsigned>(Tok::NUM_TOKENS) ? kSpellings[i] : "<invalid>";
}

const char *tokenName(Tok k) {
  unsigned i = static_cast<unsigned>(k);
  return i < static_cast<unsigned>(Tok::NUM_TOKENS) ? kNames[i] : "Invalid";
}

bool isKeyword(Tok k) { return k >= Tok::KwFn && k <= Tok::KwNil; }

bool canEndStatement(Tok k) {
  switch (k) {
  case Tok::Identifier:
  case Tok::IntLiteral:
  case Tok::FloatLiteral:
  case Tok::StringLiteral:
  case Tok::CharLiteral:
  case Tok::RParen:
  case Tok::RBracket:
  case Tok::RBrace:
  case Tok::Question:
  case Tok::Underscore:
  case Tok::KwSelfValue:
  case Tok::KwSelfType:
  case Tok::KwSuper:
  case Tok::KwTrue:
  case Tok::KwFalse:
  case Tok::KwNil:
  case Tok::KwReturn:
  case Tok::KwBreak:
  case Tok::KwContinue:
    return true;
  default:
    return false;
  }
}

Tok keywordKind(const std::string &text) {
  static const std::unordered_map<std::string, Tok> *kMap = [] {
    auto *m = new std::unordered_map<std::string, Tok>();
#define TOK(Name, Spelling)                                                    \
  if (isKeyword(Tok::Name))                                                    \
    m->emplace(Spelling, Tok::Name);
#include "rune/TokenKinds.def"
#undef TOK
    return m;
  }();
  auto it = kMap->find(text);
  return it == kMap->end() ? Tok::Identifier : it->second;
}

} // namespace rune
