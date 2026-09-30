//===- AST.h - Rune abstract syntax tree ------------------------*- C++ -*-===//
//
// One owning tree per source file. Nodes carry a SourceRange for diagnostics
// and empty slots that semantic analysis fills in (resolved types, resolved
// declarations, capture lists, ...), so the same tree feeds both the checker
// and the code generator.
//
//===----------------------------------------------------------------------===//
#ifndef RUNE_AST_H
#define RUNE_AST_H

#include "rune/Source.h"

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

namespace rune {

class Type;      // Sema's canonical type; declared in Type.h
struct Decl;
struct FunctionDecl;
struct VarDecl;
struct ValueDecl;
struct EnumDecl;
struct ClassDecl;
struct StructDecl;
struct MarkDecl;
struct BlockExpr;

/// Methods the compiler provides on builtin types, which have no Rune-level
/// declaration to point at.
enum class BuiltinMethod : uint8_t {
  None,
  StringLength, StringCharCount, StringIsEmpty, StringAt, StringByteAt,
  StringSubstring, StringFind, StringRepeat, StringToInt, StringToFloat,
  StringCStr, StringHash,
  SequenceLength, SequenceIsEmpty,
  /// `.str()` on any primitive: renders it as a String.
  ToString,
  /// `.$clone()`: a memberwise copy that owns its own everything — a fresh
  /// `String`, a new object with each field cloned in turn.
  Clone,
  /// Integer arithmetic that says what it does when the result does not fit,
  /// whatever the build's overflow setting: `n.$wrappingAdd(m)` wraps,
  /// `n.$saturatingAdd(m)` clamps, and `n.$checkedAdd(m)` is `nil`.
  IntWrappingAdd, IntWrappingSub, IntWrappingMul,
  IntSaturatingAdd, IntSaturatingSub, IntSaturatingMul,
  IntCheckedAdd, IntCheckedSub, IntCheckedMul,
};

enum class NodeKind : uint8_t {
  // --- Expressions ---
  IntLit, FloatLit, StringLit, CharLit, BoolLit, NilLit,
  ArrayLit, TupleLit, StructLit,
  DeclRef, Unary, Binary, Assign, Call, Member, Index,
  Cast, Into, TypeTest, Closure, Block, If, While, Loop, For, Match,
  Return, Break, Continue, Borrow, Deref, Range, SelfRef, SuperRef,
  Move,
  UnsafeBlock, Try, Error,

  // --- Statements ---
  ExprStmt, VarStmt, DeclStmtKind, DeferStmt,

  // --- Declarations ---
  Function, Struct, Enum, Class, Mark, Bind, Extend,
  GlobalVar, LocalVar, Import, Extern, TypeAlias, AssociatedType,
  Field, EnumVariant,

  // --- Type expressions ---
  NamedType, PointerType, ArrayType, SliceType, TupleType,
  FunctionTypeRepr, OptionalType, DynType, SelfType, InferType, UniqType,
  SomeType, TypeOfType,

  // --- Patterns ---
  WildcardPat, BindingPat, LiteralPat, TuplePat, StructPat, EnumPat,
  PathPat, RangePat, OrPat, RefPat, SlicePat,
};

//===----------------------------------------------------------------------===//
// Operators
//===----------------------------------------------------------------------===//

enum class BinaryOp : uint8_t {
  Add, Sub, Mul, Div, Rem,
  BitAnd, BitOr, BitXor, Shl, Shr,
  LogicalAnd, LogicalOr, Coalesce,
  Eq, Ne, Lt, Le, Gt, Ge,
};

enum class UnaryOp : uint8_t { Neg, Not, BitNot };

enum class AssignOp : uint8_t {
  Assign, Add, Sub, Mul, Div, Rem, BitAnd, BitOr, BitXor, Shl, Shr,
};

const char *binaryOpSpelling(BinaryOp op);
const char *unaryOpSpelling(UnaryOp op);
const char *assignOpSpelling(AssignOp op);
/// Mark method that overloads `op`, e.g. BinaryOp::Add -> "add".
const char *binaryOpMarkMethod(BinaryOp op);
const char *unaryOpMarkMethod(UnaryOp op);
/// Maps a compound assignment to the binary operator it applies.
BinaryOp assignOpToBinary(AssignOp op);
bool isComparison(BinaryOp op);

//===----------------------------------------------------------------------===//
// Node bases
//===----------------------------------------------------------------------===//

struct Node {
  NodeKind Kind;
  SourceRange Range;

  explicit Node(NodeKind k, SourceRange r = {}) : Kind(k), Range(r) {}
  virtual ~Node() = default;
};

/// Where a value lives; drives assignability and ARC decisions.
enum class ValueCategory : uint8_t {
  RValue,  ///< A temporary.
  LValue,  ///< Addressable and possibly assignable.
};

struct Expr : Node {
  Type *Ty = nullptr;                       ///< set by Sema
  ValueCategory Category = ValueCategory::RValue;
  /// True when Sema rewrote this node into a call to an operator overload.
  FunctionDecl *OverloadResolved = nullptr;

