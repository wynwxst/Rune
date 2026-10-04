//===- ASTWalk.cpp - One place that knows what a node contains --*- C++ -*-===//
//
// `forEachChild` visits the sub-nodes of one node and nothing deeper, so a
// caller writes the recursion it actually wants. Every pass that has to look
// at a whole body — the `#resource` check, the ownership pass — uses this
// rather than repeating a switch over every node kind, which is the kind of
// list that goes stale the first time a node gains a field.
//
// Declarations nested in a body are deliberately not descended into: a
// function declared inside another is checked on its own, and walking into it
// here would attribute its uses to the enclosing body.
//
//===----------------------------------------------------------------------===//
#include "rune/ASTWalk.h"

namespace rune {

void forEachChild(const Node *n, const NodeVisitor &visit) {
  if (!n)
    return;
  auto go = [&](const Node *c) { if (c) visit(c); };

  switch (n->Kind) {
  //=== Leaves ===========================================================//
  case NodeKind::IntLit:
  case NodeKind::FloatLit:
  case NodeKind::StringLit:
  case NodeKind::CharLit:
  case NodeKind::BoolLit:
  case NodeKind::NilLit:
  case NodeKind::SelfRef:
  case NodeKind::SuperRef:
  case NodeKind::Continue:
  case NodeKind::Error:
  case NodeKind::WildcardPat:
  case NodeKind::PathPat:
    return;

  //=== Expressions ======================================================//
  case NodeKind::DeclRef:
    return;
  case NodeKind::ArrayLit: {
    const auto *a = cast<ArrayLitExpr>(n);
    for (const auto &e : a->Elements) go(e.get());
    go(a->RepeatCount.get());
    return;
  }
  case NodeKind::TupleLit:
    for (const auto &e : cast<TupleLitExpr>(n)->Elements) go(e.get());
    return;
  case NodeKind::StructLit: {
    const auto *s = cast<StructLitExpr>(n);
    for (const auto &f : s->Fields) go(f.Value.get());
    go(s->Base.get());
    return;
  }
  case NodeKind::Unary:
    go(cast<UnaryExpr>(n)->Operand.get());
    return;
  case NodeKind::Binary: {
    const auto *b = cast<BinaryExpr>(n);
    go(b->LHS.get()); go(b->RHS.get());
    return;
  }
  case NodeKind::Assign: {
    const auto *a = cast<AssignExpr>(n);
    go(a->LHS.get()); go(a->RHS.get());
    return;
  }
  case NodeKind::Call: {
    const auto *c = cast<CallExpr>(n);
    go(c->Callee.get());
    for (const auto &a : c->Args) go(a.Value.get());
    return;
  }
  case NodeKind::Member:
    go(cast<MemberExpr>(n)->Base.get());
    return;
  case NodeKind::Index: {
    const auto *i = cast<IndexExpr>(n);
    go(i->Base.get()); go(i->Index.get());
    return;
  }
  case NodeKind::Cast:     go(cast<CastExpr>(n)->Operand.get()); return;
  case NodeKind::Into:     go(cast<IntoExpr>(n)->Operand.get()); return;
  case NodeKind::TypeTest: go(cast<TypeTestExpr>(n)->Operand.get()); return;
  case NodeKind::Borrow:   go(cast<BorrowExpr>(n)->Operand.get()); return;
  case NodeKind::Deref:    go(cast<DerefExpr>(n)->Operand.get()); return;
  case NodeKind::Move:     go(cast<MoveExpr>(n)->Operand.get()); return;
  case NodeKind::Try:      go(cast<TryExpr>(n)->Operand.get()); return;
  case NodeKind::Return:   go(cast<ReturnExpr>(n)->Value.get()); return;
  case NodeKind::Break:    go(cast<BreakExpr>(n)->Value.get()); return;
  case NodeKind::UnsafeBlock: go(cast<UnsafeBlockExpr>(n)->Body.get()); return;
  case NodeKind::Closure:  go(cast<ClosureExpr>(n)->Body.get()); return;
  case NodeKind::Range: {
    const auto *r = cast<RangeExpr>(n);
    go(r->Lo.get()); go(r->Hi.get());
    return;
  }
  case NodeKind::Block: {
    const auto *b = cast<BlockExpr>(n);
    for (const auto &st : b->Stmts) go(st.get());
    go(b->Tail.get());
    return;
  }
  case NodeKind::If: {
    const auto *i = cast<IfExpr>(n);
    go(i->Cond.get()); go(i->BindingPat.get());
    go(i->Then.get()); go(i->Else.get());
    return;
  }
  case NodeKind::While: {
    const auto *w = cast<WhileExpr>(n);
    go(w->Cond.get()); go(w->BindingPat.get()); go(w->Body.get());
    return;
  }
  case NodeKind::Loop:
    go(cast<LoopExpr>(n)->Body.get());
    return;
  case NodeKind::For: {
    const auto *f = cast<ForExpr>(n);
    go(f->Binding.get()); go(f->Sequence.get()); go(f->Body.get());
    return;
  }
  case NodeKind::Match: {
    const auto *m = cast<MatchExpr>(n);
    go(m->Scrutinee.get());
    for (const auto &arm : m->Arms) {
      go(arm.Pat.get()); go(arm.Guard.get()); go(arm.Body.get());
    }
    return;
  }

  //=== Statements =======================================================//
  case NodeKind::ExprStmt:  go(cast<ExprStmt>(n)->Value.get()); return;
  case NodeKind::DeferStmt: go(cast<DeferStmtNode>(n)->Body.get()); return;
  case NodeKind::VarStmt: {
    const auto *v = cast<VarStmtNode>(n);
    go(v->Binding.get()); go(v->Init.get());
    return;
  }
  // A declaration inside a body is checked in its own right.
  case NodeKind::DeclStmtKind:
    return;

  //=== Patterns =========================================================//
  case NodeKind::BindingPat: go(cast<BindingPattern>(n)->Sub.get()); return;
  case NodeKind::LiteralPat: go(cast<LiteralPattern>(n)->Value.get()); return;
  case NodeKind::TuplePat:
    for (const auto &e : cast<TuplePattern>(n)->Elements) go(e.get());
    return;
  case NodeKind::StructPat:
    for (const auto &f : cast<StructPattern>(n)->Fields) go(f.Value.get());
    return;
  case NodeKind::EnumPat:
    for (const auto &e : cast<EnumPattern>(n)->Elements) go(e.get());
    return;
  case NodeKind::RangePat: {
    const auto *r = cast<RangePattern>(n);
    go(r->Lo.get()); go(r->Hi.get());
    return;
  }
  case NodeKind::OrPat:
    for (const auto &a : cast<OrPattern>(n)->Alternatives) go(a.get());
    return;
  case NodeKind::RefPat: go(cast<RefPattern>(n)->Sub.get()); return;
  case NodeKind::SlicePat: {
    const auto *s = cast<SlicePattern>(n);
    for (const auto &e : s->Prefix) go(e.get());
    go(s->Rest.get());
    for (const auto &e : s->Suffix) go(e.get());
    return;
  }

  //=== Everything else ==================================================//
  // Declarations and type expressions have no children a body walk wants.
  default:
    return;
  }
}

} // namespace rune
