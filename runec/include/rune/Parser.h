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
         std::string moduleName, const MacroTable *macros = nullptr);

  /// The same, over tokens somebody has already produced for this file.
  /// Macros are gathered from every file before any is parsed, so the whole
  /// compilation is lexed once whether it likes it or not; handing that work
  /// back here is what stops it being done a second time.
  Parser(const SourceManager &sm, DiagnosticEngine &diags, unsigned fileID,
         std::string moduleName, std::vector<Token> tokens,
         const MacroTable *macros);

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
  /// Consumes an explicit or inferred statement terminator.
  bool expectTerminator(const char *context);
  /// Skips forward to something that plausibly starts a new statement.
  void synchronize();

  SourceRange rangeFrom(size_t startTok) const;
  SourceRange here() const { return cur().Range; }

  //=== Declarations ======================================================//
  DeclPtr parseTopLevelDecl();
  DeclPtr parseDecl(std::vector<Attribute> attrs, bool isPublic);
  void parseFileDirectives(Module &mod);
  std::vector<Attribute> parseAttributes();
  std::unique_ptr<FunctionDecl> parseFunction(std::vector<Attribute> attrs,
                                              bool isPublic, bool allowNoBody,
                                              bool implicitFnKeyword = false);
  std::unique_ptr<StructDecl> parseStruct(std::vector<Attribute> attrs, bool isPublic);
  std::unique_ptr<EnumDecl> parseEnum(std::vector<Attribute> attrs, bool isPublic);
  std::unique_ptr<ClassDecl> parseClass(std::vector<Attribute> attrs, bool isPublic);
  std::unique_ptr<MarkDecl> parseMark(std::vector<Attribute> attrs, bool isPublic);
  std::unique_ptr<BindDecl> parseBind(std::vector<Attribute> attrs, bool isPublic);
  std::unique_ptr<ExtendDecl> parseExtend(std::vector<Attribute> attrs, bool isPublic);
  std::unique_ptr<ImportDecl> parseImport(std::vector<Attribute> attrs, bool isPublic);
  std::unique_ptr<ExternDecl> parseExtern(std::vector<Attribute> attrs, bool isPublic);
  std::unique_ptr<TypeAliasDecl> parseTypeAlias(std::vector<Attribute> attrs, bool isPublic);
  std::unique_ptr<GlobalVarDecl> parseGlobalVar(std::vector<Attribute> attrs, bool isPublic);

  TypeReprPtr parseBound();
  std::vector<GenericParam> parseGenericParams();
  std::vector<WhereClause> parseWhereClauses();
  bool parseParamList(std::vector<Param> &out, bool allowSelf, bool &isVariadic);
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
  std::vector<TypeReprPtr> parseGenericArgs();
  std::vector<std::string> parsePath(SourceRange &range);

  //=== Patterns ==========================================================//
  PatternPtr parsePattern();
  PatternPtr parsePatternNoOr();
  PatternPtr parsePatternPrimary();
};

} // namespace rune

#endif
