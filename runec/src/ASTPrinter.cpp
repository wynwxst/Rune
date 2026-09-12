//===- ASTPrinter.cpp - Indented tree dump for --dump-ast ------*- C++ -*-===//

#include "rune/AST.h"
#include "rune/Source.h"

#include <ostream>

namespace rune {

namespace {

class Printer {
public:
  Printer(std::ostream &os, const SourceManager &sm) : OS(os), SM(sm) {}

  void module(const Module &m) {
    line("Module '" + m.Name + "'  (" + m.Path + ")");
    Indent += 2;
    for (const auto &d : m.Decls)
      decl(d.get());
    Indent -= 2;
  }

private:
  std::ostream &OS;
  const SourceManager &SM;
  unsigned Indent = 0;

  void line(const std::string &s) {
    OS << std::string(Indent, ' ') << s << "\n";
  }

  std::string loc(const Node *n) const {
    if (!n || !n->Range.isValid())
      return {};
    PresumedLoc pl = SM.decode(n->Range.begin());
    if (!pl.isValid())
      return {};
    return "  <" + std::to_string(pl.Line) + ":" + std::to_string(pl.Column) + ">";
  }

  struct Scope {
    Printer &P;
    explicit Scope(Printer &p) : P(p) { P.Indent += 2; }
    ~Scope() { P.Indent -= 2; }
  };

  void attrs(const std::vector<Attribute> &as) {
    for (const auto &a : as) {
      line("@" + a.Name + (a.Args.empty() ? "" : "(...)"));
    }
  }

  void generics(const std::vector<GenericParam> &gs) {
    if (gs.empty())
      return;
    std::string s = "generics: <";
    for (size_t i = 0; i < gs.size(); ++i) {
      if (i) s += ", ";
      s += gs[i].Name;
      if (!gs[i].Bounds.empty())
        s += ": " + std::to_string(gs[i].Bounds.size()) + " bound(s)";
    }
    line(s + ">");
  }

  //=== Types ============================================================//
  std::string typeStr(const TypeRepr *t) {
    if (!t)
      return "<inferred>";
    switch (t->Kind) {
    case NodeKind::NamedType: {
      const auto *n = cast<NamedTypeRepr>(t);
      std::string s;
      for (size_t i = 0; i < n->Path.size(); ++i) {
        if (i) s += "::";
        s += n->Path[i];
      }
      if (!n->GenericArgs.empty()) {
        s += "<";
        for (size_t i = 0; i < n->GenericArgs.size(); ++i) {
          if (i) s += ", ";
          s += typeStr(n->GenericArgs[i].get());
        }
        s += ">";
      }
      return s;
    }
    case NodeKind::PointerType: {
      const auto *p = cast<PointerTypeRepr>(t);
      std::string s = p->IsRaw ? "*" : "&";
      if (p->IsWeak) s += "weak ";
      if (p->IsMutable) s += "var ";
      return s + typeStr(p->Pointee.get());
    }
    case NodeKind::ArrayType: {
      const auto *a = cast<ArrayTypeRepr>(t);
      return "[N:" + typeStr(a->Element.get()) + "]";
    }
    case NodeKind::SliceType:
      return "[" + typeStr(cast<SliceTypeRepr>(t)->Element.get()) + "]";
    case NodeKind::TupleType: {
      const auto *tt = cast<TupleTypeRepr>(t);
      std::string s = "(";
      for (size_t i = 0; i < tt->Elements.size(); ++i) {
        if (i) s += ", ";
        s += typeStr(tt->Elements[i].get());
      }
      return s + ")";
    }
    case NodeKind::FunctionTypeRepr: {
      const auto *f = cast<FunctionTypeReprNode>(t);
      std::string s = "@function(";
      for (size_t i = 0; i < f->Params.size(); ++i) {
        if (i) s += ", ";
        s += typeStr(f->Params[i].get());
      }
      s += ")";
      if (f->ReturnType)
        s += " -> " + typeStr(f->ReturnType.get());
      return s;
    }
    case NodeKind::OptionalType:
      return typeStr(cast<OptionalTypeRepr>(t)->Element.get()) + "?";
    case NodeKind::DynType:
      return "dyn " + typeStr(cast<DynTypeRepr>(t)->MarkType.get());
    case NodeKind::SomeType:
      return "some " + typeStr(cast<SomeTypeRepr>(t)->MarkType.get());
    case NodeKind::UniqType:
      return "uniq " + typeStr(cast<UniqTypeRepr>(t)->Element.get());
    case NodeKind::SelfType:
      return "Self";
    case NodeKind::InferType:
      return "_";
    default:
      return "<type>";
    }
  }