  using Node::Node;
};

struct Stmt : Node {
  using Node::Node;
};

/// A field path written in source: `stats.hits`, or the tail of `self.map`.
/// Sema resolves it to field indices, one per step.
struct FieldPathRepr {
  std::vector<std::string> Path;
  SourceRange Range;
  std::vector<unsigned> Resolved;           ///< set by Sema; one index per step
};

/// One place a `from` clause names: a parameter (with an optional field path
/// under it), `self.field` inside a type declaration, a local binding, or
/// `global`. Sema fills in what the root turned out to be.
struct OriginPlace {
  std::vector<std::string> Path;            ///< `["self", "map"]`, `["global"]`
  SourceRange Range;
  enum class RootKind : uint8_t { Unresolved, Param, SelfField, Local, Global };
  RootKind Root = RootKind::Unresolved;     ///< set by Sema
  int ParamIndex = -1;                      ///< `Param`: index into the signature
  VarDecl *Local = nullptr;                 ///< `Local`: the binding named
  std::vector<unsigned> FieldPath;          ///< field indices after the root
};

/// `from place`, `from (a, b.c)`, `from global` — where a reference type is
/// borrowed from, written after the type. See Zombie.h for what it means.
struct OriginClause {
  std::vector<OriginPlace> Places;
  SourceRange Range;
};

struct TypeRepr : Node {
  Type *Resolved = nullptr;                 ///< set by Sema
  /// The `from` clause, when one was written on this type.
  std::unique_ptr<OriginClause> Origin;
  using Node::Node;
};

struct Pattern : Node {
  Type *Ty = nullptr;                       ///< set by Sema
  using Node::Node;
};

using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;
using DeclPtr = std::unique_ptr<Decl>;
using TypeReprPtr = std::unique_ptr<TypeRepr>;
using PatternPtr = std::unique_ptr<Pattern>;

//===----------------------------------------------------------------------===//
// Shared pieces
//===----------------------------------------------------------------------===//

/// A `@name(args)` decorator.
struct Attribute {
  std::string Name;
  std::vector<ExprPtr> Args;
  SourceRange Range;
  /// Resolved user-defined decorator function, if this is not a builtin.
  FunctionDecl *Resolved = nullptr;
};

/// One `<T: Mark + Other>` entry.
struct GenericParam {
  std::string Name;
  std::vector<TypeReprPtr> Bounds;
  SourceRange Range;
  /// Other names that stand for this parameter. An `extend` block on a
  /// generic type writes its own — `extend<A> List<A>` calls `T` `A` — and
  /// its methods are folded into the type, so the type's parameter answers to
  /// both names wherever those methods are resolved.
  std::vector<std::string> Aliases;
};

/// `where T: Mark` — additional bounds written after the signature.
struct WhereClause {
  TypeReprPtr Subject;
  std::vector<TypeReprPtr> Bounds;
  SourceRange Range;
};

/// A function parameter. `self` parameters set IsSelf and record how self is
/// passed (`self`, `&self`, `&var self`).
struct Param {
  std::string Name;
  TypeReprPtr TypeAnnotation;
  ExprPtr DefaultValue;
  SourceRange Range;
  bool IsSelf = false;
  bool SelfByRef = false;
  bool SelfMutable = false;
  bool IsMutable = false;   ///< `var x: T` parameter
  bool IsVariadic = false;  ///< trailing `...` in an extern declaration
  /// `&var self { counter, log }` / `list: &var List { items }`: the fields
  /// this function touches through the parameter, as a promise to callers.
  /// Empty means no view was written; `HasView` tells that apart from `{ }`.
  std::vector<FieldPathRepr> View;
  bool HasView = false;
  Type *Ty = nullptr;       ///< set by Sema
  VarDecl *Binding = nullptr;
};

/// A call argument, optionally labelled: `f(field: value, y)`.
struct Argument {
  std::string Label;        ///< empty when positional
  ExprPtr Value;
  SourceRange LabelRange;
  /// Sema wrapped the argument in a borrow because the parameter is `&T`
  /// and the argument was a `T`.
  bool AutoBorrow = false;
};

//===----------------------------------------------------------------------===//
// Type expressions
//===----------------------------------------------------------------------===//

/// `std::collections::Map<K, V>` or a bare `i64`.
struct NamedTypeRepr : TypeRepr {
  std::vector<std::string> Path;
  std::vector<TypeReprPtr> GenericArgs;
  SourceRange NameRange;
  NamedTypeRepr() : TypeRepr(NodeKind::NamedType) {}
};

/// `&T`, `&var T` (borrowed) or `*T`, `*var T` (raw, unsafe).
struct PointerTypeRepr : TypeRepr {
  TypeReprPtr Pointee;
  bool IsMutable = false;
  bool IsRaw = false;
  bool IsWeak = false;
  PointerTypeRepr() : TypeRepr(NodeKind::PointerType) {}
};

/// `[5:i64]` — a fixed-size array.
struct ArrayTypeRepr : TypeRepr {
  TypeReprPtr Element;
  ExprPtr Size;
  ArrayTypeRepr() : TypeRepr(NodeKind::ArrayType) {}
};

/// `[i64]` — a slice (pointer + length).
struct SliceTypeRepr : TypeRepr {
  TypeReprPtr Element;
  SliceTypeRepr() : TypeRepr(NodeKind::SliceType) {}
};

struct TupleTypeRepr : TypeRepr {
  std::vector<TypeReprPtr> Elements;
  TupleTypeRepr() : TypeRepr(NodeKind::TupleType) {}
};

/// `@function(arg...) -> ret`; without the arrow, the function returns `()`.
struct FunctionTypeReprNode : TypeRepr {
  std::vector<TypeReprPtr> Params;
  /// Parallel to `Params`: the name written before each, or empty. Names
  /// exist only so a `from` clause on the result can point at one.
  std::vector<std::string> ParamNames;
  TypeReprPtr ReturnType;
  /// `@cfunction(...)`: a bare pointer rather than a closure.
  bool IsCFunction = false;
  FunctionTypeReprNode() : TypeRepr(NodeKind::FunctionTypeRepr) {}
};

/// `T?` — sugar for `Option<T>`.
struct OptionalTypeRepr : TypeRepr {
  TypeReprPtr Element;
  OptionalTypeRepr() : TypeRepr(NodeKind::OptionalType) {}
};

/// `uniq T` — a move-only owning reference to a class instance.
///
/// A `uniq` value is represented exactly like an ordinary class reference, but
/// it can never be copied: it is either moved with `move`, or borrowed. Its
/// count therefore never rises above one, so a ring of `uniq` edges cannot be
/// built — which is what lets a list or a tree be owned without a cycle check.
struct UniqTypeRepr : TypeRepr {
  TypeReprPtr Element;
  UniqTypeRepr() : TypeRepr(NodeKind::UniqType) {}
};

/// `dyn Mark` — a mark object with a vtable.
struct DynTypeRepr : TypeRepr {
  TypeReprPtr MarkType;
  DynTypeRepr() : TypeRepr(NodeKind::DynType) {}
};

/// `some Mark` — a function's result whose concrete type is fixed by its
/// body and hidden from its callers, who see only the mark. Allowed as a
/// function's result type and nowhere else.
struct SomeTypeRepr : TypeRepr {
  TypeReprPtr MarkType;
  SomeTypeRepr() : TypeRepr(NodeKind::SomeType) {}
};

/// `typeof(expr)` — whatever type the expression has, without evaluating it.
///
/// A macro writing a signature out of the values it was handed needs this:
/// `@cfunction(objc_object, objc_object $(, typeof($item))*)` builds the
/// foreign signature from the arguments rather than asking the caller to
/// spell it a second time.
struct TypeOfRepr : TypeRepr {
  ExprPtr Operand;
  TypeOfRepr() : TypeRepr(NodeKind::TypeOfType) {}
};

struct SelfTypeRepr : TypeRepr {
  SelfTypeRepr() : TypeRepr(NodeKind::SelfType) {}
};

/// A type that was omitted and must be inferred.
struct InferTypeRepr : TypeRepr {
  InferTypeRepr() : TypeRepr(NodeKind::InferType) {}
};

//===----------------------------------------------------------------------===//
// Patterns
//===----------------------------------------------------------------------===//

struct WildcardPattern : Pattern {
  WildcardPattern() : Pattern(NodeKind::WildcardPat) {}
};

struct BindingPattern : Pattern {
  std::string Name;
  bool IsMutable = false;
  bool ByRef = false;
  /// Written `.Red`: the name is something on the type being matched, never
  /// a new binding. Sema says so plainly when there is no such variant.
  bool MustBeVariant = false;
  PatternPtr Sub;           ///< `name @ subpattern`
  VarDecl *Binding = nullptr;
  /// A bare name in a match arm may name a unit enum variant rather than
  /// introduce a binding. Sema sets these when that is what it turned out to
  /// be, and CodeGen then emits a tag test instead of a store.
  EnumDecl *VariantOwner = nullptr;
  int VariantIndex = -1;
  bool isVariantTest() const { return VariantIndex >= 0; }
  BindingPattern() : Pattern(NodeKind::BindingPat) {}
};

struct LiteralPattern : Pattern {
  ExprPtr Value;
  LiteralPattern() : Pattern(NodeKind::LiteralPat) {}
};

struct TuplePattern : Pattern {
  std::vector<PatternPtr> Elements;
  TuplePattern() : Pattern(NodeKind::TuplePat) {}
};

struct StructPatternField {
  std::string Name;
  PatternPtr Value;         ///< null means shorthand `Point { x, y }`
  SourceRange Range;
  unsigned FieldIndex = 0;  ///< set by Sema
};

struct StructPattern : Pattern {
  std::vector<std::string> Path;
  std::vector<StructPatternField> Fields;
  bool HasRest = false;     ///< trailing `..`
  Decl *ResolvedDecl = nullptr;
  int VariantIndex = -1;    ///< >= 0 when the path names an enum variant
  StructPattern() : Pattern(NodeKind::StructPat) {}
};

/// `Some(x)`, `Shape::Circle(r)` — a tuple-style enum or struct pattern.
struct EnumPattern : Pattern {
  std::vector<std::string> Path;
  std::vector<PatternPtr> Elements;
  Decl *ResolvedDecl = nullptr;
  int VariantIndex = -1;
  EnumPattern() : Pattern(NodeKind::EnumPat) {}
};

/// A bare path: a unit enum variant such as `None`, or a constant.
struct PathPattern : Pattern {
  std::vector<std::string> Path;
  Decl *ResolvedDecl = nullptr;
  int VariantIndex = -1;
  PathPattern() : Pattern(NodeKind::PathPat) {}
};

struct RangePattern : Pattern {
  ExprPtr Lo, Hi;
  bool Inclusive = false;
  RangePattern() : Pattern(NodeKind::RangePat) {}
};

struct OrPattern : Pattern {
  std::vector<PatternPtr> Alternatives;
  OrPattern() : Pattern(NodeKind::OrPat) {}
};

struct RefPattern : Pattern {
  PatternPtr Sub;
  bool IsMutable = false;
  RefPattern() : Pattern(NodeKind::RefPat) {}
};

/// `[a, b, c]`, `[first, ..rest]`, `[first, .., last]` — a run of elements
/// out of an array or a slice. The elements before `..` are matched from the
/// front and the ones after it from the back; `..` itself stands for however
/// many are in between, and `..name` binds them as a slice.
struct SlicePattern : Pattern {
  std::vector<PatternPtr> Prefix;
  std::vector<PatternPtr> Suffix;
  bool HasRest = false;
  /// The binding for `..rest`; null for a bare `..`.
  PatternPtr Rest;
  /// Set by Sema: the element type, and whether the value is a fixed array
  /// (whose length is known) or a slice (whose length is tested).
  Type *ElementTy = nullptr;
  SlicePattern() : Pattern(NodeKind::SlicePat) {}
};

//===----------------------------------------------------------------------===//
// Expressions
//===----------------------------------------------------------------------===//

struct IntLitExpr : Expr {
  uint64_t Value = 0;
  std::string Suffix;
  bool IsNegated = false;   ///< folded from a unary minus by Sema
  IntLitExpr() : Expr(NodeKind::IntLit) {}
};

struct FloatLitExpr : Expr {
  double Value = 0;
  std::string Suffix;
  FloatLitExpr() : Expr(NodeKind::FloatLit) {}
};

struct StringLitExpr : Expr {
  std::string Value;
  /// True when the literal appears where a CString is wanted (FFI).
  bool AsCString = false;
  StringLitExpr() : Expr(NodeKind::StringLit) {}
};

struct CharLitExpr : Expr {
  uint32_t Value = 0;
  CharLitExpr() : Expr(NodeKind::CharLit) {}
};

struct BoolLitExpr : Expr {
  bool Value = false;
  BoolLitExpr() : Expr(NodeKind::BoolLit) {}
};

struct NilLitExpr : Expr {
  NilLitExpr() : Expr(NodeKind::NilLit) {}
};

/// `[a, b, c]` or `[value; count]`.
struct ArrayLitExpr : Expr {
  std::vector<ExprPtr> Elements;
  ExprPtr RepeatCount;      ///< non-null for the `[v; n]` form
  ArrayLitExpr() : Expr(NodeKind::ArrayLit) {}
};

struct TupleLitExpr : Expr {
  std::vector<ExprPtr> Elements;
  TupleLitExpr() : Expr(NodeKind::TupleLit) {}
};

struct StructLitField {
  std::string Name;
  ExprPtr Value;            ///< null for the `Point { x }` shorthand
  SourceRange Range;
  unsigned FieldIndex = 0;  ///< set by Sema
};

/// `Point { x: 1, y: 2 }`, also used for tuple-less enum variants with fields.
struct StructLitExpr : Expr {
  std::vector<std::string> Path;
  std::vector<TypeReprPtr> GenericArgs;
  std::vector<StructLitField> Fields;
  ExprPtr Base;             ///< `..other`
  Decl *ResolvedDecl = nullptr;
  int VariantIndex = -1;
  SourceRange PathRange;
  StructLitExpr() : Expr(NodeKind::StructLit) {}
};

/// An identifier or qualified path, optionally with explicit generic args.
struct DeclRefExpr : Expr {
  std::vector<std::string> Path;
  std::vector<TypeReprPtr> GenericArgs;
  Decl *Resolved = nullptr;         ///< set by Sema
  /// Set when the path names an enum variant, e.g. `Colour::Red`.
  int VariantIndex = -1;
  /// Written `.Red` or `::seconds(5)`: the owning type was left out, and is
  /// whatever the context expects. Sema puts the name back on the front.
  bool FromInferredType = false;
  /// Set when this mention of a `Unique` local hands the value away — an
  /// assignment into another `Unique` slot, or a return. Codegen empties the
  /// slot so the scope does not release what it no longer owns.
  bool MovedOut = false;
  DeclRefExpr() : Expr(NodeKind::DeclRef) {}

