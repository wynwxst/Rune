//===- Parser.cpp - Recursive-descent parser for Rune ----------*- C++ -*-===//

#include "rune/Parser.h"

#include "rune/Macro.h"

#include "rune/Lexer.h"

#include <algorithm>

namespace rune {

//===----------------------------------------------------------------------===//
// Operator tables
//===----------------------------------------------------------------------===//

const char *binaryOpSpelling(BinaryOp op) {
  switch (op) {
  case BinaryOp::Add: return "+";
  case BinaryOp::Sub: return "-";
  case BinaryOp::Mul: return "*";
  case BinaryOp::Div: return "/";
  case BinaryOp::Rem: return "%";
  case BinaryOp::BitAnd: return "&";
  case BinaryOp::BitOr: return "|";
  case BinaryOp::BitXor: return "^";
  case BinaryOp::Shl: return "<<";
  case BinaryOp::Shr: return ">>";
  case BinaryOp::LogicalAnd: return "&&";
  case BinaryOp::LogicalOr: return "||";
  case BinaryOp::Coalesce: return "??";
  case BinaryOp::Eq: return "==";
  case BinaryOp::Ne: return "!=";
  case BinaryOp::Lt: return "<";
  case BinaryOp::Le: return "<=";
  case BinaryOp::Gt: return ">";
  case BinaryOp::Ge: return ">=";
  }
  return "?";
}

const char *unaryOpSpelling(UnaryOp op) {
  switch (op) {
  case UnaryOp::Neg: return "-";
  case UnaryOp::Not: return "!";
  case UnaryOp::BitNot: return "~";
  }
  return "?";
}

const char *assignOpSpelling(AssignOp op) {
  switch (op) {
  case AssignOp::Assign: return "=";
  case AssignOp::Add: return "+=";
  case AssignOp::Sub: return "-=";
  case AssignOp::Mul: return "*=";
  case AssignOp::Div: return "/=";
  case AssignOp::Rem: return "%=";
  case AssignOp::BitAnd: return "&=";
  case AssignOp::BitOr: return "|=";
  case AssignOp::BitXor: return "^=";
  case AssignOp::Shl: return "<<=";
  case AssignOp::Shr: return ">>=";
  }
  return "=";
}

const char *binaryOpMarkMethod(BinaryOp op) {
  switch (op) {
  case BinaryOp::Add: return "add";
  case BinaryOp::Sub: return "sub";
  case BinaryOp::Mul: return "mul";
  case BinaryOp::Div: return "div";
  case BinaryOp::Rem: return "rem";
  case BinaryOp::BitAnd: return "bitand";
  case BinaryOp::BitOr: return "bitor";
  case BinaryOp::BitXor: return "bitxor";
  case BinaryOp::Shl: return "shl";
  case BinaryOp::Shr: return "shr";
  case BinaryOp::Eq:
  case BinaryOp::Ne: return "eq";
  case BinaryOp::Lt:
  case BinaryOp::Le:
  case BinaryOp::Gt:
  case BinaryOp::Ge: return "cmp";
  default: return nullptr;
  }
}

const char *unaryOpMarkMethod(UnaryOp op) {
  switch (op) {
  case UnaryOp::Neg: return "neg";
  case UnaryOp::Not: return "not";
  case UnaryOp::BitNot: return "bitnot";
  }
  return nullptr;
}

BinaryOp assignOpToBinary(AssignOp op) {
  switch (op) {
  case AssignOp::Add: return BinaryOp::Add;
  case AssignOp::Sub: return BinaryOp::Sub;
  case AssignOp::Mul: return BinaryOp::Mul;
  case AssignOp::Div: return BinaryOp::Div;
  case AssignOp::Rem: return BinaryOp::Rem;
  case AssignOp::BitAnd: return BinaryOp::BitAnd;
  case AssignOp::BitOr: return BinaryOp::BitOr;
  case AssignOp::BitXor: return BinaryOp::BitXor;
  case AssignOp::Shl: return BinaryOp::Shl;
  case AssignOp::Shr: return BinaryOp::Shr;
  case AssignOp::Assign: return BinaryOp::Add; // never used
  }
  return BinaryOp::Add;
}

bool isComparison(BinaryOp op) {
  switch (op) {
  case BinaryOp::Eq: case BinaryOp::Ne: case BinaryOp::Lt:
  case BinaryOp::Le: case BinaryOp::Gt: case BinaryOp::Ge:
    return true;
  default:
    return false;
  }
}

namespace {

/// Higher binds tighter. -1 means "not a binary operator".
int binaryPrec(Tok k) {
  switch (k) {
  case Tok::QuestionQuestion: return 1;
  case Tok::PipePipe: return 2;
  case Tok::AmpAmp: return 3;
  case Tok::EqEq: case Tok::NotEq: case Tok::Lt:
  case Tok::LtEq: case Tok::Gt: case Tok::GtEq: return 4;
  case Tok::Pipe: return 5;
  case Tok::Caret: return 6;
  case Tok::Amp: return 7;
  case Tok::Shl: case Tok::Shr: return 8;
  case Tok::Plus: case Tok::Minus: return 9;
  case Tok::Star: case Tok::Slash: case Tok::Percent: return 10;
  default: return -1;
  }
}

BinaryOp binaryOpFor(Tok k) {
  switch (k) {
  case Tok::Plus: return BinaryOp::Add;
  case Tok::Minus: return BinaryOp::Sub;
  case Tok::Star: return BinaryOp::Mul;
  case Tok::Slash: return BinaryOp::Div;
  case Tok::Percent: return BinaryOp::Rem;
  case Tok::Amp: return BinaryOp::BitAnd;
  case Tok::Pipe: return BinaryOp::BitOr;
  case Tok::Caret: return BinaryOp::BitXor;
  case Tok::Shl: return BinaryOp::Shl;
  case Tok::Shr: return BinaryOp::Shr;
  case Tok::AmpAmp: return BinaryOp::LogicalAnd;
  case Tok::PipePipe: return BinaryOp::LogicalOr;
  case Tok::QuestionQuestion: return BinaryOp::Coalesce;
  case Tok::EqEq: return BinaryOp::Eq;
  case Tok::NotEq: return BinaryOp::Ne;
  case Tok::Lt: return BinaryOp::Lt;
  case Tok::LtEq: return BinaryOp::Le;
  case Tok::Gt: return BinaryOp::Gt;
  case Tok::GtEq: return BinaryOp::Ge;
  default: return BinaryOp::Add;
  }
}

bool assignOpFor(Tok k, AssignOp &out) {
  switch (k) {
  case Tok::Eq: out = AssignOp::Assign; return true;
  case Tok::PlusEq: out = AssignOp::Add; return true;
  case Tok::MinusEq: out = AssignOp::Sub; return true;
  case Tok::StarEq: out = AssignOp::Mul; return true;
  case Tok::SlashEq: out = AssignOp::Div; return true;
  case Tok::PercentEq: out = AssignOp::Rem; return true;
  case Tok::AmpEq: out = AssignOp::BitAnd; return true;
  case Tok::PipeEq: out = AssignOp::BitOr; return true;
  case Tok::CaretEq: out = AssignOp::BitXor; return true;
  case Tok::ShlEq: out = AssignOp::Shl; return true;
  case Tok::ShrEq: out = AssignOp::Shr; return true;
  default: return false;
  }
}

/// Tokens that can begin an expression; used to decide whether an optional
/// operand (a `return` value, an open range bound) is present.
bool startsExpression(Tok k) {
  switch (k) {
  case Tok::Identifier: case Tok::IntLiteral: case Tok::FloatLiteral:
  case Tok::StringLiteral: case Tok::CharLiteral:
  case Tok::LParen: case Tok::LBracket: case Tok::LBrace:
  case Tok::Minus: case Tok::Bang: case Tok::Tilde: case Tok::Amp:
  case Tok::Star: case Tok::PipePipe: case Tok::At:
  case Tok::KwSelfValue: case Tok::KwSelfType: case Tok::KwSuper:
  case Tok::KwTrue: case Tok::KwFalse: case Tok::KwNil:
  case Tok::KwIf: case Tok::KwMatch: case Tok::KwLoop: case Tok::KwWhile:
  case Tok::KwFor: case Tok::KwReturn: case Tok::KwBreak:
  case Tok::KwContinue: case Tok::KwUnsafe: case Tok::KwMut:
    return true;
  default:
    return false;
  }
}

/// Keywords that begin a declaration.
bool startsDecl(Tok k) {
  switch (k) {
  case Tok::KwFn: case Tok::KwStruct: case Tok::KwEnum: case Tok::KwClass:
  case Tok::KwMark: case Tok::KwBind: case Tok::KwExtend: case Tok::KwImport:
  case Tok::KwExtern: case Tok::KwType: case Tok::KwPub:
    return true;
  default:
    return false;
  }
}

template <typename T> std::unique_ptr<T> makeNode(SourceRange r) {
  auto n = std::make_unique<T>();
  n->Range = r;
  return n;
}

} // namespace

//===----------------------------------------------------------------------===//
// Construction and token utilities
//===----------------------------------------------------------------------===//

Parser::Parser(const SourceManager &sm, DiagnosticEngine &diags, unsigned fileID,
               std::string moduleName, const MacroTable *macros)
    : Parser(sm, diags, fileID, std::move(moduleName),
             Lexer(sm, diags, fileID).tokenize(), macros) {}

Parser::Parser(const SourceManager &sm, DiagnosticEngine &diags, unsigned fileID,
               std::string moduleName, std::vector<Token> tokens,
               const MacroTable *macros)
    : SM(sm), Diags(diags), FileID(fileID), ModuleName(std::move(moduleName)),
      Toks(std::move(tokens)) {
  // Macros are a rewrite over tokens, done before anything is parsed: by the
  // time the grammar sees the stream there are none left in it.
  if (macros) {
    // The table already holds this file's definitions, gathered along with
    // every other file's, so here they are only taken out of the way.
    MacroTable ignored;
    collectMacros(Toks, diags, ignored, /*record=*/false);
    expandMacros(Toks, diags, *macros, ModuleName);
  } else {
    expandMacros(Toks, diags);
  }
}

SourceRange Parser::rangeFrom(size_t startTok) const {
  SourceRange begin = Toks[std::min(startTok, Toks.size() - 1)].Range;
  size_t endIdx = Pos > 0 ? Pos - 1 : 0;
  return begin.merge(Toks[std::min(endIdx, Toks.size() - 1)].Range);
}

bool Parser::expect(Tok k, const char *context) {
  if (match(k))
    return true;

  const Token &t = cur();
  std::string got = t.is(Tok::EndOfFile)  ? "end of file"
                    : t.is(Tok::Newline)  ? "end of line"
                    : t.Text.empty()      ? tokenSpelling(t.Kind)
                                          : t.Text;
  Diags.error(t.Range, "Expected '{}' — Got: '{}'", tokenSpelling(k), got)
      .note(fmt("while parsing {}", context).c_str())
      .code(100);
  return false;
}

bool Parser::consumeCloseAngle() {
  Token &t = Toks[Pos];
  switch (t.Kind) {
  case Tok::Gt:
    advance();
    return true;
  case Tok::Shr:
    // Consume the first `>` and leave a single `>` behind for the outer list.
    t.Kind = Tok::Gt;
    t.Range = SourceRange(t.Range.begin().offsetBy(1), t.Range.end());
    t.Text = ">";
    return true;
  case Tok::GtEq:
    t.Kind = Tok::Eq;
    t.Range = SourceRange(t.Range.begin().offsetBy(1), t.Range.end());
    t.Text = "=";
    return true;
  case Tok::ShrEq:
    t.Kind = Tok::GtEq;
    t.Range = SourceRange(t.Range.begin().offsetBy(1), t.Range.end());
    t.Text = ">=";
    return true;
  default:
    return expect(Tok::Gt, "a generic argument list");
  }
}

bool Parser::expectTerminator(const char *context) {
  LastHadSemi = false;
  if (check(Tok::Semi)) {
    advance();
    LastHadSemi = true;
    return true;
  }
  if (check(Tok::Newline)) {
    advance();
    return true;
  }
  if (check(Tok::RBrace) || atEnd())
    return true;
  // The lexer only materialises a newline where one can terminate a
  // statement, but a construct may legitimately end on a token that cannot
  // (`import a::b::*`). Crossing a line break is enough.
  if (cur().AtLineStart)
    return true;

  const Token &t = cur();
  Diags.error(t.Range, "Expected '{}' — Got: '{}'", "end of statement",
              t.Text.empty() ? tokenSpelling(t.Kind) : t.Text)
      .note("statements end at a line break, or explicitly with `;`")
      .note(fmt("while parsing {}", context).c_str())
      .code(101);
  synchronize();
  return false;
}

void Parser::synchronize() {
  // Skip tokens until something that plausibly begins a new statement, keeping
  // brace nesting balanced so we do not fall out of the enclosing block.
  int depth = 0;
  while (!atEnd()) {
    if (check(Tok::LBrace)) ++depth;
    if (check(Tok::RBrace)) {
      if (depth == 0)
        return;
      --depth;
    }
    if (depth == 0 && (check(Tok::Newline) || check(Tok::Semi))) {
      advance();
      return;
    }
    if (depth == 0 && startsDecl(cur().Kind) && cur().AtLineStart)
      return;
    advance();
  }
}

//===----------------------------------------------------------------------===//
// Module and declarations
//===----------------------------------------------------------------------===//

/// `@link("m")` and `@linkpath("...")` at the very top of a file, before any
/// declaration. They belong to the file rather than to anything in it, which
/// is why they may only appear there.
void Parser::parseFileDirectives(Module &mod) {
  for (;;) {
    if (!check(Tok::At))
      return;
    // `type` is a keyword, so it does not arrive as an identifier.
    if (!peek(1).is(Tok::Identifier) && !peek(1).is(Tok::KwType))
      return;
    std::string name =
        peek(1).is(Tok::KwType) ? std::string("type") : peek(1).Text;
    if (name != "link" && name != "linkpath" && name != "type")
      return;
    if (name == "type") {
      // `@type` decides what the file produces, which is what everything else
      // about linking depends on, so it comes before them.
      if (!mod.LinkLibraries.empty() || !mod.LinkPaths.empty())
        Diags.error(peek(0).Range, "`@type` must come before `@link` and "
                                   "`@linkpath`")
            .note("what the file produces decides how the rest is used")
            .code(109);
      if (!mod.DeclaredOutput.empty())
        Diags.error(peek(0).Range, "`@type` is already set for this file")
            .code(109);
      size_t tstart = Pos;
      advance();
      advance();
      if (expect(Tok::LParen, "a type directive")) {
        if (check(Tok::Identifier)) {
          mod.DeclaredOutput = cur().Text;
          mod.DeclaredOutputRange = cur().Range;
          advance();
        } else {
          Diags.error(cur().Range, "`@type` takes a name")
              .note("one of Executable, Library, Object, Assembly or LLVM")
              .code(109);
        }
        expect(Tok::RParen, "a type directive");
      }
      (void)tstart;
      skipSeparators();
      continue;
    }
    size_t start = Pos;
    advance(); // @
    advance(); // link / linkpath
    if (!expect(Tok::LParen, "a link directive")) {
      skipSeparators();
      continue;
    }
    bool any = false;
    while (!check(Tok::RParen) && !atEnd()) {
      skipNewlines();
      if (check(Tok::StringLiteral)) {
        if (name == "link")
          mod.LinkLibraries.push_back(cur().Text);
        else
          mod.LinkPaths.push_back(cur().Text);
        any = true;
        advance();
      } else {
        Diags.error(cur().Range, "`@{}` takes string arguments", name)
            .note(name == "link"
                      ? "write `@link(\"m\")` — the name you would pass to `-l`"
                      : "write `@linkpath(\"/usr/local/lib\")`")
            .code(107);
        break;
      }
      skipNewlines();
      if (!match(Tok::Comma))
        break;
    }
    expect(Tok::RParen, "a link directive");
    if (!any)
      Diags.error(rangeFrom(start), "`@{}` needs at least one name", name)
          .code(107);
    skipSeparators();
  }
}

std::unique_ptr<Module> Parser::parseModule() {
  auto mod = std::make_unique<Module>();
  mod->Name = ModuleName;
  mod->FileID = FileID;
  mod->Path = SM.file(FileID).Path;

  skipSeparators();
  parseFileDirectives(*mod);
  while (!atEnd()) {
    size_t before = Pos;
    if (DeclPtr d = parseTopLevelDecl()) {
      d->ModulePath = ModuleName;
      mod->Decls.push_back(std::move(d));
    }
    skipSeparators();
    if (Pos == before) { // no progress: force forward motion
      advance();
      skipSeparators();
    }
    if (Diags.reachedLimit())
      break;
  }
  return mod;
}

std::vector<Attribute> Parser::parseAttributes() {
  std::vector<Attribute> attrs;
  while (check(Tok::At)) {
    // `@function(...)` in type position is a function type, never a decorator,
    // but decorators only appear before declarations so there is no clash.
    size_t start = Pos;
    advance();
    Attribute a;
    if (check(Tok::Identifier)) {
      a.Name = cur().Text;
      advance();
    } else if (isKeyword(cur().Kind)) {
      a.Name = tokenSpelling(cur().Kind);
      advance();
    } else {
      Diags.error(cur().Range, "Expected '{}' — Got: '{}'", "decorator name",
                  cur().Text.empty() ? tokenSpelling(cur().Kind) : cur().Text)
          .note("decorators are written `@name` or `@name(arguments)`")
          .code(102);
      advance();
      continue;
    }
    if (check(Tok::LParen)) {
      advance();
      skipNewlines();
      while (!check(Tok::RParen) && !atEnd()) {
        a.Args.push_back(parseExpr());
        skipNewlines();
        if (!match(Tok::Comma))
          break;
        skipNewlines();
      }
      expect(Tok::RParen, "a decorator argument list");
    }
    a.Range = rangeFrom(start);
    attrs.push_back(std::move(a));
    skipSeparators();
  }
  return attrs;
}

DeclPtr Parser::parseTopLevelDecl() {
  // `///` lines ride on the first token of the declaration — which is this
  // one, before any decorators are consumed.
  std::string doc = cur().Doc;
  std::vector<Attribute> attrs = parseAttributes();
  // These belong to the file, not to a declaration, so they are only read at
  // the very top. Reaching one here means it came too late.
  for (const Attribute &a : attrs)
    if (a.Name == "link" || a.Name == "linkpath" || a.Name == "type")
      Diags.error(a.Range, "`@{}` must be at the top of the file", a.Name)
          .note("it applies to the whole file, so it goes before every "
                "declaration — imports included")
          .code(107);
  bool isPublic = false;
  if (check(Tok::KwPub)) {
    isPublic = true;
    advance();
    skipNewlines();
    // `pub` may also precede decorators.
    if (check(Tok::At)) {
      auto more = parseAttributes();
      for (auto &a : more)
        attrs.push_back(std::move(a));
    }
  }
  DeclPtr d = parseDecl(std::move(attrs), isPublic);
  // `@Doc` wins when both are present: it was written for the reader, the
  // comment may only have been written for whoever is editing the code.
  if (d && !doc.empty())
    d->Doc = std::move(doc);
  return d;
}

DeclPtr Parser::parseDecl(std::vector<Attribute> attrs, bool isPublic) {
  switch (cur().Kind) {
  case Tok::KwFn:
    return parseFunction(std::move(attrs), isPublic, /*allowNoBody=*/false);
  case Tok::KwStruct:
    return parseStruct(std::move(attrs), isPublic);
  case Tok::KwEnum:
    return parseEnum(std::move(attrs), isPublic);
  case Tok::KwClass:
    return parseClass(std::move(attrs), isPublic);
  case Tok::KwMark:
    return parseMark(std::move(attrs), isPublic);
  case Tok::KwBind:
    return parseBind(std::move(attrs), isPublic);
  case Tok::KwExtend:
    return parseExtend(std::move(attrs), isPublic);
  case Tok::KwImport:
    return parseImport(std::move(attrs), isPublic);
  case Tok::KwExtern:
    return parseExtern(std::move(attrs), isPublic);
  case Tok::KwType:
    return parseTypeAlias(std::move(attrs), isPublic);
  case Tok::KwGlobal:
  case Tok::KwVar:
  case Tok::KwMut:
  case Tok::KwLet:
    return parseGlobalVar(std::move(attrs), isPublic);
  default:
    break;
  }

  // A bare `name: Type = value` at file scope is a global.
  if (looksLikeTypedBinding() || (check(Tok::Identifier) && peek(1).is(Tok::Eq)))
    return parseGlobalVar(std::move(attrs), isPublic);

  const Token &t = cur();
  Diags.error(t.Range, "Expected '{}' — Got: '{}'", "a declaration",
              t.Text.empty() ? tokenSpelling(t.Kind) : t.Text)
      .note("top level items are fn, struct, enum, class, mark, bind, extend, "
            "import, extern, type or a global variable")
      .code(103);
  synchronize();
  return nullptr;
}

/// A bound may name a mark, or an operator the argument has to overload:
/// `T: operator::cmp`, `T: operator::"+"`.
TypeReprPtr Parser::parseBound() {
  if (!check(Tok::KwOperator))
    return parseType();
  size_t start = Pos;
  advance();
  auto repr = makeNode<NamedTypeRepr>(here());
  repr->Path.push_back("operator");
  if (expect(Tok::ColonColon, "an operator bound")) {
    if (check(Tok::StringLiteral) || check(Tok::Identifier)) {
      repr->Path.push_back(cur().Text);
      advance();
    } else if (isKeyword(cur().Kind)) {
      repr->Path.push_back(tokenSpelling(cur().Kind));
      advance();
    } else {
      expect(Tok::Identifier, "an operator name");
    }
  }
  repr->Range = rangeFrom(start);
  repr->NameRange = repr->Range;
  return repr;
}

std::vector<GenericParam> Parser::parseGenericParams() {
  std::vector<GenericParam> params;
  if (!check(Tok::Lt))
    return params;
  advance();
  skipNewlines();
  while (!check(Tok::Gt) && !check(Tok::Shr) && !atEnd()) {
    GenericParam p;
    size_t start = Pos;
    if (check(Tok::Identifier)) {
      p.Name = cur().Text;
      advance();
    } else {
      expect(Tok::Identifier, "a generic parameter list");
      break;
    }
    if (match(Tok::Colon)) {
      do {
        skipNewlines();
        p.Bounds.push_back(parseBound());
      } while (match(Tok::Plus));
    }
    p.Range = rangeFrom(start);
    params.push_back(std::move(p));
    skipNewlines();
    if (!match(Tok::Comma))
      break;
    skipNewlines();
  }
  consumeCloseAngle();
  return params;
}

std::vector<WhereClause> Parser::parseWhereClauses() {
  std::vector<WhereClause> clauses;
  // A `where` clause conventionally sits on its own line after the signature,
  // so look past the line break before deciding it is absent.
  size_t save = Pos;
  skipNewlines();
  if (!check(Tok::KwWhere)) {
    Pos = save;
    return clauses;
  }
  advance();
  skipNewlines();
  for (;;) {
    WhereClause w;
    size_t start = Pos;
    w.Subject = parseType();
    if (expect(Tok::Colon, "a where clause")) {
      do {
        skipNewlines();
        w.Bounds.push_back(parseBound());
      } while (match(Tok::Plus));
    }
    w.Range = rangeFrom(start);
    clauses.push_back(std::move(w));
    if (!match(Tok::Comma))
      break;
    skipNewlines();
    if (check(Tok::LBrace))
      break;
  }
  return clauses;
}

/// `a.b.c` — a dotted path of names, as a view entry or the tail of a `from`
/// place. The first step may be `self`.
bool Parser::parseFieldPath(std::vector<std::string> &path, SourceRange &range) {
  size_t start = Pos;
  if (check(Tok::KwSelfValue)) {
    path.push_back("self");
    advance();
  } else if (check(Tok::Identifier)) {
    path.push_back(cur().Text);
    advance();
  } else {
    expect(Tok::Identifier, "a place");
    return false;
  }
  while (check(Tok::Dot)) {
    advance();
    if (check(Tok::Identifier)) {
      path.push_back(cur().Text);
      advance();
    } else if (check(Tok::IntLiteral)) {
      path.push_back(cur().Text);          // a tuple field, by index
      advance();
    } else {
      expect(Tok::Identifier, "a field path");
      return false;
    }
  }
  range = rangeFrom(start);
  return true;
}

/// `{ counter, stats.hits }` after a parameter: the view it promises to stay
/// within. Nothing is consumed when no brace follows.
void Parser::parseView(Param &p) {
  if (!check(Tok::LBrace))
    return;
  advance();
  p.HasView = true;
  skipNewlines();
  while (!check(Tok::RBrace) && !atEnd()) {
    FieldPathRepr f;
    if (!parseFieldPath(f.Path, f.Range)) {
      synchronize();
      break;
    }
    p.View.push_back(std::move(f));
    skipNewlines();
    if (!match(Tok::Comma))
      break;
    skipNewlines();
  }
  expect(Tok::RBrace, "a view");
}

/// `from place`, `from (a, b.c)`, `from global` after a reference type. The
/// word is contextual: it is only looked for here, so a parameter or a field
/// may still be called `from`.
std::unique_ptr<OriginClause> Parser::parseOriginClause() {
  if (!check(Tok::Identifier) || cur().Text != "from")
    return nullptr;
  size_t start = Pos;
  advance();
  auto clause = std::make_unique<OriginClause>();
  auto one = [&]() {
    OriginPlace place;
    if (check(Tok::KwGlobal)) {
      place.Path.push_back("global");
      place.Range = cur().Range;
      advance();
    } else if (!parseFieldPath(place.Path, place.Range)) {
      return false;
    }
    clause->Places.push_back(std::move(place));
    return true;
  };
  if (check(Tok::LParen)) {
    advance();
    skipNewlines();
    while (!check(Tok::RParen) && !atEnd()) {
      if (!one())
        break;
      skipNewlines();
      if (!match(Tok::Comma))
        break;
      skipNewlines();
    }
    expect(Tok::RParen, "a `from` clause");
  } else {
    one();
  }
  clause->Range = rangeFrom(start);
  return clause;
}

bool Parser::parseParamList(std::vector<Param> &out, bool allowSelf,
                            bool &isVariadic) {
  isVariadic = false;
  if (!expect(Tok::LParen, "a parameter list"))
    return false;
  skipNewlines();

  bool first = true;
  while (!check(Tok::RParen) && !atEnd()) {
    Param p;
    size_t start = Pos;

    // `self`, `&self`, `&var self`, `var self`
    bool sawAmp = check(Tok::Amp);
    size_t save = Pos;
    if (sawAmp)
      advance();
    bool sawMut = false;
    if (check(Tok::KwVar) || check(Tok::KwMut)) {
      sawMut = true;
      advance();
    }
    if (check(Tok::KwSelfValue)) {
      if (!allowSelf || !first) {
        Diags.error(cur().Range, "`self` may only appear as the first parameter")
            .note("free functions have no `self`; methods take it first")
            .code(104);
      }
      p.IsSelf = true;
      p.SelfByRef = sawAmp;
      p.SelfMutable = sawMut || !sawAmp;
      p.Name = "self";
      advance();
      parseView(p);
      p.Range = rangeFrom(start);
      out.push_back(std::move(p));
      first = false;
      skipNewlines();
      if (!match(Tok::Comma))
        break;
      skipNewlines();
      continue;
    }
    Pos = save; // not a self parameter after all

    if (check(Tok::Ellipsis)) {
      advance();
      isVariadic = true;
      skipNewlines();
      if (check(Tok::Comma)) {
        Diags.error(cur().Range, "`...` must be the last parameter").code(105);
        advance();
        skipNewlines();
        continue;
      }
      break;
    }

    if (check(Tok::KwVar) || check(Tok::KwMut)) {
      p.IsMutable = true;
      advance();
    }
    if (check(Tok::Identifier) || check(Tok::Underscore)) {
      p.Name = check(Tok::Underscore) ? "_" : cur().Text;
      advance();
    } else {
      expect(Tok::Identifier, "a parameter list");
      synchronize();
      break;
    }
    if (expect(Tok::Colon, "a parameter declaration"))
      p.TypeAnnotation = parseType();
    parseView(p);
    if (match(Tok::Eq))
      p.DefaultValue = parseExpr();

    p.Range = rangeFrom(start);
    out.push_back(std::move(p));
    first = false;
    skipNewlines();
    if (!match(Tok::Comma))
      break;
    skipNewlines();
  }
  return expect(Tok::RParen, "a parameter list");
}

std::unique_ptr<FunctionDecl> Parser::parseFunction(std::vector<Attribute> attrs,
                                                    bool isPublic,
                                                    bool allowNoBody,
                                                    bool implicitFnKeyword) {
  size_t start = Pos;
  if (!implicitFnKeyword)
    expect(Tok::KwFn, "a function declaration");

  auto fn = makeNode<FunctionDecl>(here());
  fn->Attrs = std::move(attrs);
  fn->IsPublic = isPublic;

  if (check(Tok::Identifier)) {
    fn->Name = cur().Text;
    fn->NameRange = cur().Range;
    advance();
  } else if (isKeyword(cur().Kind)) {
    // Allows methods named after keywords in bind blocks (`fn to(...)`).
    fn->Name = tokenSpelling(cur().Kind);
    fn->NameRange = cur().Range;
    advance();
  } else {
    expect(Tok::Identifier, "a function name");
  }

  fn->Generics = parseGenericParams();
  bool variadic = false;
  parseParamList(fn->Params, /*allowSelf=*/true, variadic);
  fn->IsVariadic = variadic;

  if (match(Tok::Arrow))
    fn->ReturnType = parseType();
  fn->WhereClauses = parseWhereClauses();

  for (const Param &p : fn->Params)
    if (p.IsSelf)
      fn->Flavour = FunctionFlavour::Method;
  if (fn->Name == "init")
    fn->Flavour = FunctionFlavour::Initialiser;
  else if (fn->Name == "deinit")
    fn->Flavour = FunctionFlavour::Deinitialiser;

  // Decorator-driven safety flags.
  for (const Attribute &a : fn->Attrs) {
    if (a.Name == "unsafe")
      fn->IsUnsafe = true;
    if (a.Name == "safe") {
      fn->IsSafeJustified = true;
      if (!a.Args.empty())
        if (auto *s = dyn_cast<StringLitExpr>(a.Args[0].get()))
          fn->SafetyReason = s->Value;
    }
    // `@zombie("reason")`: the borrow checker takes this body on trust. The
    // reason is checked for in Sema, the way `@safe` without one warns.
    if (a.Name == "zombie") {
      fn->IsZombieTrusted = true;
      if (!a.Args.empty())
        if (auto *s = dyn_cast<StringLitExpr>(a.Args[0].get()))
          fn->ZombieReason = s->Value;
    }
  }

  skipNewlines(); // allow the brace on its own line
  if (check(Tok::LBrace)) {
    fn->Body = parseBlock("a function body");
  } else if (!allowNoBody && !fn->hasAttr("intrinsic")) {
    Diags.error(cur().Range, "Expected '{}' — Got: '{}'", "{",
                cur().Text.empty() ? tokenSpelling(cur().Kind) : cur().Text)
        .note("functions need a body; mark requirements and extern "
              "declarations may omit it")
        .code(106);
  }

  fn->Range = rangeFrom(start);
  return fn;
}

std::unique_ptr<FieldDecl> Parser::parseField(bool isPublic) {
  size_t start = Pos;
  auto f = makeNode<FieldDecl>(here());
  f->IsPublic = isPublic;
  if (check(Tok::KwWeak)) {
    f->IsWeak = true;
    advance();
  }
  // `var` on a field is accepted and documents intent. Field mutability
  // actually follows the binding the value is reached through, as in Rust.
  if (check(Tok::KwVar) || check(Tok::KwMut))
    advance();
  if (check(Tok::Identifier)) {
    f->Name = cur().Text;
    f->NameRange = cur().Range;
    advance();
  } else {
    expect(Tok::Identifier, "a field declaration");
    return nullptr;
  }
  if (expect(Tok::Colon, "a field declaration"))
    f->TypeAnnotation = parseType();
  if (match(Tok::Eq))
    f->DefaultValue = parseExpr();
  f->Range = rangeFrom(start);
  return f;
}

std::unique_ptr<StructDecl> Parser::parseStruct(std::vector<Attribute> attrs,
                                                bool isPublic) {
  size_t start = Pos;
  advance(); // struct
  auto s = makeNode<StructDecl>(here());
  s->Attrs = std::move(attrs);
  s->IsPublic = isPublic;
  if (check(Tok::Identifier)) {
    s->Name = cur().Text;
    s->NameRange = cur().Range;
    advance();
  } else {
    expect(Tok::Identifier, "a struct name");
  }
  s->Generics = parseGenericParams();
  s->WhereClauses = parseWhereClauses();
  skipNewlines();
  if (!expect(Tok::LBrace, "a struct body")) {
    s->Range = rangeFrom(start);
    return s;
  }
  skipSeparators();
  unsigned index = 0;
  while (!check(Tok::RBrace) && !atEnd()) {
    std::string memberDoc = cur().Doc;
    auto memberAttrs = parseAttributes();
    bool fieldPublic = match(Tok::KwPub);
    if (check(Tok::KwFn)) {
      auto m = parseFunction(std::move(memberAttrs), fieldPublic, false);
      m->Parent = s.get();
      if (!memberDoc.empty() && m->Doc.empty()) m->Doc = memberDoc;
      s->Methods.push_back(std::move(m));
    } else if (auto f = parseField(fieldPublic)) {
      if (!memberDoc.empty() && f->Doc.empty()) f->Doc = memberDoc;
      f->Attrs = std::move(memberAttrs);
      f->Index = index++;
      f->Parent = s.get();
      s->Fields.push_back(std::move(f));
    } else {
      synchronize();
    }
    skipSeparators();
    if (match(Tok::Comma))
      skipSeparators();
  }
  expect(Tok::RBrace, "a struct body");
  s->Range = rangeFrom(start);
  return s;
}

std::unique_ptr<EnumDecl> Parser::parseEnum(std::vector<Attribute> attrs,
                                            bool isPublic) {
  size_t start = Pos;
  advance(); // enum
  auto e = makeNode<EnumDecl>(here());
  e->Attrs = std::move(attrs);
  e->IsPublic = isPublic;
  if (check(Tok::Identifier)) {
    e->Name = cur().Text;
    e->NameRange = cur().Range;
    advance();
  } else {
    expect(Tok::Identifier, "an enum name");
  }
  e->Generics = parseGenericParams();
  e->WhereClauses = parseWhereClauses();
  skipNewlines();
  if (!expect(Tok::LBrace, "an enum body")) {
    e->Range = rangeFrom(start);
    return e;
  }
  skipSeparators();
  unsigned index = 0;
  while (!check(Tok::RBrace) && !atEnd()) {
    auto memberAttrs = parseAttributes();
    if (check(Tok::KwFn) || (check(Tok::KwPub) && peek(1).is(Tok::KwFn))) {
      bool mPub = match(Tok::KwPub);
      auto m = parseFunction(std::move(memberAttrs), mPub, false);
      m->Parent = e.get();
      e->Methods.push_back(std::move(m));
      skipSeparators();
      continue;
    }
    size_t vstart = Pos;
    auto v = makeNode<EnumVariantDecl>(here());
    v->Attrs = std::move(memberAttrs);
    if (check(Tok::Identifier)) {
      v->Name = cur().Text;
      v->NameRange = cur().Range;
      advance();
    } else {
      expect(Tok::Identifier, "an enum variant");
      synchronize();
      skipSeparators();
      continue;
    }
    if (check(Tok::LParen)) {
      advance();
      skipNewlines();
      v->Shape = VariantShape::Tuple;
      e->IsSimple = false;
      while (!check(Tok::RParen) && !atEnd()) {
        v->TupleTypes.push_back(parseType());
        skipNewlines();
        if (!match(Tok::Comma))
          break;
        skipNewlines();
      }
      expect(Tok::RParen, "an enum variant payload");
    } else if (check(Tok::LBrace)) {
      advance();
      skipSeparators();
      v->Shape = VariantShape::Struct;
      e->IsSimple = false;
      unsigned fi = 0;
      while (!check(Tok::RBrace) && !atEnd()) {
        bool fpub = match(Tok::KwPub);
        if (auto f = parseField(fpub)) {
          f->Index = fi++;
          v->Fields.push_back(std::move(f));
        } else {
          synchronize();
        }
        skipSeparators();
        if (match(Tok::Comma))
          skipSeparators();
      }
      expect(Tok::RBrace, "an enum variant body");
    } else if (match(Tok::Eq)) {
      v->Discriminant = parseExpr();
    }
    v->Index = index++;
    v->Range = rangeFrom(vstart);
    v->Parent = e.get();
    e->Variants.push_back(std::move(v));
    skipSeparators();
    if (match(Tok::Comma))
      skipSeparators();
  }
  expect(Tok::RBrace, "an enum body");
  e->Range = rangeFrom(start);
  return e;
}

std::unique_ptr<ClassDecl> Parser::parseClass(std::vector<Attribute> attrs,
                                              bool isPublic) {
  size_t start = Pos;
  advance(); // class
  auto c = makeNode<ClassDecl>(here());
  c->Attrs = std::move(attrs);
  c->IsPublic = isPublic;
  if (check(Tok::Identifier)) {
    c->Name = cur().Text;
    c->NameRange = cur().Range;
    advance();
  } else {
    expect(Tok::Identifier, "a class name");
  }
  c->Generics = parseGenericParams();
  if (match(Tok::Colon))
    c->SuperClass = parseType();
  c->WhereClauses = parseWhereClauses();
  skipNewlines();
  if (!expect(Tok::LBrace, "a class body")) {
    c->Range = rangeFrom(start);
    return c;
  }
  skipSeparators();
  unsigned index = 0;
  while (!check(Tok::RBrace) && !atEnd()) {
    std::string memberDoc = cur().Doc;
    auto memberAttrs = parseAttributes();
    bool memberPublic = match(Tok::KwPub);
    if (check(Tok::KwFn)) {
      auto m = parseFunction(std::move(memberAttrs), memberPublic, false);
      m->Parent = c.get();
      if (!memberDoc.empty()) m->Doc = memberDoc;
      if (m->Name == "init") c->Init = m.get();
      if (m->Name == "deinit") c->Deinit = m.get();
      c->Methods.push_back(std::move(m));
    } else if (auto f = parseField(memberPublic)) {
      f->Attrs = std::move(memberAttrs);
      f->Index = index++;
      f->Parent = c.get();
      c->Fields.push_back(std::move(f));
    } else {
      synchronize();
    }
    skipSeparators();
    if (match(Tok::Comma))
      skipSeparators();
  }
  expect(Tok::RBrace, "a class body");
  c->Range = rangeFrom(start);
  return c;
}

std::unique_ptr<MarkDecl> Parser::parseMark(std::vector<Attribute> attrs,
                                            bool isPublic) {
  size_t start = Pos;
  advance(); // mark
  auto m = makeNode<MarkDecl>(here());
  m->Attrs = std::move(attrs);
  m->IsPublic = isPublic;
  if (check(Tok::Identifier)) {
    m->Name = cur().Text;
    m->NameRange = cur().Range;
    advance();
  } else {
    expect(Tok::Identifier, "a mark name");
  }
  m->Generics = parseGenericParams();
  if (match(Tok::Colon)) {
    do {
      skipNewlines();
      m->SuperMarks.push_back(parseType());
    } while (match(Tok::Plus));
  }
  m->WhereClauses = parseWhereClauses();
  skipNewlines();
  if (!expect(Tok::LBrace, "a mark body")) {
    m->Range = rangeFrom(start);
    return m;
  }
  skipSeparators();
  while (!check(Tok::RBrace) && !atEnd()) {
    auto memberAttrs = parseAttributes();
    bool memberPublic = match(Tok::KwPub);
    if (check(Tok::KwFn)) {
      auto f = parseFunction(std::move(memberAttrs), memberPublic,
                             /*allowNoBody=*/true);
      if (!f->Body)
        f->Flavour = FunctionFlavour::MarkRequirement;
      f->Parent = m.get();
      m->Methods.push_back(std::move(f));
    } else if (check(Tok::KwType)) {
      // `type Item` — a type each binding chooses, reached as `Self::Item`.
      size_t tstart = Pos;
      advance();
      auto at = makeNode<AssociatedTypeDecl>(here());
      at->Attrs = std::move(memberAttrs);
      at->IsPublic = memberPublic || isPublic;
      if (check(Tok::Identifier)) {
        at->Name = cur().Text;
        at->NameRange = cur().Range;
        advance();
      } else {
        expect(Tok::Identifier, "an associated type name");
      }
      if (match(Tok::Colon)) {
        do {
          skipNewlines();
          at->Bounds.push_back(parseBound());
        } while (match(Tok::Plus));
      }
      if (match(Tok::Eq)) {
        Diags.error(at->NameRange,
                    "a mark declares an associated type without a value")
            .note("each `bind` chooses it: write `type {} = ...` there",
                  at->Name)
            .code(108);
        parseType();
      }
      at->Range = rangeFrom(tstart);
      at->Parent = m.get();
      m->AssociatedTypes.push_back(std::move(at));
    } else {
      Diags.error(cur().Range, "Expected '{}' — Got: '{}'", "fn",
                  cur().Text.empty() ? tokenSpelling(cur().Kind) : cur().Text)
          .note("a mark body contains method requirements, defaults and "
                "associated types")
          .code(107);
      synchronize();
    }
    skipSeparators();
  }
  expect(Tok::RBrace, "a mark body");
  m->Range = rangeFrom(start);
  return m;
}

std::unique_ptr<BindDecl> Parser::parseBind(std::vector<Attribute> attrs,
                                            bool isPublic) {
  size_t start = Pos;
  advance(); // bind
  auto b = makeNode<BindDecl>(here());
  b->Attrs = std::move(attrs);
  b->IsPublic = isPublic;
  b->Generics = parseGenericParams();

  size_t markStart = Pos;
  if (check(Tok::KwOperator)) {
    b->IsOperatorBinding = true;
    b->MarkPath.push_back("operator");
    advance();
    if (expect(Tok::ColonColon, "an operator binding")) {
      if (check(Tok::StringLiteral)) {
        // `operator::"*"` — the punctuation itself, for spellings no
        // identifier can hold.
        b->OperatorName = cur().Text;
        b->MarkPath.push_back(b->OperatorName);
        advance();
      } else if (check(Tok::Identifier)) {
        b->OperatorName = cur().Text;
        b->MarkPath.push_back(cur().Text);
        advance();
      } else if (isKeyword(cur().Kind)) {
        b->OperatorName = tokenSpelling(cur().Kind);
        b->MarkPath.push_back(b->OperatorName);
        advance();
      } else {
        expect(Tok::Identifier, "an operator name");
      }
    }
  } else {
    SourceRange r;
    b->MarkPath = parsePath(r);
    // A mark may take arguments of its own: `bind As<Fahrenheit> to Celsius`.
    // `bind Celsius into Fahrenheit` is the same binding, parsed below.
    if (check(Tok::ColonColon) && peek(1).is(Tok::Lt)) {
      advance();
      b->MarkGenericArgs = parseGenericArgs();
    } else if (check(Tok::Lt)) {
      b->MarkGenericArgs = parseGenericArgs();
    }
  }
  b->MarkRange = rangeFrom(markStart);
  b->Name = b->MarkPath.empty() ? "" : b->MarkPath.back();

  // `bind Celsius into Fahrenheit { ... }` is `bind As<Fahrenheit> to Celsius`.
  // The value-level spelling is `c into Fahrenheit`; the binding reads the
  // same way. Operator bindings keep `to`, because they name a mark, not a
  // conversion.
  if (!b->IsOperatorBinding && check(Tok::KwInto)) {
    advance();
    auto src = makeNode<NamedTypeRepr>(b->MarkRange);
    src->Path = std::move(b->MarkPath);
    src->GenericArgs = std::move(b->MarkGenericArgs);
    src->NameRange = b->MarkRange;
    src->Range = b->MarkRange;
    TypeReprPtr dest = parseType();
    b->TargetType = std::move(src);
    b->MarkPath = {"As"};
    b->Name = "As";
    b->MarkGenericArgs.clear();
    if (dest) {
      if (dest->Range.isValid())
        b->MarkRange = dest->Range;
      b->MarkGenericArgs.push_back(std::move(dest));
    }
  } else {
    if (!expect(Tok::KwTo, "a bind declaration")) {
      b->Range = rangeFrom(start);
      return b;
    }
    b->TargetType = parseType();
  }
  b->WhereClauses = parseWhereClauses();
  skipNewlines();
  if (!expect(Tok::LBrace, "a bind body")) {
    b->Range = rangeFrom(start);
    return b;
  }
  skipSeparators();
  while (!check(Tok::RBrace) && !atEnd()) {
    auto memberAttrs = parseAttributes();
    bool memberPublic = match(Tok::KwPub);
    if (check(Tok::KwFn)) {
      auto f = parseFunction(std::move(memberAttrs), memberPublic, false);
      f->Parent = b.get();
      b->Methods.push_back(std::move(f));
    } else if (check(Tok::KwType)) {
      // `type Item = i64` — this binding's choice for one of the mark's
      // associated types.
      size_t tstart = Pos;
      advance();
      auto at = makeNode<AssociatedTypeDecl>(here());
      at->Attrs = std::move(memberAttrs);
      at->IsPublic = memberPublic;
      if (check(Tok::Identifier)) {
        at->Name = cur().Text;
        at->NameRange = cur().Range;
        advance();
      } else {
        expect(Tok::Identifier, "an associated type name");
      }
      if (expect(Tok::Eq, "an associated type"))
        at->Value = parseType();
      at->Range = rangeFrom(tstart);
      at->Parent = b.get();
      b->AssociatedTypes.push_back(std::move(at));
    } else {
      Diags.error(cur().Range, "Expected '{}' — Got: '{}'", "fn",
                  cur().Text.empty() ? tokenSpelling(cur().Kind) : cur().Text)
          .note("a bind body implements the mark's methods and chooses its "
                "associated types")
          .code(108);
      synchronize();
    }
    skipSeparators();
  }
  expect(Tok::RBrace, "a bind body");
  b->Range = rangeFrom(start);
  return b;
}

std::unique_ptr<ExtendDecl> Parser::parseExtend(std::vector<Attribute> attrs,
                                                bool isPublic) {
  size_t start = Pos;
  advance(); // extend
  auto e = makeNode<ExtendDecl>(here());
  e->Attrs = std::move(attrs);
  e->IsPublic = isPublic;
  e->Generics = parseGenericParams();
  e->TargetType = parseType();
  if (auto *n = dyn_cast<NamedTypeRepr>(e->TargetType.get()))
    e->Name = n->Path.empty() ? "" : n->Path.back();
  skipNewlines();
  if (!expect(Tok::LBrace, "an extend body")) {
    e->Range = rangeFrom(start);
    return e;
  }
  skipSeparators();
  while (!check(Tok::RBrace) && !atEnd()) {
    // The `///` above the member, before the attributes eat the token it is
    // attached to. A method in an `extend` is documented exactly as one in a
    // class body is, and for a struct or an enum it is usually where all of
    // them are.
    std::string memberDoc = cur().Doc;
    auto memberAttrs = parseAttributes();
    bool memberPublic = match(Tok::KwPub);
    if (check(Tok::KwFn)) {
      auto f = parseFunction(std::move(memberAttrs), memberPublic, false);
      f->Parent = e.get();
      if (!memberDoc.empty() && f->Doc.empty()) f->Doc = memberDoc;
      e->Methods.push_back(std::move(f));
    } else {
      Diags.error(cur().Range, "Expected '{}' — Got: '{}'", "fn",
                  cur().Text.empty() ? tokenSpelling(cur().Kind) : cur().Text)
          .note("`extend` adds methods to an existing type")
          .code(109);
      synchronize();
    }
    skipSeparators();
  }
  expect(Tok::RBrace, "an extend body");
  e->Range = rangeFrom(start);
  return e;
}

std::unique_ptr<ImportDecl> Parser::parseImport(std::vector<Attribute> attrs,
                                                bool isPublic) {
  size_t start = Pos;
  advance(); // import
  auto imp = makeNode<ImportDecl>(here());
  imp->Attrs = std::move(attrs);
  imp->IsPublic = isPublic;

  for (;;) {
    if (check(Tok::Identifier)) {
      imp->Path.push_back(cur().Text);
      advance();
    } else {
      expect(Tok::Identifier, "an import path");
      break;
    }
    if (!match(Tok::ColonColon))
      break;
    if (check(Tok::Star)) { // import a::b::*
      advance();
      imp->IsGlob = true;
      break;
    }
    if (check(Tok::LBrace)) { // import a::{b, c}
      advance();
      skipNewlines();
      while (!check(Tok::RBrace) && !atEnd()) {
        if (check(Tok::Identifier)) {
          imp->Names.push_back(cur().Text);
          advance();
        } else {
          expect(Tok::Identifier, "an import list");
          break;
        }
        skipNewlines();
        if (!match(Tok::Comma))
          break;
        skipNewlines();
      }
      expect(Tok::RBrace, "an import list");
      break;
    }
  }
  if (match(Tok::KwAs)) {
    if (check(Tok::Identifier)) {
      imp->Alias = cur().Text;
      advance();
    } else {
      expect(Tok::Identifier, "an import alias");
    }
  }
  imp->Name = imp->Alias.empty()
                  ? (imp->Path.empty() ? "" : imp->Path.back())
                  : imp->Alias;
  expectTerminator("an import declaration");
  imp->Range = rangeFrom(start);
  return imp;
}

namespace {
/// `@as("newName")` on a foreign declaration: the C side keeps the name that
/// was written, and Rune sees the new one instead.
///
/// This is what lets a program import C's `bind` and still declare a Rune
/// function called `bind` — the foreign one arrives under whatever name the
/// program picks, so the two never collide.
void applyForeignRename(ValueDecl *d, DiagnosticEngine &diags) {
  const Attribute *a = d->findAttr("as");
  if (!a)
    return;
  if (a->Args.size() != 1) {
    diags.error(a->Range, "`@as` takes one string — the name Rune should use")
        .note("write `@as(\"cbind\")` above the declaration")
        .code(234);
    return;
  }
  const auto *lit = dyn_cast<StringLitExpr>(a->Args[0].get());
  if (!lit || lit->Value.empty()) {
    diags.error(a->Args[0]->Range, "`@as` takes a non-empty string")
        .note("write `@as(\"cbind\")` above the declaration")
        .code(234);
    return;
  }
  if (d->Name.empty())
    return;
  d->LinkName = d->Name;
  d->Name = lit->Value;
}
} // namespace

std::unique_ptr<ExternDecl> Parser::parseExtern(std::vector<Attribute> attrs,
                                                bool isPublic) {
  size_t start = Pos;
  advance(); // extern
  auto ext = makeNode<ExternDecl>(here());
  ext->Attrs = std::move(attrs);
  ext->IsPublic = isPublic;
  ext->ABI = "C";
  if (check(Tok::StringLiteral)) {
    ext->ABI = cur().Text;
    advance();
  }
  ext->Name = ext->ABI;
  skipNewlines();
  if (!expect(Tok::LBrace, "an extern block")) {
    ext->Range = rangeFrom(start);
    return ext;
  }
  skipSeparators();
  while (!check(Tok::RBrace) && !atEnd()) {
    auto memberAttrs = parseAttributes();
    bool memberPublic = match(Tok::KwPub);

    bool isVarDecl = check(Tok::KwVar) || check(Tok::KwMut);
    bool implicitFn = check(Tok::Identifier) && peek(1).is(Tok::LParen);
    if (check(Tok::KwFn) || implicitFn) {
      auto f = parseFunction(std::move(memberAttrs), memberPublic,
                             /*allowNoBody=*/true, /*implicitFnKeyword=*/implicitFn);
      f->IsExtern = true;
      f->ExternABI = ext->ABI;
      f->IsPublic = memberPublic || isPublic;
      f->Parent = ext.get();
      applyForeignRename(f.get(), Diags);
      ext->Functions.push_back(std::move(f));
    } else if (isVarDecl || check(Tok::Identifier)) {
      size_t vstart = Pos;
      auto g = makeNode<GlobalVarDecl>(here());
      g->Attrs = std::move(memberAttrs);
      g->IsPublic = memberPublic || isPublic;
      if (isVarDecl) {
        g->IsMutable = true;
        advance();
      }
      if (check(Tok::Identifier)) {
        g->Name = cur().Text;
        g->NameRange = cur().Range;
        advance();
      }
      if (expect(Tok::Colon, "an extern variable"))
        g->TypeAnnotation = parseType();
      g->Range = rangeFrom(vstart);
      g->Parent = ext.get();
      applyForeignRename(g.get(), Diags);
      ext->Globals.push_back(std::move(g));
    } else {
      Diags.error(cur().Range, "Expected '{}' — Got: '{}'", "a declaration",
                  cur().Text.empty() ? tokenSpelling(cur().Kind) : cur().Text)
          .note("an extern block declares foreign functions and variables")
          .code(110);
      synchronize();
    }
    skipSeparators();
  }
  expect(Tok::RBrace, "an extern block");
  ext->Range = rangeFrom(start);
  return ext;
}

std::unique_ptr<TypeAliasDecl> Parser::parseTypeAlias(std::vector<Attribute> attrs,
                                                      bool isPublic) {
  size_t start = Pos;
  advance(); // type
  auto t = makeNode<TypeAliasDecl>(here());
  t->Attrs = std::move(attrs);
  t->IsPublic = isPublic;
  if (check(Tok::Identifier)) {
    t->Name = cur().Text;
    t->NameRange = cur().Range;
    advance();
  } else {
    expect(Tok::Identifier, "a type alias name");
  }
  t->Generics = parseGenericParams();
  if (expect(Tok::Eq, "a type alias"))
    t->Aliased = parseType();
  expectTerminator("a type alias");
  t->Range = rangeFrom(start);
  return t;
}

std::unique_ptr<GlobalVarDecl> Parser::parseGlobalVar(std::vector<Attribute> attrs,
                                                      bool isPublic) {
  size_t start = Pos;
  auto g = makeNode<GlobalVarDecl>(here());
  g->Attrs = std::move(attrs);
  g->IsPublic = isPublic;
  if (check(Tok::KwGlobal))
    advance();
  if (check(Tok::KwLet)) {
    advance();
  } else if (check(Tok::KwVar) || check(Tok::KwMut)) {
    g->IsMutable = true;
    advance();
  }
  if (check(Tok::Identifier)) {
    g->Name = cur().Text;
    g->NameRange = cur().Range;
    advance();
  } else {
    expect(Tok::Identifier, "a global variable");
  }
  if (match(Tok::Colon))
    g->TypeAnnotation = parseType();
  if (match(Tok::Eq))
    g->Init = parseExpr();
  expectTerminator("a global variable declaration");
  g->Range = rangeFrom(start);
  return g;
}

//===----------------------------------------------------------------------===//
// Types
//===----------------------------------------------------------------------===//

std::vector<std::string> Parser::parsePath(SourceRange &range) {
  std::vector<std::string> path;
  size_t start = Pos;
  for (;;) {
    if (check(Tok::Identifier)) {
      path.push_back(cur().Text);
      advance();
    } else if (check(Tok::KwSelfType)) {
      path.push_back("Self");
      advance();
    } else {
      expect(Tok::Identifier, "a qualified name");
      break;
    }
    // `::<` starts a turbofish, not another path segment.
    if (check(Tok::ColonColon) && !peek(1).is(Tok::Lt) &&
        !peek(1).is(Tok::LBrace) && !peek(1).is(Tok::Star)) {
      advance();
      continue;
    }
    break;
  }
  range = rangeFrom(start);
  return path;
}

std::vector<TypeReprPtr> Parser::parseGenericArgs() {
  std::vector<TypeReprPtr> args;
  if (!check(Tok::Lt))
    return args;
  advance();
  skipNewlines();
  while (!check(Tok::Gt) && !check(Tok::Shr) && !check(Tok::GtEq) &&
         !check(Tok::ShrEq) && !atEnd()) {
    args.push_back(parseType());
    skipNewlines();
    if (!match(Tok::Comma))
      break;
    skipNewlines();
  }
  consumeCloseAngle();
  return args;
}

TypeReprPtr Parser::parseType() {
  TypeReprPtr t = parseTypeNoSuffix();
  // `T?` is sugar for Option<T>; it may stack (`T??`) though that is unusual.
  while (check(Tok::Question)) {
    auto opt = makeNode<OptionalTypeRepr>(t->Range.merge(here()));
    advance();
    opt->Element = std::move(t);
    opt->Range = opt->Range.merge(rangeFrom(Pos - 1));
    t = std::move(opt);
  }
  return t;
}

TypeReprPtr Parser::parseTypeNoSuffix() {
  size_t start = Pos;

  if (check(Tok::Underscore)) {
    advance();
    return makeNode<InferTypeRepr>(rangeFrom(start));
  }

  if (check(Tok::KwSelfType)) {
    advance();
    // `Self::Item` names an associated type; bare `Self` is the type itself.
    if (check(Tok::ColonColon) && peek(1).is(Tok::Identifier)) {
      auto named = makeNode<NamedTypeRepr>(here());
      named->Path.push_back("Self");
      while (match(Tok::ColonColon)) {
        if (!check(Tok::Identifier)) {
          expect(Tok::Identifier, "an associated type");
          break;
        }
        named->Path.push_back(cur().Text);
        advance();
      }
      named->Range = rangeFrom(start);
      named->NameRange = named->Range;
      return named;
    }
    return makeNode<SelfTypeRepr>(rangeFrom(start));
  }

  if (check(Tok::KwDyn)) {
    advance();
    auto d = makeNode<DynTypeRepr>(here());
    d->MarkType = parseTypeNoSuffix();
    d->Range = rangeFrom(start);
    return d;
  }

  // `some Mark` — the mark's name follows, so the word cannot be mistaken
  // for a type called `some`. It is not a keyword: `option::some` and a
  // local called `some` go on meaning what they did.
  if (check(Tok::Identifier) && cur().Text == "some" &&
      (peek(1).is(Tok::Identifier) || peek(1).is(Tok::KwOperator))) {
    advance();
    auto o = makeNode<SomeTypeRepr>(here());
    o->MarkType = parseTypeNoSuffix();
    o->Range = rangeFrom(start);
    return o;
  }

  if (check(Tok::Amp) || check(Tok::Star)) {
    bool raw = check(Tok::Star);
    advance();
    auto p = makeNode<PointerTypeRepr>(here());
    p->IsRaw = raw;
    if (check(Tok::KwWeak)) {
      p->IsWeak = true;
      advance();
    }
    if (check(Tok::KwVar) || check(Tok::KwMut)) {
      p->IsMutable = true;
      advance();
    }
    p->Pointee = parseType();
    p->Origin = parseOriginClause();
    p->Range = rangeFrom(start);
    return p;
  }

  if (check(Tok::LBracket)) {
    // `[N:T]` is a fixed array, `[T]` a slice. Scan to the matching bracket
    // and look for a top-level `:` to tell them apart without backtracking.
    size_t probe = Pos + 1;
    int depth = 0;
    bool isArray = false;
    while (probe < Toks.size() && !Toks[probe].is(Tok::EndOfFile)) {
      Tok k = Toks[probe].Kind;
      if (k == Tok::LBracket || k == Tok::LParen || k == Tok::LBrace) ++depth;
      else if (k == Tok::RParen || k == Tok::RBrace) --depth;
      else if (k == Tok::RBracket) {
        if (depth == 0) break;
        --depth;
      } else if (k == Tok::Colon && depth == 0) {
        isArray = true;
        break;
      }
      ++probe;
    }
    advance(); // [
    if (isArray) {
      auto a = makeNode<ArrayTypeRepr>(here());
      a->Size = parseExpr();
      expect(Tok::Colon, "an array type");
      a->Element = parseType();
      expect(Tok::RBracket, "an array type");
      a->Range = rangeFrom(start);
      return a;
    }
    auto s = makeNode<SliceTypeRepr>(here());
    s->Element = parseType();
    expect(Tok::RBracket, "a slice type");
    s->Origin = parseOriginClause();      // a slice is a borrow of its buffer
    s->Range = rangeFrom(start);
    return s;
  }

  if (check(Tok::LParen)) {
    advance();
    skipNewlines();
    auto t = makeNode<TupleTypeRepr>(here());
    while (!check(Tok::RParen) && !atEnd()) {
      t->Elements.push_back(parseType());
      skipNewlines();
      if (!match(Tok::Comma))
        break;
      skipNewlines();
    }
    expect(Tok::RParen, "a tuple type");
    t->Range = rangeFrom(start);
    // `(T)` is just T in parentheses, not a one-element tuple.
    if (t->Elements.size() == 1)
      return std::move(t->Elements[0]);
    return t;
  }

  if (check(Tok::At)) {
    advance();
    bool isC = check(Tok::Identifier) && cur().Text == "cfunction";
    bool named = isC || (check(Tok::Identifier) && cur().Text == "function");
    if (!named) {
      Diags.error(cur().Range, "Expected '{}' — Got: '{}'", "function",
                  cur().Text.empty() ? tokenSpelling(cur().Kind) : cur().Text)
          .note("function types are written @function(args...) -> ret, or "
                "@function(args...) for one that returns nothing")
          .code(111);
    } else {
      advance();
    }
    // `@function(args...) -> ret` is the one spelling. The list is always
    // the parameters; leaving the arrow off means the function returns
    // nothing, exactly as leaving `-> ret` off a `fn` does. (An older form
    // put the result first, `@function(ret, args...)`, which made
    // `@function(T)` mean "takes nothing, returns T" — almost never what was
    // meant, and reported only later, at the call.)
    auto f = makeNode<FunctionTypeReprNode>(here());
    f->IsCFunction = isC;
    if (expect(Tok::LParen, "a function type")) {
      skipNewlines();
      while (!check(Tok::RParen) && !atEnd()) {
        // `name: T` names the parameter so a `from` on the result can point
        // at it; the name means nothing else.
        std::string name;
        if (check(Tok::Identifier) && peek(1).is(Tok::Colon)) {
          name = cur().Text;
          advance();
          advance();
        }
        f->ParamNames.push_back(std::move(name));
        f->Params.push_back(parseType());
        skipNewlines();
        if (!match(Tok::Comma))
          break;
        skipNewlines();
      }
      expect(Tok::RParen, "a function type");
    }
    if (match(Tok::Arrow))
      f->ReturnType = parseType();
    f->Range = rangeFrom(start);
    return f;
  }

  auto n = makeNode<NamedTypeRepr>(here());
  n->Path = parsePath(n->NameRange);
  if (check(Tok::ColonColon) && peek(1).is(Tok::Lt)) {
    advance();
    n->GenericArgs = parseGenericArgs();
  } else if (check(Tok::Lt)) {
    n->GenericArgs = parseGenericArgs();
  }
  // A struct that holds references borrows from wherever they came from;
  // `Cursor from text` says where, the way `&T from text` does.
  n->Origin = parseOriginClause();
  n->Range = rangeFrom(start);
  return n;
}

//===----------------------------------------------------------------------===//
// Patterns
//===----------------------------------------------------------------------===//

PatternPtr Parser::parsePattern() {
  size_t start = Pos;
  PatternPtr first = parsePatternNoOr();
  if (!check(Tok::Pipe))
    return first;
  auto orPat = makeNode<OrPattern>(here());
  orPat->Alternatives.push_back(std::move(first));
  while (match(Tok::Pipe)) {
    skipNewlines();
    orPat->Alternatives.push_back(parsePatternNoOr());
  }
  orPat->Range = rangeFrom(start);
  return orPat;
}

PatternPtr Parser::parsePatternNoOr() {
  size_t start = Pos;
  PatternPtr p = parsePatternPrimary();
  if (check(Tok::DotDot) || check(Tok::DotDotEq)) {
    bool inclusive = check(Tok::DotDotEq);
    advance();
    auto r = makeNode<RangePattern>(here());
    r->Inclusive = inclusive;
    if (auto *lit = dyn_cast<LiteralPattern>(p.get()))
      r->Lo = std::move(lit->Value);
    if (startsExpression(cur().Kind))
      r->Hi = parseBinaryExpr(4);
    r->Range = rangeFrom(start);
    return r;
  }
  return p;
}

PatternPtr Parser::parsePatternPrimary() {
  size_t start = Pos;

  if (check(Tok::Underscore)) {
    advance();
    return makeNode<WildcardPattern>(rangeFrom(start));
  }

  if (check(Tok::Amp)) {
    advance();
    auto r = makeNode<RefPattern>(here());
    if (check(Tok::KwVar) || check(Tok::KwMut)) {
      r->IsMutable = true;
      advance();
    }
    r->Sub = parsePatternPrimary();
    r->Range = rangeFrom(start);
    return r;
  }

  if (check(Tok::LParen)) {
    advance();
    skipNewlines();
    auto t = makeNode<TuplePattern>(here());
    while (!check(Tok::RParen) && !atEnd()) {
      t->Elements.push_back(parsePattern());
      skipNewlines();
      if (!match(Tok::Comma))
        break;
      skipNewlines();
    }
    expect(Tok::RParen, "a tuple pattern");
    t->Range = rangeFrom(start);
    if (t->Elements.size() == 1)
      return std::move(t->Elements[0]);
    return t;
  }

  // `[a, b, c]`, `[first, ..rest]`, `[first, .., last]` — elements of an
  // array or a slice. `..` may appear once, and takes whatever is between
  // the elements before it and the ones after.
  if (check(Tok::LBracket)) {
    advance();
    skipNewlines();
    auto sp = makeNode<SlicePattern>(here());
    while (!check(Tok::RBracket) && !atEnd()) {
      if (check(Tok::DotDot)) {
        SourceRange dots = cur().Range;
        advance();
        if (sp->HasRest) {
          Diags.error(dots, "a slice pattern may have only one `..`")
              .note("everything before it is matched from the front and "
                    "everything after it from the back")
              .code(113);
        }
        sp->HasRest = true;
        // `..rest` binds the middle; a bare `..` skips it.
        if (check(Tok::Identifier)) {
          auto b = makeNode<BindingPattern>(here());
          b->Name = cur().Text;
          advance();
          b->Range = dots.merge(rangeFrom(Pos - 1));
          sp->Rest = std::move(b);
        } else if (check(Tok::KwVar) || check(Tok::KwMut)) {
          advance();
          auto b = makeNode<BindingPattern>(here());
          b->IsMutable = true;
          if (check(Tok::Identifier)) {
            b->Name = cur().Text;
            advance();
          } else {
            expect(Tok::Identifier, "a pattern binding");
          }
          b->Range = dots.merge(rangeFrom(Pos - 1));
          sp->Rest = std::move(b);
        }
      } else if (sp->HasRest) {
        sp->Suffix.push_back(parsePattern());
      } else {
        sp->Prefix.push_back(parsePattern());
      }
      skipNewlines();
      if (!match(Tok::Comma))
        break;
      skipNewlines();
    }
    expect(Tok::RBracket, "a slice pattern");
    sp->Range = rangeFrom(start);
    return sp;
  }

  if (check(Tok::KwVar) || check(Tok::KwMut)) {
    advance();
    auto b = makeNode<BindingPattern>(here());
    b->IsMutable = true;
    if (check(Tok::Identifier)) {
      b->Name = cur().Text;
      advance();
    } else {
      expect(Tok::Identifier, "a pattern binding");
    }
    b->Range = rangeFrom(start);
    return b;
  }

  // Literals, including negative numbers.
  if (check(Tok::IntLiteral) || check(Tok::FloatLiteral) ||
      check(Tok::StringLiteral) || check(Tok::CharLiteral) ||
      check(Tok::KwTrue) || check(Tok::KwFalse) ||
      (check(Tok::Minus) && (peek(1).is(Tok::IntLiteral) || peek(1).is(Tok::FloatLiteral)))) {
    auto lit = makeNode<LiteralPattern>(here());
    lit->Value = parseUnaryExpr();
    lit->Range = rangeFrom(start);
    return lit;
  }

  if (check(Tok::Identifier) || check(Tok::KwSelfType)) {
    SourceRange pathRange;
    std::vector<std::string> path = parsePath(pathRange);

    if (check(Tok::LParen)) {
      advance();
      skipNewlines();
      auto e = makeNode<EnumPattern>(here());
      e->Path = std::move(path);
      while (!check(Tok::RParen) && !atEnd()) {
        e->Elements.push_back(parsePattern());
        skipNewlines();
        if (!match(Tok::Comma))
          break;
        skipNewlines();
      }
      expect(Tok::RParen, "an enum pattern");
      e->Range = rangeFrom(start);
      return e;
    }

    // In a condition (`if value is Point { ... }`) a `{` opens the body, not a
    // struct pattern, which is the same rule struct literals follow.
    if (check(Tok::LBrace) && !NoStructLiteral) {
      advance();
      skipNewlines();
      auto s = makeNode<StructPattern>(here());
      s->Path = std::move(path);
      while (!check(Tok::RBrace) && !atEnd()) {
        if (check(Tok::DotDot)) {
          advance();
          s->HasRest = true;
          skipNewlines();
          break;
        }
        StructPatternField f;
        size_t fstart = Pos;
        if (check(Tok::Identifier)) {
          f.Name = cur().Text;
          advance();
        } else {
          expect(Tok::Identifier, "a struct pattern field");
          break;
        }
        if (match(Tok::Colon))
          f.Value = parsePattern();
        f.Range = rangeFrom(fstart);
        s->Fields.push_back(std::move(f));
        skipNewlines();
        if (!match(Tok::Comma))
          break;
        skipNewlines();
      }
      expect(Tok::RBrace, "a struct pattern");
      s->Range = rangeFrom(start);
      return s;
    }

    // A single lowercase-ish name binds; anything qualified names a variant.
    // Sema re-classifies a bare name that turns out to be a unit variant.
    if (path.size() == 1) {
      auto b = makeNode<BindingPattern>(here());
      b->Name = path[0];
      if (match(Tok::At))
        b->Sub = parsePatternPrimary();
      b->Range = rangeFrom(start);
      return b;
    }
    auto p = makeNode<PathPattern>(here());
    p->Path = std::move(path);
    p->Range = rangeFrom(start);
    return p;
  }

  Diags.error(cur().Range, "Expected '{}' — Got: '{}'", "a pattern",
              cur().Text.empty() ? tokenSpelling(cur().Kind) : cur().Text)
      .note("patterns are literals, `_`, bindings, tuples, slices `[a, ..]`, "
            "or enum/struct shapes")
      .code(112);
  advance();
  return makeNode<WildcardPattern>(rangeFrom(start));
}

//===----------------------------------------------------------------------===//
// Statements
//===----------------------------------------------------------------------===//

bool Parser::looksLikeTypedBinding() const {
  if (check(Tok::KwLet))
    return true;
  if (!check(Tok::Identifier) || !peek(1).is(Tok::Colon))
    return false;
  // `name: loop {}` is a labelled loop, not a binding.
  return !peek(2).isAny(Tok::KwLoop, Tok::KwWhile, Tok::KwFor);
}

bool Parser::looksLikeLoopLabel() const {
  return check(Tok::Identifier) && peek(1).is(Tok::Colon) &&
         peek(2).isAny(Tok::KwLoop, Tok::KwWhile, Tok::KwFor);
}

std::unique_ptr<BlockExpr> Parser::parseBlock(const char *context) {
  size_t start = Pos;
  auto block = makeNode<BlockExpr>(here());
  if (!expect(Tok::LBrace, context)) {
    block->Range = rangeFrom(start);
    return block;
  }
  skipSeparators();

  while (!check(Tok::RBrace) && !atEnd()) {
    size_t before = Pos;
    StmtPtr s = parseStatement();
    if (!s) {
      if (Pos == before)
        advance();
      skipSeparators();
      continue;
    }
    // A trailing expression with no `;` is the block's value.
    if (!LastHadSemi && isa<ExprStmt>(s.get())) {
      skipNewlines();
      if (check(Tok::RBrace) || atEnd()) {
        block->Tail = std::move(cast<ExprStmt>(s.get())->Value);
        break;
      }
    }
    block->Stmts.push_back(std::move(s));
    skipSeparators();
    if (Diags.reachedLimit())
      break;
  }

  expect(Tok::RBrace, context);
  block->Range = rangeFrom(start);
  return block;
}

StmtPtr Parser::parseVarStatement(bool isGlobal) {
  size_t start = Pos;
  auto v = makeNode<VarStmtNode>(here());
  v->IsGlobal = isGlobal;
  if (isGlobal)
    advance(); // global
  // `let` states immutability explicitly; `var`/`mut` opt into mutation.
  if (check(Tok::KwLet)) {
    advance();
    if (check(Tok::KwVar) || check(Tok::KwMut)) {
      Diags.error(here(), "`let` already means immutable")
          .note("write `var name` on its own to declare a mutable binding")
          .code(116);
      advance();
    }
  } else if (check(Tok::KwVar) || check(Tok::KwMut)) {
    v->IsMutable = true;
    advance();
  }
  v->Binding = parsePattern();
  if (match(Tok::Colon))
    v->TypeAnnotation = parseType();
  if (match(Tok::Eq)) {
    skipNewlines();
    v->Init = parseExpr();
  }
  expectTerminator("a variable declaration");
  v->Range = rangeFrom(start);
  return v;
}

StmtPtr Parser::parseStatement() {
  skipSeparators();
  if (check(Tok::RBrace) || atEnd())
    return nullptr;

  size_t start = Pos;

  // Nested declarations keep their decorators.
  if (check(Tok::At) || check(Tok::KwPub) || startsDecl(cur().Kind)) {
    std::vector<Attribute> attrs = parseAttributes();
    bool isPublic = match(Tok::KwPub);
    if (startsDecl(cur().Kind) || check(Tok::KwFn)) {
      auto d = parseDecl(std::move(attrs), isPublic);
      if (!d)
        return nullptr;
      auto ds = makeNode<DeclStmt>(rangeFrom(start));
      ds->Inner = std::move(d);
      LastHadSemi = false;
      skipSeparators();
      return ds;
    }
    // Decorators on an expression statement are not meaningful; fall through
    // with them dropped after reporting.
    if (!attrs.empty())
      Diags.error(attrs.front().Range, "decorators may only precede declarations")
          .note("move the `@{}` decorator onto a `fn`, `class`, `struct` or "
                "`enum`", attrs.front().Name)
          .code(113);
  }

  if (check(Tok::KwGlobal))
    return parseVarStatement(/*isGlobal=*/true);
  if (check(Tok::KwVar) || check(Tok::KwMut) || check(Tok::KwLet))
    return parseVarStatement(/*isGlobal=*/false);
  if (looksLikeTypedBinding())
    return parseVarStatement(/*isGlobal=*/false);

  if (check(Tok::KwDefer)) {
    advance();
    auto d = makeNode<DeferStmtNode>(here());
    d->Body = parseExpr();
    expectTerminator("a defer statement");
    d->Range = rangeFrom(start);
    return d;
  }

  if (looksLikeLoopLabel()) {
    std::string label = cur().Text;
    advance();
    advance(); // ':'
    auto es = makeNode<ExprStmt>(here());
    if (check(Tok::KwLoop))
      es->Value = parseLoop(label);
    else if (check(Tok::KwWhile))
      es->Value = parseWhile(label);
    else
      es->Value = parseFor(label);
    expectTerminator("a labelled loop");
    es->Range = rangeFrom(start);
    return es;
  }

  auto es = makeNode<ExprStmt>(here());
  es->Value = parseExpr();
  expectTerminator("a statement");
  es->Range = rangeFrom(start);
  return es;
}

//===----------------------------------------------------------------------===//
// Expressions
//===----------------------------------------------------------------------===//

ExprPtr Parser::parseExpr() { return parseAssignExpr(); }

ExprPtr Parser::parseAssignExpr() {
  size_t start = Pos;
  ExprPtr lhs = parseRangeExpr();
  AssignOp op;
  if (assignOpFor(cur().Kind, op)) {
    SourceRange opRange = here();
    advance();
    skipNewlines();
    auto a = makeNode<AssignExpr>(here());
    a->Op = op;
    a->OpRange = opRange;
    a->LHS = std::move(lhs);
    a->RHS = parseAssignExpr();
    a->Range = rangeFrom(start);
    return a;
  }
  return lhs;
}

ExprPtr Parser::parseRangeExpr() {
  size_t start = Pos;

  if (check(Tok::DotDot) || check(Tok::DotDotEq)) { // `..end`
    bool inclusive = check(Tok::DotDotEq);
    advance();
    auto r = makeNode<RangeExpr>(here());
    r->Inclusive = inclusive;
    if (startsExpression(cur().Kind))
      r->Hi = parseBinaryExpr(1);
    r->Range = rangeFrom(start);
    return r;
  }

  ExprPtr lo = parseBinaryExpr(1);
  if (check(Tok::DotDot) || check(Tok::DotDotEq)) {
    bool inclusive = check(Tok::DotDotEq);
    advance();
    auto r = makeNode<RangeExpr>(here());
    r->Inclusive = inclusive;
    r->Lo = std::move(lo);
    if (startsExpression(cur().Kind))
      r->Hi = parseBinaryExpr(1);
    r->Range = rangeFrom(start);
    return r;
  }
  return lo;
}

ExprPtr Parser::parseBinaryExpr(int minPrec) {
  size_t start = Pos;
  ExprPtr lhs = parseUnaryExpr();

  // `as` and `is` bind tighter than any binary operator.
  for (;;) {
    if (check(Tok::KwAs)) {
      advance();
      auto c = makeNode<CastExpr>(here());
      c->Operand = std::move(lhs);
      c->TargetType = parseType();
      c->Range = rangeFrom(start);
      lhs = std::move(c);
      continue;
    }
    if (check(Tok::KwInto)) {
      advance();
      auto c = makeNode<IntoExpr>(here());
      c->Operand = std::move(lhs);
      c->TargetType = parseType();
      c->Range = rangeFrom(start);
      lhs = std::move(c);
      continue;
    }
    if (check(Tok::KwIs) && !StopAtIs) {
      advance();
      auto c = makeNode<TypeTestExpr>(here());
      c->Operand = std::move(lhs);
      c->TargetType = parseType();
      c->Range = rangeFrom(start);
      lhs = std::move(c);
      continue;
    }
    break;
  }

  for (;;) {
    int prec = binaryPrec(cur().Kind);
    if (prec < minPrec)
      break;
    Tok opTok = cur().Kind;
    SourceRange opRange = here();
    advance();
    skipNewlines();
    // `??` is right-associative; everything else is left-associative.
    int nextMin = opTok == Tok::QuestionQuestion ? prec : prec + 1;
    ExprPtr rhs = parseBinaryExpr(nextMin);
    auto b = makeNode<BinaryExpr>(here());
    b->Op = binaryOpFor(opTok);
    b->OpRange = opRange;
    b->LHS = std::move(lhs);
    b->RHS = std::move(rhs);
    b->Range = rangeFrom(start);
    lhs = std::move(b);
  }
  return lhs;
}

ExprPtr Parser::parseUnaryExpr() {
  size_t start = Pos;

  if (check(Tok::Minus) || check(Tok::Bang) || check(Tok::Tilde)) {
    UnaryOp op = check(Tok::Minus)  ? UnaryOp::Neg
                 : check(Tok::Bang) ? UnaryOp::Not
                                    : UnaryOp::BitNot;
    SourceRange opRange = here();
    advance();
    auto u = makeNode<UnaryExpr>(here());
    u->Op = op;
    u->OpRange = opRange;
    u->Operand = parseUnaryExpr();
    u->Range = rangeFrom(start);
    return u;
  }

  if (check(Tok::Amp)) {
    advance();
    auto b = makeNode<BorrowExpr>(here());
    if (check(Tok::KwVar) || check(Tok::KwMut)) {
      b->IsMutable = true;
      advance();
    }
    b->Operand = parseUnaryExpr();
    b->Range = rangeFrom(start);
    return b;
  }

  if (check(Tok::Star)) {
    advance();
    auto d = makeNode<DerefExpr>(here());
    d->Operand = parseUnaryExpr();
    d->Range = rangeFrom(start);
    return d;
  }

  return parsePostfixExpr(parsePrimaryExpr());
}

bool Parser::parseCallArgs(std::vector<Argument> &out) {
  if (!expect(Tok::LParen, "an argument list"))
    return false;
  skipNewlines();
  while (!check(Tok::RParen) && !atEnd()) {
    Argument a;
    // `name: value` is a labelled argument.
    if (check(Tok::Identifier) && peek(1).is(Tok::Colon)) {
      a.Label = cur().Text;
      a.LabelRange = here();
      advance();
      advance();
      skipNewlines();
    }
    // Struct literals are fine inside an argument list even when the call
    // itself sits in a condition.
    bool savedNoStruct = NoStructLiteral;
    bool savedStopIs = StopAtIs;
    NoStructLiteral = false;
    StopAtIs = false;
    a.Value = parseExpr();
    NoStructLiteral = savedNoStruct;
    StopAtIs = savedStopIs;
    out.push_back(std::move(a));
    skipNewlines();
    if (!match(Tok::Comma))
      break;
    skipNewlines();
  }
  return expect(Tok::RParen, "an argument list");
}

ExprPtr Parser::parsePostfixExpr(ExprPtr base) {
  size_t start = Pos;
  for (;;) {
    if (check(Tok::LParen)) {
      auto c = makeNode<CallExpr>(base->Range);
      c->ParenRange = here();
      c->Callee = std::move(base);
      parseCallArgs(c->Args);
      c->IsMethodCall = isa<MemberExpr>(c->Callee.get());
      c->Range = c->Callee->Range.merge(rangeFrom(start));
      base = std::move(c);
      continue;
    }
    if (check(Tok::LBracket)) {
      auto ix = makeNode<IndexExpr>(base->Range);
      ix->BracketRange = here();
      advance();
      skipNewlines();
      ix->Base = std::move(base);
      bool saved = NoStructLiteral;
      NoStructLiteral = false;
      ix->Index = parseExpr();
      NoStructLiteral = saved;
      skipNewlines();
      expect(Tok::RBracket, "an index expression");
      ix->Range = ix->Base->Range.merge(rangeFrom(start));
      base = std::move(ix);
      continue;
    }
    if (check(Tok::Dot)) {
      advance();
      auto m = makeNode<MemberExpr>(base->Range);
      m->Base = std::move(base);
      // `value.$name()` asks for a member the compiler provides rather than
      // one the type declares — the sigil keeps the two namespaces apart.
      if (check(Tok::Dollar)) {
        m->IsIntrinsic = true;
        advance();
      }
      if (check(Tok::IntLiteral)) {
        m->IsTupleIndex = true;
        m->TupleIndex = static_cast<uint32_t>(cur().IntValue);
        m->Name = cur().Text;
        m->NameRange = here();
        advance();
      } else if (check(Tok::Identifier)) {
        m->Name = cur().Text;
        m->NameRange = here();
        advance();
      } else if (isKeyword(cur().Kind)) {
        m->Name = tokenSpelling(cur().Kind);
        m->NameRange = here();
        advance();
      } else {
        expect(Tok::Identifier, "a member access");
      }
      // `value.$move()` hands a `Unique` on explicitly. Assignment and
      // `return` already move on their own; this is for the cases where
      // neither applies, or where saying so reads better.
      if (m->IsIntrinsic && m->Name == "move" && check(Tok::LParen) &&
          peek(1).is(Tok::RParen)) {
        advance();
        advance();
        auto mv = makeNode<MoveExpr>(m->Range);
        mv->Operand = std::move(m->Base);
        mv->Range = m->Range.merge(rangeFrom(Pos - 1));
        base = std::move(mv);
        continue;
      }
      if (check(Tok::ColonColon) && peek(1).is(Tok::Lt)) {
        advance();
        m->GenericArgs = parseGenericArgs();
      } else if (check(Tok::ColonColon) && peek(1).is(Tok::Identifier)) {
        // `value.field::name` — `::` walks namespaces, `.` reaches members,
        // and there is no namespace inside a value. This is almost always a
        // method call written with the wrong separator.
        Diags.error(here(), "`::` does not reach a member of a value")
            .note(fmt("write `.{}` instead — `::` is for module and type "
                      "paths, `.` for members",
                      peek(1).Text)
                      .c_str())
            .code(114);
        advance();
      }
      m->Range = m->Base->Range.merge(rangeFrom(start));
      base = std::move(m);
      continue;
    }
    if (check(Tok::Question)) {
      advance();
      auto t = makeNode<TryExpr>(base->Range);
      t->Operand = std::move(base);
      t->Range = t->Operand->Range.merge(rangeFrom(start));
      base = std::move(t);
      continue;
    }
    break;
  }
  return base;
}

ExprPtr Parser::parseClosure() {
  size_t start = Pos;
  // `move` is contextual: only the word immediately before `||` means this.
  bool isMove = false;
  if (check(Tok::KwMove) && peek(1).is(Tok::PipePipe)) {
    isMove = true;
    advance();
  }
  expect(Tok::PipePipe, "a closure");
  auto c = makeNode<ClosureExpr>(here());
  c->IsMove = isMove;
  bool variadic = false;
  if (check(Tok::LParen))
    parseParamList(c->Params, /*allowSelf=*/false, variadic);
  if (match(Tok::Arrow))
    c->ReturnType = parseType();
  skipNewlines();
  bool savedNoStruct = NoStructLiteral;
  NoStructLiteral = false;
  c->Body = parseBlock("a closure body");
  NoStructLiteral = savedNoStruct;
  c->Range = rangeFrom(start);
  return c;
}

ExprPtr Parser::parseCondition(PatternPtr &binding) {
  bool savedNoStruct = NoStructLiteral;
  bool savedStopIs = StopAtIs;
  NoStructLiteral = true;
  StopAtIs = true;
  ExprPtr cond = parseExpr();
  if (check(Tok::KwIs)) {
    advance();
    StopAtIs = false;
    binding = parsePattern();
  }
  NoStructLiteral = savedNoStruct;
  StopAtIs = savedStopIs;
  return cond;
}

ExprPtr Parser::parseIf() {
  size_t start = Pos;
  advance(); // if / elif
  auto ifExpr = makeNode<IfExpr>(here());
  ifExpr->Cond = parseCondition(ifExpr->BindingPat);
  skipNewlines();
  ifExpr->Then = parseBlock("an if body");

  // `else` and `elif` may sit on the line after the closing brace.
  size_t save = Pos;
  skipNewlines();
  if (check(Tok::KwElif)) {
    ifExpr->Else = parseIf();
  } else if (check(Tok::KwElse)) {
    advance();
    skipNewlines();
    if (check(Tok::KwIf)) {
      ifExpr->Else = parseIf();
    } else {
      ifExpr->Else = parseBlock("an else body");
    }
  } else {
    Pos = save;
  }
  ifExpr->Range = rangeFrom(start);
  return ifExpr;
}

ExprPtr Parser::parseWhile(std::string label) {
  size_t start = Pos;
  advance(); // while
  auto w = makeNode<WhileExpr>(here());
  w->Label = std::move(label);
  w->Cond = parseCondition(w->BindingPat);
  skipNewlines();
  ++LoopDepth;
  w->Body = parseBlock("a while body");
  --LoopDepth;
  w->Range = rangeFrom(start);
  return w;
}

ExprPtr Parser::parseLoop(std::string label) {
  size_t start = Pos;
  advance(); // loop
  auto l = makeNode<LoopExpr>(here());
  l->Label = std::move(label);
  skipNewlines();
  ++LoopDepth;
  l->Body = parseBlock("a loop body");
  --LoopDepth;
  l->Range = rangeFrom(start);
  return l;
}

ExprPtr Parser::parseFor(std::string label) {
  size_t start = Pos;
  advance(); // for
  auto f = makeNode<ForExpr>(here());
  f->Label = std::move(label);
  f->Binding = parsePattern();
  expect(Tok::KwIn, "a for loop");
  bool saved = NoStructLiteral;
  NoStructLiteral = true;
  f->Sequence = parseExpr();
  NoStructLiteral = saved;
  skipNewlines();
  ++LoopDepth;
  f->Body = parseBlock("a for body");
  --LoopDepth;
  f->Range = rangeFrom(start);
  return f;
}

ExprPtr Parser::parseMatch() {
  size_t start = Pos;
  advance(); // match
  auto m = makeNode<MatchExpr>(here());
  bool saved = NoStructLiteral;
  NoStructLiteral = true;
  m->Scrutinee = parseExpr();
  NoStructLiteral = saved;
  skipNewlines();
  if (!expect(Tok::LBrace, "a match body")) {
    m->Range = rangeFrom(start);
    return m;
  }
  skipSeparators();
  while (!check(Tok::RBrace) && !atEnd()) {
    MatchArm arm;
    size_t astart = Pos;
    arm.Pat = parsePattern();
    if (check(Tok::KwIf)) {
      advance();
      bool s2 = NoStructLiteral;
      NoStructLiteral = true;
      arm.Guard = parseExpr();
      NoStructLiteral = s2;
    }
    expect(Tok::FatArrow, "a match arm");
    skipNewlines();
    arm.Body = parseExpr();
    arm.Range = rangeFrom(astart);
    m->Arms.push_back(std::move(arm));
    // Arms are separated by commas, newlines, or both.
    if (!match(Tok::Comma) && !check(Tok::RBrace) && !check(Tok::Newline) &&
        !check(Tok::Semi) && !atEnd()) {
      Diags.error(cur().Range, "Expected '{}' — Got: '{}'", ",",
                  cur().Text.empty() ? tokenSpelling(cur().Kind) : cur().Text)
          .note("match arms are separated by `,` or a line break")
          .code(114);
      synchronize();
    }
    skipSeparators();
  }
  expect(Tok::RBrace, "a match body");
  m->Range = rangeFrom(start);
  return m;
}

ExprPtr Parser::parsePrimaryExpr() {
  // `move ||(...)` is a closure that copies its captures; `move value` hands
  // on a `uniq` reference. The token after it tells the two apart.
  if (check(Tok::KwMove)) {
    if (peek(1).is(Tok::PipePipe))
      return parseClosure();
    size_t mstart = Pos;
    advance();
    auto m = makeNode<MoveExpr>(here());
    m->Operand = parseUnaryExpr();
    m->Range = rangeFrom(mstart);
    return m;
  }
  size_t start = Pos;

  switch (cur().Kind) {
  case Tok::IntLiteral: {
    auto e = makeNode<IntLitExpr>(here());
    e->Value = cur().IntValue;
    e->Suffix = cur().Suffix;
    advance();
    e->Range = rangeFrom(start);
    return e;
  }
  case Tok::FloatLiteral: {
    auto e = makeNode<FloatLitExpr>(here());
    e->Value = cur().FloatValue;
    e->Suffix = cur().Suffix;
    advance();
    e->Range = rangeFrom(start);
    return e;
  }
  case Tok::StringLiteral: {
    auto e = makeNode<StringLitExpr>(here());
    e->Value = cur().Text;
    advance();
    e->Range = rangeFrom(start);
    return e;
  }
  case Tok::CharLiteral: {
    auto e = makeNode<CharLitExpr>(here());
    e->Value = static_cast<uint32_t>(cur().IntValue);
    advance();
    e->Range = rangeFrom(start);
    return e;
  }
  case Tok::KwTrue:
  case Tok::KwFalse: {
    auto e = makeNode<BoolLitExpr>(here());
    e->Value = check(Tok::KwTrue);
    advance();
    e->Range = rangeFrom(start);
    return e;
  }
  case Tok::KwNil: {
    advance();
    return makeNode<NilLitExpr>(rangeFrom(start));
  }
  case Tok::KwSelfValue: {
    advance();
    return makeNode<SelfExpr>(rangeFrom(start));
  }
  case Tok::KwSuper: {
    advance();
    return makeNode<SuperExpr>(rangeFrom(start));
  }
  case Tok::PipePipe:
    return parseClosure();
  case Tok::KwIf:
    return parseIf();
  case Tok::KwMatch:
    return parseMatch();
  case Tok::KwLoop:
    return parseLoop({});
  case Tok::KwWhile:
    return parseWhile({});
  case Tok::KwFor:
    return parseFor({});
  case Tok::KwUnsafe: {
    advance();
    skipNewlines();
    auto u = makeNode<UnsafeBlockExpr>(here());
    bool saved = NoStructLiteral;
    NoStructLiteral = false;
    u->Body = parseBlock("an unsafe block");
    NoStructLiteral = saved;
    u->Range = rangeFrom(start);
    return u;
  }
  case Tok::KwReturn: {
    advance();
    auto r = makeNode<ReturnExpr>(here());
    if (startsExpression(cur().Kind))
      r->Value = parseExpr();
    r->Range = rangeFrom(start);
    return r;
  }
  case Tok::KwBreak: {
    advance();
    auto b = makeNode<BreakExpr>(here());
    if (match(Tok::Colon)) { // `break :outer`
      if (check(Tok::Identifier)) {
        b->Label = cur().Text;
        advance();
      } else {
        expect(Tok::Identifier, "a break label");
      }
    }
    if (startsExpression(cur().Kind))
      b->Value = parseExpr();
    b->Range = rangeFrom(start);
    return b;
  }
  case Tok::KwContinue: {
    advance();
    auto c = makeNode<ContinueExpr>(here());
    if (match(Tok::Colon)) {
      if (check(Tok::Identifier)) {
        c->Label = cur().Text;
        advance();
      } else {
        expect(Tok::Identifier, "a continue label");
      }
    }
    c->Range = rangeFrom(start);
    return c;
  }
  case Tok::LBrace: {
    bool saved = NoStructLiteral;
    NoStructLiteral = false;
    auto b = parseBlock("a block expression");
    NoStructLiteral = saved;
    return b;
  }
  case Tok::LParen: {
    advance();
    skipNewlines();
    bool saved = NoStructLiteral;
    bool savedIs = StopAtIs;
    NoStructLiteral = false;
    StopAtIs = false;
    if (check(Tok::RParen)) { // unit value
      advance();
      NoStructLiteral = saved;
      StopAtIs = savedIs;
      return makeNode<TupleLitExpr>(rangeFrom(start));
    }
    ExprPtr first = parseExpr();
    skipNewlines();
    if (check(Tok::Comma)) {
      auto t = makeNode<TupleLitExpr>(here());
      t->Elements.push_back(std::move(first));
      while (match(Tok::Comma)) {
        skipNewlines();
        if (check(Tok::RParen))
          break;
        t->Elements.push_back(parseExpr());
        skipNewlines();
      }
      expect(Tok::RParen, "a tuple expression");
      NoStructLiteral = saved;
      StopAtIs = savedIs;
      t->Range = rangeFrom(start);
      return t;
    }
    expect(Tok::RParen, "a parenthesised expression");
    NoStructLiteral = saved;
    StopAtIs = savedIs;
    return first;
  }
  case Tok::LBracket: {
    advance();
    skipNewlines();
    bool saved = NoStructLiteral;
    NoStructLiteral = false;
    auto a = makeNode<ArrayLitExpr>(here());
    if (!check(Tok::RBracket)) {
      a->Elements.push_back(parseExpr());
      skipNewlines();
      if (check(Tok::Semi)) { // `[value; count]`
        advance();
        skipNewlines();
        a->RepeatCount = parseExpr();
        skipNewlines();
      } else {
        while (match(Tok::Comma)) {
          skipNewlines();
          if (check(Tok::RBracket))
            break;
          a->Elements.push_back(parseExpr());
          skipNewlines();
        }
      }
    }
    expect(Tok::RBracket, "an array literal");
    NoStructLiteral = saved;
    a->Range = rangeFrom(start);
    return a;
  }
  case Tok::Identifier:
  case Tok::KwSelfType: {
    SourceRange pathRange;
    std::vector<std::string> path = parsePath(pathRange);
    std::vector<TypeReprPtr> generics;
    if (check(Tok::ColonColon) && peek(1).is(Tok::Lt)) { // turbofish
      advance();
      generics = parseGenericArgs();
    } else if (check(Tok::Lt)) {
      // `Wrapper<i64> { ... }`, `make<i64>(...)` and `Option<i64>::Some` are
      // generic uses, but `a < b` is a comparison. Speculatively parse the
      // argument list and keep it only when the follow token confirms it.
      size_t save = Pos;
      Diags.beginSpeculation();
      std::vector<TypeReprPtr> speculative = parseGenericArgs();
      Diags.endSpeculation();
      bool looksGeneric =
          !speculative.empty() &&
          ((check(Tok::LBrace) && !NoStructLiteral) || check(Tok::LParen) ||
           check(Tok::ColonColon));
      if (looksGeneric)
        generics = std::move(speculative);
      else
        Pos = save;
    }
    // Generic arguments may be followed by more path segments, as in
    // `Option::<i64>::Some` or `Option<i64>::Some`.
    while (check(Tok::ColonColon) && peek(1).is(Tok::Identifier)) {
      advance();
      path.push_back(cur().Text);
      advance();
    }
    if (!NoStructLiteral && check(Tok::LBrace)) {
      advance();
      skipNewlines();
      auto lit = makeNode<StructLitExpr>(here());
      lit->Path = std::move(path);
      lit->PathRange = pathRange;
      lit->GenericArgs = std::move(generics);
      while (!check(Tok::RBrace) && !atEnd()) {
        if (check(Tok::DotDot)) {
          advance();
          lit->Base = parseExpr();
          skipNewlines();
          break;
        }
        StructLitField f;
        size_t fstart = Pos;
        if (check(Tok::Identifier)) {
          f.Name = cur().Text;
          advance();
        } else {
          expect(Tok::Identifier, "a struct literal field");
          break;
        }
        if (match(Tok::Colon)) {
          skipNewlines();
          f.Value = parseExpr();
        }
        f.Range = rangeFrom(fstart);
        lit->Fields.push_back(std::move(f));
        skipNewlines();
        if (!match(Tok::Comma))
          break;
        skipNewlines();
      }
      expect(Tok::RBrace, "a struct literal");
      lit->Range = rangeFrom(start);
      return lit;
    }
    auto ref = makeNode<DeclRefExpr>(here());
    ref->Path = std::move(path);
    ref->GenericArgs = std::move(generics);
    ref->Range = rangeFrom(start);
    return ref;
  }
  default:
    break;
  }

  const Token &t = cur();
  Diags.error(t.Range, "Expected '{}' — Got: '{}'", "an expression",
              t.Text.empty() ? tokenSpelling(t.Kind) : t.Text)
      .note("an expression starts with a literal, a name, `(`, `[`, `{`, a "
            "unary operator, or a keyword like `if` or `match`")
      .code(115);
  advance();
  return makeNode<ErrorExpr>(rangeFrom(start));
}

} // namespace rune