  //=== Declarations =====================================================//
  void decl(const Decl *d) {
    if (!d) {
      line("<null decl>");
      return;
    }
    switch (d->Kind) {
    case NodeKind::Function: {
      const auto *f = cast<FunctionDecl>(d);
      std::string s = "Function '" + f->Name + "'";
      if (f->IsPublic) s += " pub";
      if (f->IsExtern) s += " extern(\"" + f->ExternABI + "\")";
      if (f->IsUnsafe) s += " unsafe";
      if (f->IsSafeJustified) s += " @safe(\"" + f->SafetyReason + "\")";
      if (f->IsVariadic) s += " variadic";
      line(s + loc(d));
      Scope sc(*this);
      // Prose the compiler kept, from `@Doc` or a `///` comment. Shown as one
      // line so a dump stays a dump. Only populated after Sema.
      if (!f->Doc.empty()) {
        std::string one;
        for (char c : f->Doc) one += (c == '\n') ? ' ' : c;
        if (one.size() > 60) one = one.substr(0, 57) + "...";
        line("doc \"" + one + "\"");
      }
      attrs(f->Attrs);
      generics(f->Generics);
      for (const Param &p : f->Params) {
        std::string ps = "param " + p.Name;
        if (p.IsSelf) {
          ps = "param self";
          if (p.SelfByRef) ps += " (&)";
          if (p.SelfMutable) ps += " (mut)";
        } else {
          ps += ": " + typeStr(p.TypeAnnotation.get());
          if (p.IsMutable) ps += " [var]";
          if (p.DefaultValue) ps += " = <default>";
        }
        line(ps);
      }
      line("returns: " + typeStr(f->ReturnType.get()));
      if (f->Body) {
        line("body:");
        Scope s2(*this);
        expr(f->Body.get());
      }
      break;
    }
    case NodeKind::Struct: {
      const auto *s = cast<StructDecl>(d);
      line("Struct '" + s->Name + "'" + (s->IsPublic ? " pub" : "") + loc(d));
      Scope sc(*this);
      attrs(s->Attrs);
      generics(s->Generics);
      for (const auto &f : s->Fields)
        line("field " + f->Name + ": " + typeStr(f->TypeAnnotation.get()) +
             (f->IsPublic ? " pub" : ""));
      for (const auto &m : s->Methods)
        decl(m.get());
      break;
    }
    case NodeKind::Enum: {
      const auto *e = cast<EnumDecl>(d);
      line("Enum '" + e->Name + "'" + (e->IsPublic ? " pub" : "") +
           (e->IsSimple ? " [simple]" : "") + loc(d));
      Scope sc(*this);
      attrs(e->Attrs);
      generics(e->Generics);
      for (const auto &v : e->Variants) {
        std::string s = "variant " + v->Name;
        if (v->Shape == VariantShape::Tuple) {
          s += "(";
          for (size_t i = 0; i < v->TupleTypes.size(); ++i) {
            if (i) s += ", ";
            s += typeStr(v->TupleTypes[i].get());
          }
          s += ")";
        } else if (v->Shape == VariantShape::Struct) {
          s += " { ";
          for (size_t i = 0; i < v->Fields.size(); ++i) {
            if (i) s += ", ";
            s += v->Fields[i]->Name + ": " + typeStr(v->Fields[i]->TypeAnnotation.get());
          }
          s += " }";
        }
        line(s);
      }
      for (const auto &m : e->Methods)
        decl(m.get());
      break;
    }
    case NodeKind::Class: {
      const auto *c = cast<ClassDecl>(d);
      std::string s = "Class '" + c->Name + "'";
      if (c->SuperClass) s += " : " + typeStr(c->SuperClass.get());
      if (c->IsPublic) s += " pub";
      line(s + loc(d));
      Scope sc(*this);
      attrs(c->Attrs);
      generics(c->Generics);
      for (const auto &f : c->Fields)
        line("field " + f->Name + ": " + typeStr(f->TypeAnnotation.get()) +
             (f->IsPublic ? " pub" : "") + (f->IsWeak ? " weak" : ""));
      for (const auto &m : c->Methods)
        decl(m.get());
      break;
    }
    case NodeKind::Mark: {
      const auto *m = cast<MarkDecl>(d);
      std::string s = "Mark '" + m->Name + "'";
      if (!m->SuperMarks.empty()) {
        s += " : ";
        for (size_t i = 0; i < m->SuperMarks.size(); ++i) {
          if (i) s += " + ";
          s += typeStr(m->SuperMarks[i].get());
        }
      }
      line(s + (m->IsPublic ? " pub" : "") + loc(d));
      Scope sc(*this);
      generics(m->Generics);
      for (const auto &f : m->Methods)
        decl(f.get());
      break;
    }
    case NodeKind::Bind: {
      const auto *b = cast<BindDecl>(d);
      std::string path;
      for (size_t i = 0; i < b->MarkPath.size(); ++i) {
        if (i) path += "::";
        path += b->MarkPath[i];
      }
      line("Bind " + path + " to " + typeStr(b->TargetType.get()) +
           (b->IsOperatorBinding ? "  [operator]" : "") + loc(d));
      Scope sc(*this);
      generics(b->Generics);
      for (const auto &m : b->Methods)
        decl(m.get());
      break;
    }
    case NodeKind::Extend: {
      const auto *e = cast<ExtendDecl>(d);
      line("Extend " + typeStr(e->TargetType.get()) + loc(d));
      Scope sc(*this);
      generics(e->Generics);
      for (const auto &m : e->Methods)
        decl(m.get());
      break;
    }
    case NodeKind::Import: {
      const auto *i = cast<ImportDecl>(d);
      std::string s = "Import ";
      for (size_t k = 0; k < i->Path.size(); ++k) {
        if (k) s += "::";
        s += i->Path[k];
      }
      if (i->IsGlob) s += "::*";
      if (!i->Names.empty()) {
        s += "::{";
        for (size_t k = 0; k < i->Names.size(); ++k) {
          if (k) s += ", ";
          s += i->Names[k];
        }
        s += "}";
      }
      if (!i->Alias.empty()) s += " as " + i->Alias;
      line(s + loc(d));
      break;
    }
    case NodeKind::Extern: {
      const auto *e = cast<ExternDecl>(d);
      line("Extern \"" + e->ABI + "\"" + loc(d));
      Scope sc(*this);
      for (const auto &f : e->Functions)
        decl(f.get());
      for (const auto &g : e->Globals)
        decl(g.get());
      break;
    }
    case NodeKind::TypeAlias: {
      const auto *t = cast<TypeAliasDecl>(d);
      line("TypeAlias '" + t->Name + "' = " + typeStr(t->Aliased.get()) + loc(d));
      break;
    }
    case NodeKind::GlobalVar: {
      const auto *g = cast<GlobalVarDecl>(d);
      line(std::string("Global ") + (g->IsMutable ? "var " : "") + g->Name + ": " +
           typeStr(g->TypeAnnotation.get()) + (g->IsPublic ? "  pub" : "") + loc(d));
      if (g->Init) {
        Scope sc(*this);
        expr(g->Init.get());
      }
      break;
    }
    default:
      line("<decl kind " + std::to_string(static_cast<int>(d->Kind)) + ">");
      break;
    }
  }