  std::string joined(const char *sep = "::") const {
    std::string s;
    for (size_t i = 0; i < Path.size(); ++i) {
      if (i) s += sep;
      s += Path[i];
    }
    return s;
  }
};

struct UnaryExpr : Expr {
  UnaryOp Op;
  ExprPtr Operand;
  SourceRange OpRange;
  UnaryExpr() : Expr(NodeKind::Unary), Op(UnaryOp::Neg) {}
};

struct BinaryExpr : Expr {
  BinaryOp Op;
  ExprPtr LHS, RHS;
  SourceRange OpRange;
  BinaryExpr() : Expr(NodeKind::Binary), Op(BinaryOp::Add) {}
};

struct AssignExpr : Expr {
  AssignOp Op;
  ExprPtr LHS, RHS;
  SourceRange OpRange;
  /// `x = 7` introduces `x` when no binding named `x` is in scope; Sema sets
  /// this so CodeGen creates storage instead of storing into an existing slot.
  bool DeclaresBinding = false;
  VarDecl *DeclaredVar = nullptr;
  /// Set when a `bind operator::add`-style overload handles a compound
  /// assignment such as `+=`.
  FunctionDecl *OperatorImpl = nullptr;
  /// Set for `*value = x` on a type that overloads `deref`: the write goes
  /// through this rather than into storage.
  FunctionDecl *DerefSetImpl = nullptr;
  /// Set for `value[i] = x` on a type that overloads `index`, for the same
  /// reason: there is no storage here the compiler can see.
  FunctionDecl *IndexSetImpl = nullptr;
  AssignExpr() : Expr(NodeKind::Assign), Op(AssignOp::Assign) {}
};

struct CallExpr : Expr {
  ExprPtr Callee;
  std::vector<Argument> Args;
  SourceRange ParenRange;
  /// Set by Sema when the callee resolves statically; null for indirect calls.
  FunctionDecl *Target = nullptr;
  /// Argument order after labelled arguments are matched to parameters.
  std::vector<unsigned> ArgOrder;
  /// For each argument given by position, the name of the parameter it
  /// landed in — the label it could have been written with; empty for a
  /// labelled argument or one with no named parameter. Editors show these.
  std::vector<std::string> ParamLabels;
  /// True when this is a method call whose receiver is Callee's base.
  bool IsMethodCall = false;
  /// The `look()`/`touch()` inserted to reach through a stand-in — a
  /// `Handle`, a `Box`, an `Rc`. Only diagnostics use it: a write refused
  /// because the stand-in lends for reading only should say so.
  bool PointeeAccess = false;
  /// The `Path::empty()` a builder block stands for. Only diagnostics use it:
  /// a type that is not a `Builder` has no `empty`, and the message should
  /// say that rather than talk about a method nobody wrote.
  bool BuilderSeed = false;
  /// Set when the callee is a compiler builtin rather than a declaration.
  BuiltinMethod Builtin = BuiltinMethod::None;
  /// Set when the call constructs a class instance (`Dog("rex")`).
  ClassDecl *ConstructsClass = nullptr;
  /// Set when the call constructs a tuple-shaped enum variant.
  EnumDecl *ConstructsEnum = nullptr;
  int ConstructsVariant = -1;
  /// `reflect::conforms<T, M>()`: the answer, worked out while checking —
  /// where conformance is known, including the automatic marks the code
  /// generator has no way to ask about. -1 until then.
  int8_t ReflectAnswer = -1;
  CallExpr() : Expr(NodeKind::Call) {}
};

/// `base.name` or `base.0`.
struct MemberExpr : Expr {
  ExprPtr Base;
  std::string Name;
  std::vector<TypeReprPtr> GenericArgs;
  SourceRange NameRange;
  bool IsTupleIndex = false;
  uint32_t TupleIndex = 0;
  /// Sema fills exactly one of these.
  int FieldIndex = -1;
  Decl *ResolvedMethod = nullptr;
  BuiltinMethod Builtin = BuiltinMethod::None;
  /// Number of implicit dereferences inserted before the access.
  unsigned AutoDerefs = 0;
  /// Set for `super.method()`, which bypasses the vtable.
  bool NonVirtual = false;
  /// Set for `value.$name()`: the member is one the compiler provides, not one
  /// the type declares.
  bool IsIntrinsic = false;
  /// Set for `value::Mark.name()`, which looks the method up in that mark's
  /// binding rather than in the type's ordinary method table.
  MarkDecl *QualifiedMark = nullptr;
  /// Set when the base is a borrow that must be taken implicitly so the
  /// method can receive `&self`.
  bool NeedsAddressOfBase = false;
  /// Written `value.await`. The parser rewrites it into a call of the
  /// `await` method `std::task::Future` declares; this says so, for the
  /// error a value that is not a future gets.
  bool IsAwait = false;
  MemberExpr() : Expr(NodeKind::Member) {}
};

struct IndexExpr : Expr {
  ExprPtr Base, Index;
  SourceRange BracketRange;
  IndexExpr() : Expr(NodeKind::Index) {}
  /// `p[n]` where `p` is a raw pointer: plain offset arithmetic, no bounds.
  bool ThroughRawPointer = false;
  /// `text[i]` on a `String`: the i-th character, read out of the UTF-8 —
  /// a value, never a place, and the same read as `text.$at(i)`.
  bool StringChar = false;
  /// The overload lends the element (`index -> &T from self`) and the element
  /// is plain data, so `v[i]` reads it out: `let n = v[i]` is a number, as it
  /// would be for an array. An element that owns something stays lent.
  bool ReadsThrough = false;
};

struct CastExpr : Expr {
  ExprPtr Operand;
  TypeReprPtr TargetType;
  CastExpr() : Expr(NodeKind::Cast) {}
  /// `someFunction as *var u8` — the address itself, not a closure value.
  bool IsFunctionAddress = false;
};

/// `value into Target` — a conversion the program supplied with
/// `bind Source into Target` (the same as `bind As<Target> to Source`),
/// as opposed to `as`, which is the compiler's.
struct IntoExpr : Expr {
  ExprPtr Operand;
  TypeReprPtr TargetType;
  /// The `convert` the chosen binding supplies.
  FunctionDecl *Conversion = nullptr;
  IntoExpr() : Expr(NodeKind::Into) {}
};

/// `value is SomeType` — a runtime class check.
struct TypeTestExpr : Expr {
  ExprPtr Operand;
  TypeReprPtr TargetType;
  TypeTestExpr() : Expr(NodeKind::TypeTest) {}
};

/// One captured variable inside a closure.
struct Capture {
  VarDecl *Var = nullptr;
  std::string Name;
  bool ByRef = false;
  Type *Ty = nullptr;
  unsigned Index = 0;
};

struct BlockExpr;

/// `||(a: i64, b: i64) -> i64 { ... }`
struct ClosureExpr : Expr {
  std::vector<Param> Params;
  TypeReprPtr ReturnType;
  std::unique_ptr<BlockExpr> Body;
  std::vector<Capture> Captures;   ///< computed by Sema
  FunctionDecl *Lifted = nullptr;  ///< synthesised top-level function
  /// Written `move ||(...)`. Captures are copied either way — this says so.
  bool IsMove = false;
  /// The body of an `async fn`, an `async ||` closure or an `async { }`
  /// block, as the parser rewrote it: a closure handed to `std::task::spawn`
  /// to run as a task. `.await` is allowed directly inside one of these and
  /// nowhere else; the parser enforces that, and Sema words its messages
  /// for the function the programmer wrote rather than the closure it
  /// became.
  bool IsAsyncBody = false;
  ClosureExpr() : Expr(NodeKind::Closure) {}
};

struct BlockExpr : Expr {
  std::vector<StmtPtr> Stmts;
  /// The trailing expression whose value the block produces, if any.
  ExprPtr Tail;
  BlockExpr() : Expr(NodeKind::Block) {}
};

struct IfExpr : Expr {
  ExprPtr Cond;
  /// `if let`-style binding: `if x is Some(v)` desugars into this slot.
  PatternPtr BindingPat;
  std::unique_ptr<BlockExpr> Then;
  ExprPtr Else;             ///< a BlockExpr or a nested IfExpr
  IfExpr() : Expr(NodeKind::If) {}
};

struct WhileExpr : Expr {
  ExprPtr Cond;
  /// `while value is Some(v)`: the loop runs while the pattern matches, with
  /// its bindings live for the body.
  PatternPtr BindingPat;
  std::unique_ptr<BlockExpr> Body;
  std::string Label;
  WhileExpr() : Expr(NodeKind::While) {}
};

struct LoopExpr : Expr {
  std::unique_ptr<BlockExpr> Body;
  std::string Label;
  LoopExpr() : Expr(NodeKind::Loop) {}
};

struct ForExpr : Expr {
  PatternPtr Binding;
  ExprPtr Sequence;
  std::unique_ptr<BlockExpr> Body;
  std::string Label;
  /// Set when the loop is driven by `Iterator::next` rather than by an index.
  /// Sema resolves the implementation; the loop calls it once per turn.
  FunctionDecl *NextMethod = nullptr;
  /// Set when `Sequence` is a `Sequence` rather than an iterator itself: the
  /// loop calls this once, up front, to get the iterator it then drives.
  FunctionDecl *IterateMethod = nullptr;
  /// The iterator's own type — the sequence's, or what `iterate` returns.
  Type *IterType = nullptr;
  /// `Option<Item>`, which is what `next` hands back.
  Type *NextResult = nullptr;
  ForExpr() : Expr(NodeKind::For) {}
};

struct MatchArm {
  PatternPtr Pat;
  ExprPtr Guard;            ///< `if cond` after the pattern
  ExprPtr Body;
  SourceRange Range;
};

struct MatchExpr : Expr {
  ExprPtr Scrutinee;
  std::vector<MatchArm> Arms;
  MatchExpr() : Expr(NodeKind::Match) {}
};

struct ReturnExpr : Expr {
  ExprPtr Value;
  ReturnExpr() : Expr(NodeKind::Return) {}
};

struct BreakExpr : Expr {
  ExprPtr Value;
  std::string Label;
  BreakExpr() : Expr(NodeKind::Break) {}
};

struct ContinueExpr : Expr {
  std::string Label;
  ContinueExpr() : Expr(NodeKind::Continue) {}
};

/// `&expr` / `&var expr`.
struct BorrowExpr : Expr {
  ExprPtr Operand;
  bool IsMutable = false;
  BorrowExpr() : Expr(NodeKind::Borrow) {}
};

struct DerefExpr : Expr {
  ExprPtr Operand;
  /// Set when `*value` goes through `bind operator::deref` rather than
  /// through a real pointer.
  FunctionDecl *OverloadResolved = nullptr;
  DerefExpr() : Expr(NodeKind::Deref) {}
};

/// `move value` — hands ownership of a `uniq` reference on, leaving the
/// source unusable. This is the only way to transfer one.
struct MoveExpr : Expr {
  ExprPtr Operand;
  /// The local the value was moved out of, when it was one. Codegen clears
  /// its drop flag so the scope does not release it again.
  VarDecl *MovedFrom = nullptr;
  /// True when the operand named a field, which is emptied by the move.
  bool FromField = false;
  MoveExpr() : Expr(NodeKind::Move) {}
};

struct RangeExpr : Expr {
  ExprPtr Lo, Hi;           ///< either may be null for open ranges
  bool Inclusive = false;
  RangeExpr() : Expr(NodeKind::Range) {}
};

struct SelfExpr : Expr {
  VarDecl *Binding = nullptr;
  SelfExpr() : Expr(NodeKind::SelfRef) {}
};

struct SuperExpr : Expr {
  SuperExpr() : Expr(NodeKind::SuperRef) {}
};

/// `unsafe { ... }` — suppresses unsafe-operation warnings inside.
struct UnsafeBlockExpr : Expr {
  std::unique_ptr<BlockExpr> Body;
  UnsafeBlockExpr() : Expr(NodeKind::UnsafeBlock) {}
};

/// `expr?` — early-returns the empty case of an Option or Result.
struct TryExpr : Expr {
  ExprPtr Operand;
  /// Set by Sema when the operand's error type differs from the function's
  /// and `bind Theirs into Ours` (the same as `bind As<Ours> to Theirs`)
  /// says how to get from one to the other: the `convert` the error goes
  /// through on the way out.
  FunctionDecl *ErrorConversion = nullptr;
  /// Set instead when the two error types convert implicitly — a subclass
  /// into its base, a narrow integer into a wide one.
  bool ErrorCoerces = false;
  TryExpr() : Expr(NodeKind::Try) {}
};

/// Placeholder produced during error recovery so the tree stays well-formed.
struct ErrorExpr : Expr {
  ErrorExpr() : Expr(NodeKind::Error) {}
};

//===----------------------------------------------------------------------===//
// Statements
//===----------------------------------------------------------------------===//

struct ExprStmt : Stmt {
  ExprPtr Value;
  ExprStmt() : Stmt(NodeKind::ExprStmt) {}
};

/// `x = 7`, `x: i64 = 7`, `var x: i64`, `global var x: i64 = 0`.
struct VarStmtNode : Stmt {
  PatternPtr Binding;       ///< usually a BindingPattern; may destructure
  TypeReprPtr TypeAnnotation;
  ExprPtr Init;
  bool IsMutable = false;
  bool IsGlobal = false;
  bool DeclaresNew = true;  ///< false once Sema proves `x = v` is an assignment
  VarStmtNode() : Stmt(NodeKind::VarStmt) {}
};

struct DeclStmt : Stmt {
  DeclPtr Inner;
  DeclStmt() : Stmt(NodeKind::DeclStmtKind) {}
};

struct DeferStmtNode : Stmt {
  ExprPtr Body;
  DeferStmtNode() : Stmt(NodeKind::DeferStmt) {}
};

//===----------------------------------------------------------------------===//
// Declarations
//===----------------------------------------------------------------------===//

struct Decl : Node {
  std::string Name;
  SourceRange NameRange;
  bool IsPublic = false;
  std::vector<Attribute> Attrs;
  /// Module that owns this declaration, e.g. "std::io".
  std::string ModulePath;
  /// The enclosing type for methods, null for free declarations.
  Decl *Parent = nullptr;
  /// Prose attached to this declaration: the text of `@Doc("...")`, or the
  /// `///` comment above it when there is no `@Doc`. Recorded at compile time
  /// rather than run, so documentation can be generated for a library that is
  /// never executed.
  std::string Doc;

