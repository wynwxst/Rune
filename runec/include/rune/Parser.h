//===- Parser.h - Recursive-descent parser for Rune ------------*- C++ -*-===//
//
// Hand-written recursive descent with operator precedence for binary
// expressions. The parser never throws: on a syntax error it emits a
// diagnostic, drops an ErrorExpr into the tree and resynchronises at the next
// statement or declaration boundary, so one bad line does not cascade.
//
//===----------------------------------------------------------------------===//
#ifndef RUNE_PARSER_H
#define RUNE_PARSER_H

#include "rune/AST.h"
#include "rune/Macro.h"
#include "rune/Diagnostics.h"
#include "rune/Token.h"

#include <memory>
#include <string>
#include <vector>

namespace rune {

class Parser {
public:
  /// `macros` is the table built from every file in the compilation, so a
  /// macro written in one module can be used from another. Without one, this
  /// file's own definitions are all that is in scope.
  /// `macros` is the table built from every file in the compilation, so a
  /// macro written in one module can be used from another. Passing none
  /// leaves this file's own definitions as all that is in scope.
  Parser(const SourceManager &sm, DiagnosticEngine &diags, unsigned fileID,
         std::string moduleName, const MacroTable *macros = nullptr,
         const MacroPackage *procs = nullptr);

  /// The same, over tokens somebody has already produced for this file.
  /// Macros are gathered from every file before any is parsed, so the whole
  /// compilation is lexed once whether it likes it or not; handing that work
  /// back here is what stops it being done a second time.
  Parser(const SourceManager &sm, DiagnosticEngine &diags, unsigned fileID,
         std::string moduleName, std::vector<Token> tokens,
         const MacroTable *macros, const MacroPackage *procs = nullptr);

  /// Parses the whole file. Always returns a module, possibly with holes.
  std::unique_ptr<Module> parseModule();

private:
  const SourceManager &SM;
  DiagnosticEngine &Diags;
  unsigned FileID;
  std::string ModuleName;
  std::vector<Token> Toks;
  size_t Pos = 0;

  /// Set while parsing the head of `if`/`while`/`for`/`match`, where a `{`
  /// always opens the body rather than a struct literal.
  bool NoStructLiteral = false;
  /// Set in condition position so `x is Pattern` can bind, rather than being
  /// consumed as an ordinary `is` type test.
  bool StopAtIs = false;
  /// Depth of enclosing loops; used to reject stray break/continue early.
  unsigned LoopDepth = 0;
  /// Whether the statement just parsed ended with an explicit `;`. A trailing
  /// expression only becomes a block's value when it did not.
  bool LastHadSemi = false;
  /// Set while parsing the body of an `async fn`, an `async ||` closure or an
  /// `async { }` block — directly inside it, not inside a plain closure or a
  /// nested `fn` written there. `.await` is only allowed where this is set.
  bool InAsyncBody = false;
  /// Types declared inside an `extern "C++"` block. They belong to the module
  /// like any other type, so `parseModule` moves them into its declaration
  /// list right after the block that declared them.
  std::vector<DeclPtr> Hoisted;

  //=== Token access ======================================================//
  const Token &cur() const { return Toks[Pos]; }
  const Token &peek(size_t n = 1) const {
    size_t i = Pos + n;
    return i < Toks.size() ? Toks[i] : Toks.back();
  }
  bool check(Tok k) const { return cur().is(k); }
  bool checkAhead(size_t n, Tok k) const { return peek(n).is(k); }
  const Token &advance() { return Toks[Pos < Toks.size() - 1 ? Pos++ : Pos]; }
  bool match(Tok k) {
    if (!check(k)) return false;
    advance();
    return true;
  }
  bool atEnd() const { return cur().is(Tok::EndOfFile); }

  /// Consumes `k` or reports "expected X, got Y" pointing at the current token.
  bool expect(Tok k, const char *context);
  /// True when the tokens from `n` ahead start a function: `fn`, or `async fn`.
  bool atFunctionStart(size_t n = 0) const {
    return peek(n).is(Tok::KwFn) ||
           (peek(n).is(Tok::KwAsync) && peek(n + 1).is(Tok::KwFn));
  }
  /// Splits `>>` / `>>=` when closing nested generic argument lists.
  bool consumeCloseAngle();