  //=== Patterns =========================================================//
  void pattern(const Pattern *p) {
    if (!p) {
      line("<null pattern>");
      return;
    }
    switch (p->Kind) {
    case NodeKind::WildcardPat:
      line("Wildcard `_`");
      break;
    case NodeKind::BindingPat: {
      const auto *b = cast<BindingPattern>(p);
      line("Binding '" + b->Name + "'" + (b->IsMutable ? " [var]" : ""));
      if (b->Sub) {
        Scope sc(*this);
        pattern(b->Sub.get());
      }
      break;
    }
    case NodeKind::LiteralPat: {
      line("LiteralPattern");
      Scope sc(*this);
      expr(cast<LiteralPattern>(p)->Value.get());
      break;
    }
    case NodeKind::TuplePat: {
      line("TuplePattern");
      Scope sc(*this);
      for (const auto &e : cast<TuplePattern>(p)->Elements)
        pattern(e.get());
      break;
    }
    case NodeKind::StructPat: {
      const auto *s = cast<StructPattern>(p);
      std::string path;
      for (size_t i = 0; i < s->Path.size(); ++i) {
        if (i) path += "::";
        path += s->Path[i];
      }
      line("StructPattern " + path + (s->HasRest ? " { .. }" : ""));
      Scope sc(*this);
      for (const auto &f : s->Fields) {
        line("field " + f.Name);
        if (f.Value) {
          Scope s2(*this);
          pattern(f.Value.get());
        }
      }
      break;
    }
    case NodeKind::EnumPat: {
      const auto *e = cast<EnumPattern>(p);
      std::string path;
      for (size_t i = 0; i < e->Path.size(); ++i) {
        if (i) path += "::";
        path += e->Path[i];
      }
      line("EnumPattern " + path);
      Scope sc(*this);
      for (const auto &el : e->Elements)
        pattern(el.get());
      break;
    }
    case NodeKind::PathPat: {
      const auto *pp = cast<PathPattern>(p);
      std::string path;
      for (size_t i = 0; i < pp->Path.size(); ++i) {
        if (i) path += "::";
        path += pp->Path[i];
      }
      line("PathPattern " + path);
      break;
    }
    case NodeKind::RangePat: {
      const auto *r = cast<RangePattern>(p);
      line(std::string("RangePattern") + (r->Inclusive ? " inclusive" : ""));
      Scope sc(*this);
      if (r->Lo) expr(r->Lo.get());
      if (r->Hi) expr(r->Hi.get());
      break;
    }
    case NodeKind::SlicePat: {
      const auto *s = cast<SlicePattern>(p);
      line(std::string("SlicePattern") + (s->HasRest ? " [..]" : ""));
      Scope sc(*this);
      for (const auto &e : s->Prefix)
        pattern(e.get());
      if (s->Rest) {
        line("rest");
        Scope s2(*this);
        pattern(s->Rest.get());
      }
      for (const auto &e : s->Suffix)
        pattern(e.get());
      break;
    }
    case NodeKind::OrPat: {
      line("OrPattern");
      Scope sc(*this);
      for (const auto &a : cast<OrPattern>(p)->Alternatives)
        pattern(a.get());
      break;
    }
    case NodeKind::RefPat: {
      const auto *r = cast<RefPattern>(p);
      line(std::string("RefPattern") + (r->IsMutable ? " var" : ""));
      Scope sc(*this);
      pattern(r->Sub.get());
      break;
    }
    default:
      line("<pattern>");
      break;
    }
  }