  using Node::Node;

  const Attribute *findAttr(const std::string &name) const {
    for (const auto &a : Attrs)
      if (a.Name == name)
        return &a;
    return nullptr;
  }
  bool hasAttr(const std::string &name) const { return findAttr(name) != nullptr; }
};

/// Anything that can be referenced as a value (functions, variables, params).
struct ValueDecl : Decl {
  Type *Ty = nullptr;
  /// The symbol the linker sees, when it differs from `Name`. Set by `@as`,
  /// which renames a foreign declaration for Rune's side only: the C library
  /// still exports `bind`, and this file calls it `cbind`.
  std::string LinkName;
  using Decl::Decl;
};

struct VarDecl : ValueDecl {
  TypeReprPtr TypeAnnotation;
  ExprPtr Init;
  bool IsMutable = false;
  bool IsGlobal = false;
  bool IsParam = false;
  bool IsCaptured = false;   ///< referenced by a nested closure
  /// True when some mention of this local hands its value on: a `move`, or a
  /// store of an owning value into somewhere that outlives the scope. The
  /// slot then carries a flag saying whether it still owns what it holds, so
  /// a `deinit` on an early return does not also run at the end of the block.
  bool MovedSomewhere = false;
  /// True when the ownership pass proved this local never leaves the scope
  /// that declared it: nothing returns it, stores it anywhere that outlives
  /// the scope, captures it, or hands it to a call by value. A reference-
  /// counted value in such a slot needs no counting — the scope's own
  /// lifetime already says exactly when it goes. Only set at `--safety full`,
  /// where the pass runs.
  bool NoEscape = false;
  /// Set by the Zombie borrow checker: this binding is an alias of borrowed
  /// content (a pattern binding out of `&self`, an element of a borrowed
  /// array). It holds a copy of the value but owns nothing — never dropped,
  /// never emptied when read.
  bool ZombieAlias = false;
  /// Set by the Zombie borrow checker on a local whose value is handed on
  /// somewhere, so codegen knows the slot may be empty at scope end.
  bool ZombieMoved = false;
  /// Slot assigned by CodeGen.
  void *Storage = nullptr;
  VarDecl() : ValueDecl(NodeKind::LocalVar) {}
};

enum class FunctionFlavour : uint8_t {
  Free,        ///< a plain `fn`
  Method,      ///< has a `self` parameter
  Initialiser, ///< `fn init(self, ...)`
  Deinitialiser,///< `fn deinit(self)`
  MarkRequirement,
  Closure,
};

struct FunctionDecl : ValueDecl {
  std::vector<GenericParam> Generics;
  std::vector<WhereClause> WhereClauses;
  std::vector<Param> Params;
  TypeReprPtr ReturnType;
  std::unique_ptr<BlockExpr> Body;   ///< null for extern and mark requirements