  void skipNewlines() {
    while (check(Tok::Newline))
      advance();
  }
  /// Newlines that are only separators (inside braces, argument lists, ...).
  void skipSeparators() {
    while (check(Tok::Newline) || check(Tok::Semi))
      advance();
  }
  /// At a `(`, `[` or `{`: steps past it and its matching close.
  void skipBalanced() {
    int depth = 0;
    do {
      if (check(Tok::LParen) || check(Tok::LBracket) || check(Tok::LBrace))
        ++depth;
      else if (check(Tok::RParen) || check(Tok::RBracket) || check(Tok::RBrace))
        --depth;
      advance();
    } while (depth > 0 && !atEnd());
  }
  /// At the `@` of an `#lint(...)`: whether it is the file's own, which it is
  /// when every decorator after it is followed by an `import` or by the end
  /// of the file — nothing it could be attached to instead.
  bool lintIsFileDirective() const {
    size_t i = Pos;
    auto at = [&](size_t k) -> const Token & {
      return k < Toks.size() ? Toks[k] : Toks.back();
    };
    // Over this directive and any after it, arguments and all.
    while (at(i).is(Tok::At) || at(i).is(Tok::Hash)) {
      i += 2;
      if (at(i).is(Tok::LParen)) {
        int depth = 0;
        do {
          if (at(i).is(Tok::LParen)) ++depth;
          else if (at(i).is(Tok::RParen)) --depth;
          ++i;
        } while (depth > 0 && !at(i).is(Tok::EndOfFile));
      }
      while (at(i).is(Tok::Newline) || at(i).is(Tok::Semi))
        ++i;
    }
    return at(i).is(Tok::KwImport) || at(i).is(Tok::EndOfFile);
  }
  /// Consumes an explicit or inferred statement terminator.
  bool expectTerminator(const char *context);
  /// Skips forward to something that plausibly starts a new statement.
  void synchronize();

  SourceRange rangeFrom(size_t startTok) const;
  SourceRange here() const { return cur().Range; }

  /// True when what follows the `{` of a `Name { ... }` is a field rather
  /// than a value — which is what tells a struct literal from a builder
  /// block.
  bool atStructLiteralField() const;
  /// `[k: v, ...]`, rewritten into the `dictionary::mapOf` call it stands
  /// for. The first key has already been read.
  ExprPtr parseMapLiteral(ExprPtr firstKey, size_t start);
  /// `[:]`, rewritten into `dictionary::emptyMap()`.
  ExprPtr makeEmptyMapLiteral(SourceRange range);
  /// `Body { Text("hi"); Button {} }`, rewritten into the block it stands
  /// for. See the definition for the shape.
  ExprPtr parseBuilderBlock(std::vector<std::string> path,
                            SourceRange pathRange,
                            std::vector<TypeReprPtr> generics, size_t start);
  /// Nesting depth of builder blocks, so each one's hidden local has a name
  /// of its own and an inner block does not shadow the one outside it.
  unsigned BuilderDepth = 0;

  //=== Declarations ======================================================//
  DeclPtr parseTopLevelDecl();
  DeclPtr parseDecl(std::vector<Attribute> attrs, bool isPublic);
  void parseFileDirectives(Module &mod);
  /// `#link(...)` or `#linkpath(...)` at the sigil: the names and paths it
  /// gives, `static:` / `dynamic:` / `framework:` in front of a library
  /// when it says `type:`.
  void parseLinkDirective(const std::string &name, std::vector<std::string> &libs,
                          std::vector<std::string> &paths);
  /// Whether the decorator at the sigil is `#link` or `#linkpath`.
  bool atLinkDirective() const {
    return atDecorator() && peek(1).is(Tok::Identifier) &&
           (peek(1).Text == "link" || peek(1).Text == "linkpath");
  }
  /// Conditional link directives met while parsing, for the module.
  std::vector<Module::ConditionalLink> PendingLinks;
  std::vector<Attribute> parseAttributes();
  /// At `@` or `#`, the two ways a decorator begins.
  bool atDecorator() const { return check(Tok::At) || check(Tok::Hash); }
  /// Says so when `sigil` does not suit `name`: `#` is only for the
  /// compiler's own decorators, and those are written with it. False when
  /// the decorator is to be dropped.
  bool checkDecoratorSigil(const Token &sigil, const std::string &name,
                           SourceRange at);
  std::unique_ptr<FunctionDecl> parseFunction(std::vector<Attribute> attrs,
                                              bool isPublic, bool allowNoBody,
                                              bool implicitFnKeyword = false);
  std::unique_ptr<StructDecl> parseStruct(std::vector<Attribute> attrs, bool isPublic);
  std::unique_ptr<EnumDecl> parseEnum(std::vector<Attribute> attrs, bool isPublic);
  std::unique_ptr<ClassDecl> parseClass(std::vector<Attribute> attrs, bool isPublic);
  std::unique_ptr<MarkDecl> parseMark(std::vector<Attribute> attrs, bool isPublic);
  std::unique_ptr<BindDecl> parseBind(std::vector<Attribute> attrs, bool isPublic);
  /// The `where` clauses and `{ ... }` of a bind, once the mark and the
  /// target have been read.
  std::unique_ptr<BindDecl> parseBindBody(std::unique_ptr<BindDecl> b,
                                          size_t start);
  std::unique_ptr<ExtendDecl> parseExtend(std::vector<Attribute> attrs, bool isPublic);
  std::unique_ptr<ImportDecl> parseImport(std::vector<Attribute> attrs, bool isPublic);
  std::unique_ptr<ExternDecl> parseExtern(std::vector<Attribute> attrs, bool isPublic);
  /// The items of an `extern "C++"` block or of a `namespace` inside one,
  /// up to the closing brace. `scope` is the C++ path they live under.
  void parseCxxItems(ExternDecl &ext, std::vector<std::string> &scope,
                     bool isPublic);
  /// `class Name : Base { ... }` / `struct Name<T> { ... }` inside
  /// `extern "C++"`. The type goes to `Hoisted`; nested types recurse.
  void parseCxxType(ExternDecl &ext, const std::vector<std::string> &scope,
                    std::vector<Attribute> attrs, bool isPublic);
  std::unique_ptr<TypeAliasDecl> parseTypeAlias(std::vector<Attribute> attrs, bool isPublic);
  std::unique_ptr<GlobalVarDecl> parseGlobalVar(std::vector<Attribute> attrs, bool isPublic);

