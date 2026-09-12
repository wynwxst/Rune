//===- ASTClone.h - Deep copies of AST subtrees ----------------*- C++ -*-===//
//
// Generic instantiation works by cloning the template's tree and re-running
// semantic analysis over the copy with the type parameters bound. Clones drop
// every field Sema fills in (resolved types, resolved declarations, capture
// lists) so the copy starts from a clean slate.
//
//===----------------------------------------------------------------------===//
#ifndef RUNE_ASTCLONE_H
#define RUNE_ASTCLONE_H

#include "rune/AST.h"

namespace rune {

ExprPtr cloneExpr(const Expr *e);
StmtPtr cloneStmt(const Stmt *s);
DeclPtr cloneDecl(const Decl *d);
TypeReprPtr cloneTypeRepr(const TypeRepr *t);
PatternPtr clonePattern(const Pattern *p);
Param cloneParam(const Param &p);
Attribute cloneAttribute(const Attribute &a);

std::unique_ptr<FunctionDecl> cloneFunction(const FunctionDecl *f);
std::unique_ptr<BlockExpr> cloneBlock(const BlockExpr *b);

} // namespace rune

#endif