  FunctionFlavour Flavour = FunctionFlavour::Free;
  bool IsUnsafe = false;             ///< carries @unsafe
  bool IsSafeJustified = false;      ///< carries @safe("...")
  std::string SafetyReason;
  /// `@zombie("reason")`: the borrow checker does not read this body. Its
  /// signature — `from` clauses and views — is still the contract callers
  /// are checked against.
  bool IsZombieTrusted = false;
  std::string ZombieReason;
  /// Set once the `from` clauses and views in the signature have been
  /// resolved; several paths assemble a signature and each may ask.
  bool OriginsResolved = false;
  bool IsExtern = false;
  std::string ExternABI;
  /// For an `extern "C++"` free function: the namespaces it was declared in,
  /// outermost first. A method's scope is its owning type's.
  std::vector<std::string> CxxScope;
  /// `@operator("new")` on an `extern "C++"` member: the C++ operator this
  /// declares, spelled as C++ spells it after the keyword.
  std::string CxxOperator;
  bool IsVariadic = false;
  /// Declared by an imported `.rul`. The body is present (generics need it)
  /// but the compiled code already lives in that library's object file, so
  /// CodeGen only emits a declaration. Instantiated clones clear this.
  bool IsImported = false;
  bool IsVirtual = false;            ///< a class method reachable via vtable
  /// A method of an instantiated generic type whose own `where` this
  /// instantiation does not meet: never checked, never emitted, and refused
  /// wherever it is called. Its vtable slot is empty.
  bool WhereUnmet = false;
  bool IsOverride = false;
  bool IsStatic = false;             ///< no self parameter but namespaced
  /// The type this method belongs to, for methods reached through `extend` or
  /// `bind`. Set even when that type is builtin, where `Parent` is null.
  Type *OwnerType = nullptr;
  /// The mark this method came from, when a `bind` supplied it.
  MarkDecl *FromMark = nullptr;
  /// The `bind` block that supplied it. Two blocks may supply one name with
  /// different parameters, and this is what tells their overloads apart.
  const struct BindDecl *Bind = nullptr;
  /// Set when this came from a `bind operator::…` block. Such a method may be
  /// one of several for the same operator on the same type, told apart by the
  /// type of its right-hand operand — which is why its symbol carries it.
  bool IsOperatorImpl = false;
  /// User-defined decorators applied to this function, resolved by Sema. Each
  /// is called once before `main`, with this function as its last argument.
  std::vector<std::pair<struct FunctionDecl *, struct Attribute *>> Decorators;
  /// The symbol name used for linking; set by Sema/Mangle.
  std::string MangledName;
  /// Index in the owning class's vtable, or -1.
  int VTableIndex = -1;
  /// For generic functions: instantiations keyed by mangled name.
  std::vector<FunctionDecl *> Instantiations;
  FunctionDecl *GenericTemplate = nullptr;
  std::vector<Type *> TypeArguments;
  /// Set for closures lifted to top level.
  ClosureExpr *SourceClosure = nullptr;
  std::vector<Capture> Captures;
  void *CodeGenFn = nullptr;
  /// Declared `async fn`. The parser has already rewritten it: the written
  /// result type `T` became `std::task::Future<T>`, and the body became a
  /// closure handed to `std::task::spawn`. What is left of the original is
  /// this flag and `AsyncResult`, for diagnostics and documentation, which
  /// describe the function as it was written.
  bool IsAsync = false;
  TypeReprPtr AsyncResult;