  TypeReprPtr parseBound();
  std::vector<GenericParam> parseGenericParams();
  std::vector<WhereClause> parseWhereClauses();
  /// `allowUntyped` lets a parameter be written as a bare name, which only a
  /// closure may do: what its type is, is whatever the place the closure is
  /// going says it is.
  bool parseParamList(std::vector<Param> &out, bool allowSelf, bool &isVariadic,
                      bool allowUntyped = false);
  std::unique_ptr<FieldDecl> parseField(bool isPublic);

  //=== Statements ========================================================//
  StmtPtr parseStatement();
  std::unique_ptr<BlockExpr> parseBlock(const char *context);
  StmtPtr parseVarStatement(bool isGlobal);
  /// True when the tokens at `Pos` start a `name: Type ...` binding.
  bool looksLikeTypedBinding() const;
  bool looksLikeLoopLabel() const;

  //=== Expressions =======================================================//
  ExprPtr parseExpr();
  ExprPtr parseAssignExpr();
  ExprPtr parseRangeExpr();
  ExprPtr parseBinaryExpr(int minPrec);
  ExprPtr parseUnaryExpr();
  ExprPtr parsePostfixExpr(ExprPtr base);
  ExprPtr parsePrimaryExpr();
  ExprPtr parseClosure();
  /// `async { ... }`, `async ||(...) { ... }` and `async move ||(...) { ... }`.
  ExprPtr parseAsyncExpr();

  //=== `async`, rewritten =================================================//
  //
  // An `async fn f(a: A) -> T { body }` is parsed as written and then
  // rewritten into what it means:
  //
  //     fn f(a: A) -> std::task::Future<T> {
  //         std::task::spawn(move ||() -> T { body })
  //     }
  //
  // The closure captures the parameters — and `self` — by value, the way any
  // closure does, and `spawn` starts it on a stack of its own. An `async ||`
  // closure and an `async { }` block are the same rewrite without the
  // function around it. Nothing later in the compiler knows the keyword
  // existed: the type system, generics and libraries see an ordinary
  // function whose result happens to be a `Future`.
  //
  /// `std::task::Future<result>`, where a null `result` means `()`.
  TypeReprPtr futureTypeReprFor(TypeReprPtr result, SourceRange at);
  /// `std::task::spawn(move ||() -> result { body })`.
  ExprPtr spawnExprFor(std::unique_ptr<BlockExpr> body, TypeReprPtr result,
                       SourceRange at);
  /// Parses a body with `InAsyncBody` set to `async`, restoring it after.
  std::unique_ptr<BlockExpr> parseBodyInAsync(const char *context, bool async);
  ExprPtr parseIf();
  ExprPtr parseWhile(std::string label);
  ExprPtr parseLoop(std::string label);
  ExprPtr parseFor(std::string label);
  ExprPtr parseMatch();
  /// Parses a condition, allowing the `expr is pattern` binding form.
  ExprPtr parseCondition(PatternPtr &binding);
  bool parseCallArgs(std::vector<Argument> &out);

  //=== Types =============================================================//
  TypeReprPtr parseType();
  TypeReprPtr parseTypeNoSuffix();
  /// The `from …` clause after a reference type, or null when there is none.
  std::unique_ptr<OriginClause> parseOriginClause();
  /// `a.b.c`; the first step may be `self`. False (with a report) when the
  /// current token cannot start one.
  bool parseFieldPath(std::vector<std::string> &path, SourceRange &range);
  /// `{ field, other.sub }` after a parameter, when there is one.
  void parseView(Param &p);
  std::vector<TypeReprPtr> parseGenericArgs();
  std::vector<std::string> parsePath(SourceRange &range);

  //=== Patterns ==========================================================//
  PatternPtr parsePattern();
  PatternPtr parsePatternNoOr();
  PatternPtr parsePatternPrimary();
};

} // namespace rune

#endif