  //=== Statements =======================================================//
  void stmt(const Stmt *s) {
    if (!s) {
      line("<null stmt>");
      return;
    }
    switch (s->Kind) {
    case NodeKind::ExprStmt:
      expr(cast<ExprStmt>(s)->Value.get());
      break;
    case NodeKind::VarStmt: {
      const auto *v = cast<VarStmtNode>(s);
      line(std::string("VarDecl") + (v->IsMutable ? " var" : " let") +
           (v->IsGlobal ? " global" : "") + ": " +
           typeStr(v->TypeAnnotation.get()) + loc(s));
      Scope sc(*this);
      pattern(v->Binding.get());
      if (v->Init) {
        line("init:");
        Scope s2(*this);
        expr(v->Init.get());
      }
      break;
    }
    case NodeKind::DeclStmtKind:
      decl(cast<DeclStmt>(s)->Inner.get());
      break;
    case NodeKind::DeferStmt:
      line("Defer" + loc(s));
      {
        Scope sc(*this);
        expr(cast<DeferStmtNode>(s)->Body.get());
      }
      break;
    default:
      line("<stmt>");
      break;
    }
  }

  //=== Expressions ======================================================//
  void expr(const Expr *e) {
    if (!e) {
      line("<null expr>");
      return;
    }
    switch (e->Kind) {
    case NodeKind::IntLit:
      line("Int " + std::to_string(cast<IntLitExpr>(e)->Value) +
           (cast<IntLitExpr>(e)->Suffix.empty()
                ? ""
                : (" " + cast<IntLitExpr>(e)->Suffix)));
      break;
    case NodeKind::FloatLit:
      line("Float " + std::to_string(cast<FloatLitExpr>(e)->Value));
      break;
    case NodeKind::StringLit:
      line("String \"" + cast<StringLitExpr>(e)->Value + "\"");
      break;
    case NodeKind::CharLit:
      line("Char U+" + std::to_string(cast<CharLitExpr>(e)->Value));
      break;
    case NodeKind::BoolLit:
      line(std::string("Bool ") + (cast<BoolLitExpr>(e)->Value ? "true" : "false"));
      break;
    case NodeKind::NilLit:
      line("Nil");
      break;
    case NodeKind::ArrayLit: {
      const auto *a = cast<ArrayLitExpr>(e);
      line(a->RepeatCount ? "ArrayLit [value; count]" : "ArrayLit");
      Scope sc(*this);
      for (const auto &el : a->Elements)
        expr(el.get());
      if (a->RepeatCount) {
        line("count:");
        Scope s2(*this);
        expr(a->RepeatCount.get());
      }
      break;
    }
    case NodeKind::TupleLit: {
      line("TupleLit");
      Scope sc(*this);
      for (const auto &el : cast<TupleLitExpr>(e)->Elements)
        expr(el.get());
      break;
    }
    case NodeKind::StructLit: {
      const auto *s = cast<StructLitExpr>(e);
      std::string path;
      for (size_t i = 0; i < s->Path.size(); ++i) {
        if (i) path += "::";
        path += s->Path[i];
      }
      line("StructLit " + path + loc(e));
      Scope sc(*this);
      for (const auto &f : s->Fields) {
        line("." + f.Name + ":");
        Scope s2(*this);
        if (f.Value) expr(f.Value.get());
        else line("<shorthand>");
      }
      if (s->Base) {
        line("..base:");
        Scope s2(*this);
        expr(s->Base.get());
      }
      break;
    }
    case NodeKind::DeclRef: {
      const auto *r = cast<DeclRefExpr>(e);
      line("Ref " + r->joined() +
           (r->GenericArgs.empty() ? "" : "::<...>") + loc(e));
      break;
    }
    case NodeKind::Unary: {
      const auto *u = cast<UnaryExpr>(e);
      line(std::string("Unary '") + unaryOpSpelling(u->Op) + "'");
      Scope sc(*this);
      expr(u->Operand.get());
      break;
    }
    case NodeKind::Binary: {
      const auto *b = cast<BinaryExpr>(e);
      line(std::string("Binary '") + binaryOpSpelling(b->Op) + "'" + loc(e));
      Scope sc(*this);
      expr(b->LHS.get());
      expr(b->RHS.get());
      break;
    }
    case NodeKind::Assign: {
      const auto *a = cast<AssignExpr>(e);
      line(std::string("Assign '") + assignOpSpelling(a->Op) + "'" + loc(e));
      Scope sc(*this);
      expr(a->LHS.get());
      expr(a->RHS.get());
      break;
    }
    case NodeKind::Call: {
      const auto *c = cast<CallExpr>(e);
      line("Call" + std::string(c->IsMethodCall ? " [method]" : "") + loc(e));
      Scope sc(*this);
      line("callee:");
      {
        Scope s2(*this);
        expr(c->Callee.get());
      }
      for (const auto &a : c->Args) {
        line(a.Label.empty() ? "arg:" : ("arg " + a.Label + ":"));
        Scope s2(*this);
        expr(a.Value.get());
      }
      break;
    }
    case NodeKind::Member: {
      const auto *m = cast<MemberExpr>(e);
      line("Member ." + m->Name + (m->IsTupleIndex ? " [tuple]" : "") + loc(e));
      Scope sc(*this);
      expr(m->Base.get());
      break;
    }
    case NodeKind::Index: {
      const auto *i = cast<IndexExpr>(e);
      line("Index" + loc(e));
      Scope sc(*this);
      expr(i->Base.get());
      expr(i->Index.get());
      break;
    }
    case NodeKind::Cast: {
      const auto *c = cast<CastExpr>(e);
      line("Cast to " + typeStr(c->TargetType.get()) + loc(e));
      Scope sc(*this);
      expr(c->Operand.get());
      break;
    }
    case NodeKind::TypeTest: {
      const auto *c = cast<TypeTestExpr>(e);
      line("TypeTest is " + typeStr(c->TargetType.get()) + loc(e));
      Scope sc(*this);
      expr(c->Operand.get());
      break;
    }
    case NodeKind::Closure: {
      const auto *c = cast<ClosureExpr>(e);
      line("Closure" + loc(e));
      Scope sc(*this);
      for (const Param &p : c->Params)
        line("param " + p.Name + ": " + typeStr(p.TypeAnnotation.get()));
      line("returns: " + typeStr(c->ReturnType.get()));
      expr(c->Body.get());
      break;
    }
    case NodeKind::Block: {
      const auto *b = cast<BlockExpr>(e);
      line("Block" + loc(e));
      Scope sc(*this);
      for (const auto &s : b->Stmts)
        stmt(s.get());
      if (b->Tail) {
        line("tail:");
        Scope s2(*this);
        expr(b->Tail.get());
      }
      break;
    }
    case NodeKind::If: {
      const auto *i = cast<IfExpr>(e);
      line("If" + loc(e));
      Scope sc(*this);
      line("cond:");
      {
        Scope s2(*this);
        expr(i->Cond.get());
      }
      if (i->BindingPat) {
        line("is:");
        Scope s2(*this);
        pattern(i->BindingPat.get());
      }
      line("then:");
      {
        Scope s2(*this);
        expr(i->Then.get());
      }
      if (i->Else) {
        line("else:");
        Scope s2(*this);
        expr(i->Else.get());
      }
      break;
    }
    case NodeKind::While: {
      const auto *w = cast<WhileExpr>(e);
      line("While" + (w->Label.empty() ? "" : " :" + w->Label) + loc(e));
      Scope sc(*this);
      expr(w->Cond.get());
      expr(w->Body.get());
      break;
    }
    case NodeKind::Loop: {
      const auto *l = cast<LoopExpr>(e);
      line("Loop" + (l->Label.empty() ? "" : " :" + l->Label) + loc(e));
      Scope sc(*this);
      expr(l->Body.get());
      break;
    }
    case NodeKind::For: {
      const auto *f = cast<ForExpr>(e);
      line("For" + (f->Label.empty() ? "" : " :" + f->Label) + loc(e));
      Scope sc(*this);
      pattern(f->Binding.get());
      line("in:");
      {
        Scope s2(*this);
        expr(f->Sequence.get());
      }
      expr(f->Body.get());
      break;
    }
    case NodeKind::Match: {
      const auto *m = cast<MatchExpr>(e);
      line("Match" + loc(e));
      Scope sc(*this);
      line("scrutinee:");
      {
        Scope s2(*this);
        expr(m->Scrutinee.get());
      }
      for (const auto &arm : m->Arms) {
        line("arm:");
        Scope s2(*this);
        pattern(arm.Pat.get());
        if (arm.Guard) {
          line("guard:");
          Scope s3(*this);
          expr(arm.Guard.get());
        }
        line("=>");
        Scope s3(*this);
        expr(arm.Body.get());
      }
      break;
    }
    case NodeKind::Return: {
      line("Return" + loc(e));
      if (cast<ReturnExpr>(e)->Value) {
        Scope sc(*this);
        expr(cast<ReturnExpr>(e)->Value.get());
      }
      break;
    }
    case NodeKind::Break: {
      const auto *b = cast<BreakExpr>(e);
      line("Break" + (b->Label.empty() ? "" : " :" + b->Label) + loc(e));
      if (b->Value) {
        Scope sc(*this);
        expr(b->Value.get());
      }
      break;
    }
    case NodeKind::Continue: {
      const auto *c = cast<ContinueExpr>(e);
      line("Continue" + (c->Label.empty() ? "" : " :" + c->Label) + loc(e));
      break;
    }
    case NodeKind::Borrow: {
      const auto *b = cast<BorrowExpr>(e);
      line(std::string("Borrow ") + (b->IsMutable ? "&var" : "&") + loc(e));
      Scope sc(*this);
      expr(b->Operand.get());
      break;
    }
    case NodeKind::Deref: {
      line("Deref" + loc(e));
      Scope sc(*this);
      expr(cast<DerefExpr>(e)->Operand.get());
      break;
    }
    case NodeKind::Range: {
      const auto *r = cast<RangeExpr>(e);
      line(std::string("Range") + (r->Inclusive ? " ..=" : " ..") + loc(e));
      Scope sc(*this);
      if (r->Lo) expr(r->Lo.get()); else line("<open>");
      if (r->Hi) expr(r->Hi.get()); else line("<open>");
      break;
    }
    case NodeKind::SelfRef:
      line("Self" + loc(e));
      break;
    case NodeKind::SuperRef:
      line("Super" + loc(e));
      break;
    case NodeKind::UnsafeBlock: {
      line("Unsafe" + loc(e));
      Scope sc(*this);
      expr(cast<UnsafeBlockExpr>(e)->Body.get());
      break;
    }
    case NodeKind::Try: {
      line("Try `?`" + loc(e));
      Scope sc(*this);
      expr(cast<TryExpr>(e)->Operand.get());
      break;
    }
    case NodeKind::Error:
      line("<error expr>" + loc(e));
      break;
    default:
      line("<expr kind " + std::to_string(static_cast<int>(e->Kind)) + ">");
      break;
    }
  }
};

} // namespace

void printAST(const Module &m, const SourceManager &sm, std::ostream &os) {
  Printer p(os, sm);
  p.module(m);
}

} // namespace rune