  FunctionDecl() : ValueDecl(NodeKind::Function) {}
};

struct FieldDecl : ValueDecl {
  TypeReprPtr TypeAnnotation;
  ExprPtr DefaultValue;
  unsigned Index = 0;
  bool IsMutable = true;
  bool IsWeak = false;
  /// `body: &String from self.text` — the field borrows from a sibling that
  /// owns a heap object. Set by Sema (SemaOrigins.cpp); read by the borrow
  /// checker, and by codegen, which stores such a field as the handle it
  /// points at rather than as a pointer to a slot.
  bool InternalRef = false;
  /// For a weak field, the class it refers to. `Ty` is then `Option<that>`,
  /// because reading a weak reference can always come back empty.
  Type *WeakTarget = nullptr;
  FieldDecl() : ValueDecl(NodeKind::Field) {}
};

/// Types the compiler itself needs to know about by name.
enum class LangItem : uint8_t { NotSpecial, Option, Result };

/// What a type declared inside `extern "C++"` knows about its other half.
///
/// A `struct` there is a C++ struct Rune lays out identically and may hold by
/// value; a `class` is opaque — its size is `@size(N)` or unknown — and only
/// ever reached through a pointer. `Scope` is the C++ path the name lives
/// under (namespaces, then enclosing classes), which is what its mangled
/// symbols are built from; Rune's own scope stays flat.
struct CxxDeclInfo {
  std::vector<std::string> Scope;
  bool IsClass = false;
  uint64_t Size = 0;           ///< `@size(N)`, or 0 when not given
  unsigned Align = 0;          ///< `@align(N)`, or 0 for the default
  /// `class Derived : Base` — a pointer to the derived class converts to one
  /// to the base, and the base's methods are reachable through it. Single,
  /// non-virtual inheritance only: `this` is the same address for both.
  std::string BaseName;
  SourceRange BaseRange;
  struct NominalDecl *BaseDecl = nullptr;   ///< set by Sema
};

/// A nominal type that owns fields and methods.
struct NominalDecl : Decl {
  /// Set when this declaration is one the language depends on, so `T?`, `nil`
  /// and `?` can be lowered onto ordinary Rune enums.
  LangItem Lang = LangItem::NotSpecial;
  std::vector<GenericParam> Generics;
  std::vector<WhereClause> WhereClauses;
  std::vector<std::unique_ptr<FieldDecl>> Fields;
  std::vector<std::unique_ptr<FunctionDecl>> Methods;
  /// Populated by Sema: every mark bound to this type.
  std::vector<struct BindDecl *> Bindings;
  /// Instantiation bookkeeping for generic nominal types.
  NominalDecl *GenericTemplate = nullptr;
  std::vector<Type *> TypeArguments;
  std::vector<NominalDecl *> Instantiations;
  Type *DeclaredType = nullptr;
  /// The `deinit` this type runs on the way out, wherever it was written:
  /// in the body, in an `extend`, or supplied by a `bind`. A class's runs
  /// when its last reference goes; a struct's or an enum's runs when the
  /// value it lives in is destroyed, which is what makes a value able to own
  /// something a reference count cannot see — a file descriptor, a lock.
  FunctionDecl *Deinit = nullptr;
  /// The type's own `clone(&self) -> Self`, wherever it was written — in the
  /// body, in an `extend`, or supplied by a `bind`. `$clone()` calls it
  /// instead of copying the type member by member, which is what a type
  /// holding something a copy must not duplicate depends on.
  FunctionDecl *CloneFn = nullptr;
  /// `struct Derived : Base` / `enum Derived : Base` — the type this one
  /// extends. Its fields come first, or its variants do, so a `Derived` is a
  /// `Base` with more on the end and may be read as one.
  TypeReprPtr Inherits;
  NominalDecl *InheritsDecl = nullptr;   ///< set by Sema
  /// Set once the parent's members have been spliced in, so it happens once
  /// however many things ask.
  bool InheritanceDone = false;
  /// Present when this type was declared inside an `extern "C++"` block.
  std::unique_ptr<CxxDeclInfo> Cxx;

  using Decl::Decl;
};

struct StructDecl : NominalDecl {
  StructDecl() : NominalDecl(NodeKind::Struct) {}
};

enum class VariantShape : uint8_t { Unit, Tuple, Struct };

struct EnumVariantDecl : Decl {
  VariantShape Shape = VariantShape::Unit;
  std::vector<TypeReprPtr> TupleTypes;
  std::vector<std::unique_ptr<FieldDecl>> Fields;
  ExprPtr Discriminant;
  unsigned Index = 0;
  /// The tag: the declared integer value, or the next one after the last.
  int64_t Value = 0;
  /// The declared value of a float-valued enum's variant (see `RawFloat`).
  double FloatValue = 0;
  EnumVariantDecl() : Decl(NodeKind::EnumVariant) {}
};

struct EnumDecl : NominalDecl {
  std::vector<std::unique_ptr<EnumVariantDecl>> Variants;
  /// True when every variant is a unit variant (a plain tagged enum).
  bool IsSimple = true;
  /// `f64` or `f32` when the variants' values are floats, `USD = 1.0`,
  /// `AUD = 1.43`: the tags are then just 0, 1, 2…, each variant keeps its
  /// value in `FloatValue`, and `as` gives that value. Null for the usual
  /// integer-valued enum, whose tags are its values.
  Type *RawFloat = nullptr;
  EnumDecl() : NominalDecl(NodeKind::Enum) {}
};

struct ClassDecl : NominalDecl {
  TypeReprPtr SuperClass;
  ClassDecl *Super = nullptr;
  FunctionDecl *Init = nullptr;
  /// Flattened virtual method table; index matches FunctionDecl::VTableIndex.
  std::vector<FunctionDecl *> VTable;
  /// Byte offset at which this class's own fields begin.
  unsigned FieldBase = 0;
  ClassDecl() : NominalDecl(NodeKind::Class) {}
};

/// `mark Show { fn show(&self) -> String }`
/// `type Item` inside a mark, and `type Item = i64` inside a bind.
struct AssociatedTypeDecl : Decl {
  /// Present only in a `bind`: the type the implementation chooses.
  TypeReprPtr Value;
  /// Bounds the mark requires of it: `type Item: Display`.
  std::vector<TypeReprPtr> Bounds;
  Type *Resolved = nullptr;
  AssociatedTypeDecl() : Decl(NodeKind::AssociatedType) {}
};

struct MarkDecl : NominalDecl {
  /// Super-marks: `mark Ord: Eq { ... }`.
  std::vector<TypeReprPtr> SuperMarks;
  std::vector<MarkDecl *> Supers;
  /// `type Item` — every binding has to say what it is.
  std::vector<std::unique_ptr<AssociatedTypeDecl>> AssociatedTypes;
  /// `@auto`: a mark nobody implements. It asks nothing of a type, so the
  /// compiler answers for it — a type has it when every part has it — and a
  /// `bind` or a `@never` says otherwise where the structure cannot.
  bool IsAuto = false;
  MarkDecl() : NominalDecl(NodeKind::Mark) {}
};

/// `bind Show to Point { ... }` or `bind operator::add to Vec2 { ... }`.
/// `bind Celsius into Fahrenheit` is parsed as `bind As<Fahrenheit> to Celsius`.
struct BindDecl : Decl {
  std::vector<GenericParam> Generics;
  std::vector<WhereClause> WhereClauses;
  /// The mark path; for operator overloads this is {"operator", "add"}.
  std::vector<std::string> MarkPath;
  /// `bind As<Fahrenheit> to Celsius` — the mark's own type arguments.
  /// `bind Celsius into Fahrenheit` fills this with `Fahrenheit`.
  std::vector<TypeReprPtr> MarkGenericArgs;
  SourceRange MarkRange;
  TypeReprPtr TargetType;
  std::vector<std::unique_ptr<FunctionDecl>> Methods;
  /// `type Item = i64` inside the bind body.
  std::vector<std::unique_ptr<AssociatedTypeDecl>> AssociatedTypes;

  MarkDecl *ResolvedMark = nullptr;
  Type *ResolvedTarget = nullptr;
  bool IsOperatorBinding = false;
  std::string OperatorName;   ///< canonical form, e.g. "add", "index"
  BindDecl() : Decl(NodeKind::Bind) {}
};

/// `extend Point { fn length(&self) -> f64 { ... } }`
struct ExtendDecl : Decl {
  std::vector<GenericParam> Generics;
  TypeReprPtr TargetType;
  std::vector<std::unique_ptr<FunctionDecl>> Methods;
  Type *ResolvedTarget = nullptr;
  /// Set when the target is a generic type: the methods have been folded into
  /// that type's declaration, so every instantiation clones them along with
  /// the ones written inside the type. Nothing is left here to register.
  bool FoldedIntoTemplate = false;
  ExtendDecl() : Decl(NodeKind::Extend) {}
};

struct GlobalVarDecl : ValueDecl {
  TypeReprPtr TypeAnnotation;
  ExprPtr Init;
  bool IsMutable = false;
  /// For an `extern "C++"` variable: the namespaces it was declared in.
  std::vector<std::string> CxxScope;
  VarDecl *Storage = nullptr;
  void *CodeGenGlobal = nullptr;
  GlobalVarDecl() : ValueDecl(NodeKind::GlobalVar) {}
};

/// `import std::io` / `import std::io as sio` / `import std::{io, fs}`.
struct ImportDecl : Decl {
  std::vector<std::string> Path;
  std::string Alias;
  /// Specific names pulled into scope; empty means the module itself.
  std::vector<std::string> Names;
  bool IsGlob = false;
  /// Resolved module, filled in by the module loader.
  struct Module *ResolvedModule = nullptr;
  ImportDecl() : Decl(NodeKind::Import) {}
};

/// `extern "C" { ... }` or `extern "C++" { ... }`.
///
/// The types an `extern "C++"` block declares — its classes, structs and
/// enums — are not kept here: the parser hoists them to the module's own
/// declaration list, each carrying a `CxxDeclInfo`, so every later pass sees
/// an ordinary nominal type.
struct ExternDecl : Decl {
  std::string ABI;
  std::vector<std::unique_ptr<FunctionDecl>> Functions;
  std::vector<std::unique_ptr<GlobalVarDecl>> Globals;
  ExternDecl() : Decl(NodeKind::Extern) {}
};

struct TypeAliasDecl : Decl {
  std::vector<GenericParam> Generics;
  TypeReprPtr Aliased;
  Type *Resolved = nullptr;
  TypeAliasDecl() : Decl(NodeKind::TypeAlias) {}
};

//===----------------------------------------------------------------------===//
// Module
//===----------------------------------------------------------------------===//

struct Module {
  std::string Name;            ///< dotted module path, e.g. "std::io"
  std::string Path;            ///< on-disk source path
  unsigned FileID = 0;
  std::vector<DeclPtr> Decls;
  /// Modules this one imports, in declaration order.
  std::vector<Module *> Imports;
  bool IsStdlib = false;
  /// True when the module came from an imported `.rul`. Its non-generic code
  /// already exists in that library's object file, so CodeGen only declares it.
  bool FromLibrary = false;
  /// Set once Sema has run over this module.
  bool Checked = false;
  /// `@link("m")` and `@linkpath("/usr/local/lib")` at the top of the file:
  /// native libraries and search paths this module needs.
  std::vector<std::string> LinkLibraries;
  std::vector<std::string> LinkPaths;
  /// Other names this module answers to, so one implementation can live at
  /// more than one path.
  std::vector<std::string> Aliases;
  /// `@type(Library)` — what this file produces. Empty when it did not say,
  /// in which case a `main` makes it an executable.
  std::string DeclaredOutput;
  SourceRange DeclaredOutputRange;
};

//===----------------------------------------------------------------------===//
// Casting helpers
//===----------------------------------------------------------------------===//

template <typename T> bool isa(const Node *n) {
  return n && T::classofKind(n->Kind);
}
template <typename T> T *cast(Node *n) { return static_cast<T *>(n); }
template <typename T> const T *cast(const Node *n) {
  return static_cast<const T *>(n);
}
template <typename T> T *dyn_cast(Node *n) {
  return isa<T>(n) ? static_cast<T *>(n) : nullptr;
}
template <typename T> const T *dyn_cast(const Node *n) {
  return isa<T>(n) ? static_cast<const T *>(n) : nullptr;
}

/// Prints a tree in an indented, human-readable form (`--dump-ast`).
void printAST(const Module &m, const SourceManager &sm, std::ostream &os);

} // namespace rune

//===----------------------------------------------------------------------===//
// classofKind specialisations
//===----------------------------------------------------------------------===//
namespace rune {
#define DEF_CLASSOF(Class, KindName)                                           \
  struct Class;                                                                \
  template <> inline bool isa<Class>(const Node *n) {                          \
    return n && n->Kind == NodeKind::KindName;                                 \
  }
DEF_CLASSOF(IntLitExpr, IntLit)
DEF_CLASSOF(FloatLitExpr, FloatLit)
DEF_CLASSOF(StringLitExpr, StringLit)
DEF_CLASSOF(CharLitExpr, CharLit)
DEF_CLASSOF(BoolLitExpr, BoolLit)
DEF_CLASSOF(NilLitExpr, NilLit)
DEF_CLASSOF(ArrayLitExpr, ArrayLit)
DEF_CLASSOF(TupleLitExpr, TupleLit)
DEF_CLASSOF(StructLitExpr, StructLit)
DEF_CLASSOF(DeclRefExpr, DeclRef)
DEF_CLASSOF(UnaryExpr, Unary)
DEF_CLASSOF(BinaryExpr, Binary)
DEF_CLASSOF(AssignExpr, Assign)
DEF_CLASSOF(CallExpr, Call)
DEF_CLASSOF(MemberExpr, Member)
DEF_CLASSOF(IndexExpr, Index)
DEF_CLASSOF(CastExpr, Cast)
DEF_CLASSOF(IntoExpr, Into)
DEF_CLASSOF(TypeTestExpr, TypeTest)
DEF_CLASSOF(ClosureExpr, Closure)
DEF_CLASSOF(BlockExpr, Block)
DEF_CLASSOF(IfExpr, If)
DEF_CLASSOF(WhileExpr, While)
DEF_CLASSOF(LoopExpr, Loop)
DEF_CLASSOF(ForExpr, For)
DEF_CLASSOF(MatchExpr, Match)
DEF_CLASSOF(ReturnExpr, Return)
DEF_CLASSOF(BreakExpr, Break)
DEF_CLASSOF(ContinueExpr, Continue)
DEF_CLASSOF(BorrowExpr, Borrow)
DEF_CLASSOF(DerefExpr, Deref)
DEF_CLASSOF(RangeExpr, Range)
DEF_CLASSOF(SelfExpr, SelfRef)
DEF_CLASSOF(SuperExpr, SuperRef)
DEF_CLASSOF(UnsafeBlockExpr, UnsafeBlock)
DEF_CLASSOF(TryExpr, Try)
DEF_CLASSOF(ErrorExpr, Error)
DEF_CLASSOF(ExprStmt, ExprStmt)
DEF_CLASSOF(VarStmtNode, VarStmt)
DEF_CLASSOF(DeclStmt, DeclStmtKind)
DEF_CLASSOF(DeferStmtNode, DeferStmt)
DEF_CLASSOF(FunctionDecl, Function)
DEF_CLASSOF(StructDecl, Struct)
DEF_CLASSOF(EnumDecl, Enum)
DEF_CLASSOF(ClassDecl, Class)
DEF_CLASSOF(MarkDecl, Mark)
DEF_CLASSOF(BindDecl, Bind)
DEF_CLASSOF(ExtendDecl, Extend)
DEF_CLASSOF(GlobalVarDecl, GlobalVar)
DEF_CLASSOF(VarDecl, LocalVar)
DEF_CLASSOF(ImportDecl, Import)
DEF_CLASSOF(ExternDecl, Extern)
DEF_CLASSOF(TypeAliasDecl, TypeAlias)
DEF_CLASSOF(AssociatedTypeDecl, AssociatedType)
DEF_CLASSOF(FieldDecl, Field)
DEF_CLASSOF(EnumVariantDecl, EnumVariant)
DEF_CLASSOF(UniqTypeRepr, UniqType)
DEF_CLASSOF(MoveExpr, Move)
DEF_CLASSOF(NamedTypeRepr, NamedType)
DEF_CLASSOF(PointerTypeRepr, PointerType)
DEF_CLASSOF(ArrayTypeRepr, ArrayType)
DEF_CLASSOF(SliceTypeRepr, SliceType)
DEF_CLASSOF(TupleTypeRepr, TupleType)
DEF_CLASSOF(FunctionTypeReprNode, FunctionTypeRepr)
DEF_CLASSOF(OptionalTypeRepr, OptionalType)
DEF_CLASSOF(DynTypeRepr, DynType)
DEF_CLASSOF(SomeTypeRepr, SomeType)
DEF_CLASSOF(SelfTypeRepr, SelfType)
DEF_CLASSOF(InferTypeRepr, InferType)
DEF_CLASSOF(WildcardPattern, WildcardPat)
DEF_CLASSOF(BindingPattern, BindingPat)
DEF_CLASSOF(LiteralPattern, LiteralPat)
DEF_CLASSOF(TuplePattern, TuplePat)
DEF_CLASSOF(StructPattern, StructPat)
DEF_CLASSOF(EnumPattern, EnumPat)
DEF_CLASSOF(PathPattern, PathPat)
DEF_CLASSOF(RangePattern, RangePat)
DEF_CLASSOF(OrPattern, OrPat)
DEF_CLASSOF(RefPattern, RefPat)
DEF_CLASSOF(SlicePattern, SlicePat)
#undef DEF_CLASSOF

/// NominalDecl covers three kinds, so it needs a hand-written test.
template <> inline bool isa<NominalDecl>(const Node *n) {
  return n && (n->Kind == NodeKind::Struct || n->Kind == NodeKind::Enum ||
               n->Kind == NodeKind::Class || n->Kind == NodeKind::Mark);
}
template <> inline bool isa<ValueDecl>(const Node *n) {
  return n && (n->Kind == NodeKind::Function || n->Kind == NodeKind::GlobalVar ||
               n->Kind == NodeKind::LocalVar || n->Kind == NodeKind::Field);
}
} // namespace rune

#endif
