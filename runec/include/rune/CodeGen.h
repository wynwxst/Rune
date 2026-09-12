//===- CodeGen.h - Lowering the typed AST to LLVM IR -----------*- C++ -*-===//
//
// Memory management
// -----------------
// Reference counting is inserted here, using a deliberately simple ownership
// convention that is easy to audit:
//
//   * A function returns reference-counted results at +1 (the caller owns it).
//   * emitRValue always hands back a **borrowed** (+0) value. Anything that
//     arrives at +1 is parked in the statement's temporary list, so the
//     statement owns it and releases it when it finishes.
//   * Storing into a slot retains the incoming value and releases the old one.
//   * Leaving a scope releases every local it declared.
//
// That costs a little extra retain/release traffic compared to a flow-sensitive
// scheme, but it never leaks and never over-releases.
//
//===----------------------------------------------------------------------===//
#ifndef RUNE_CODEGEN_H
#define RUNE_CODEGEN_H

#include "rune/AST.h"
#include "rune/Diagnostics.h"
#include "rune/Driver.h"
#include "rune/Sema.h"
#include "rune/Type.h"

#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace rune {

class CodeGen {
public:
  CodeGen(const SourceManager &sm, DiagnosticEngine &diags, TypeContext &types,
          const SemaResult &sema, const CompilerOptions &opts);
  ~CodeGen();

  /// Emits everything Sema collected. Returns false if an error was reported.
  bool run();

  llvm::Module &module() { return *M; }
  llvm::LLVMContext &context() { return *Ctx; }
  /// Hands ownership of the finished module to the caller.
  std::unique_ptr<llvm::Module> takeModule() { return std::move(M); }

private:
  const SourceManager &SM;
  DiagnosticEngine &Diags;
  TypeContext &Types;
  const SemaResult &Sema;
  const CompilerOptions &Opts;

  std::unique_ptr<llvm::LLVMContext> Ctx;
  std::unique_ptr<llvm::Module> M;
  std::unique_ptr<llvm::IRBuilder<>> B;

  //=== Cached LLVM types and runtime declarations ========================//
  llvm::StructType *ObjectHeaderTy = nullptr; ///< { i64 refcount, ptr type }
  llvm::StructType *TypeInfoTy = nullptr;
  llvm::PointerType *PtrTy = nullptr;

  std::map<Type *, llvm::Type *> LoweredTypes;
  std::map<NominalDecl *, llvm::StructType *> NominalLayouts;
  std::map<NominalDecl *, llvm::GlobalVariable *> TypeInfos;
  std::map<NominalDecl *, llvm::Function *> ClassDeinits;
  /// `rune.clone.<type>` — a fresh copy of an object or an enum, generated
  /// on demand for `$clone()`.
  std::map<Type *, llvm::Function *> CloneFns;
  std::map<std::string, llvm::GlobalVariable *> StringLiterals;
  std::map<FunctionDecl *, llvm::Function *> Functions;
  std::map<GlobalVarDecl *, llvm::GlobalVariable *> Globals;
  std::map<llvm::Function *, llvm::Function *> ValueThunks;
  /// Environment layout `{ header, capture... }` for each closure.
  std::map<ClosureExpr *, llvm::StructType *> ClosureEnvTypes;
  std::map<ClosureExpr *, llvm::GlobalVariable *> ClosureEnvInfos;
  std::map<ClosureExpr *, llvm::Function *> ClosureEnvDeinits;
  /// Per (mark, concrete type) dispatch table for mark objects.
  std::map<std::pair<MarkDecl *, Type *>, llvm::GlobalVariable *> MarkVTables;
  /// Thunks adapting a concrete method to the uniform `(ptr self, ...)` shape.
  std::map<std::pair<FunctionDecl *, Type *>, llvm::Function *> MarkThunks;
  /// Type metadata for values boxed into a mark object.
  std::map<Type *, llvm::GlobalVariable *> BoxInfos;
  /// Whether each type has a `deinit` somewhere inside it. The walk follows
  /// fields, so it is worth answering once per type.
  std::map<Type *, bool> DeinitCache;
  /// The one expression whose result is being stored into a slot that takes
  /// ownership of it as it stands. A fresh construction already carries the
  /// count the slot needs, so the usual retain-store-release round trip is
  /// left out: the allocation's own reference becomes the binding's.
  ///
  /// Only set for a local the ownership pass proved never leaves its scope,
  /// which is what makes "nothing else refers to it" a fact rather than a
  /// guess about the code that follows.
  Expr *AdoptedResult = nullptr;

  //=== Per-function state ================================================//
  struct LoopFrame {
    llvm::BasicBlock *Continue = nullptr;
    llvm::BasicBlock *Break = nullptr;
    /// Slot receiving the value of `break value` for a value-producing loop.
    llvm::Value *ResultSlot = nullptr;
    Type *ResultType = nullptr;
    std::string Label;
    size_t ScopeDepth = 0;
  };

  /// A slot the scope is responsible for destroying on the way out.
  struct OwnedSlot {
    llvm::Value *Addr = nullptr;
    Type *Ty = nullptr;
    /// An `i1` alloca saying whether this slot still owns what it holds.
    /// Present only where some mention of the binding hands the value on, so
    /// a `deinit` that already ran on an early return does not run again at
    /// the end of the block. Null — the usual case — means it always owns it.
    llvm::Value *LiveFlag = nullptr;

    OwnedSlot(llvm::Value *addr, Type *ty, llvm::Value *flag = nullptr)
        : Addr(addr), Ty(ty), LiveFlag(flag) {}
  };

  /// One entry per `{ ... }`; owns the releases for the locals declared in it.
  struct LexicalScope {
    std::vector<OwnedSlot> Locals;
    /// `defer` bodies, run in reverse order when the scope ends.
    std::vector<Expr *> Deferred;
  };

  struct FunctionState {
    FunctionDecl *Decl = nullptr;
    ClosureExpr *Closure = nullptr;
    llvm::Function *Fn = nullptr;
    llvm::Value *ReturnSlot = nullptr;
    llvm::BasicBlock *ReturnBlock = nullptr;
    Type *ReturnType = nullptr;
    llvm::Value *SelfValue = nullptr;
    llvm::Value *EnvValue = nullptr;
    std::vector<LexicalScope> Scopes;
    std::vector<LoopFrame> Loops;
    /// Temporaries owned by the statement being emitted.
    std::vector<std::pair<llvm::Value *, Type *>> Temps;
    /// Under Zombie: which temporary slot holds a tracked value, so a
    /// consumer that keeps the value can take it back out of the statement's
    /// cleanup (`adopt`).
    std::map<llvm::Value *, llvm::Value *> TempOf;
    std::map<VarDecl *, llvm::Value *> Slots;
    /// `i1` flags for the locals whose value can be handed on; see OwnedSlot.
    std::map<VarDecl *, llvm::Value *> LiveFlags;
    std::map<VarDecl *, unsigned> CaptureIndex;
  };
  std::vector<FunctionState> FnStack;
  FunctionState &fs() { return FnStack.back(); }

  //=== Setup =============================================================//
  //=== Debug information ================================================//
  // Emitted only for `-g`: a compile unit, a subprogram per function, a
  // location on every instruction, and a declaration for every parameter and
  // local so a debugger can name and read them.
  std::unique_ptr<llvm::DIBuilder> DI;
  llvm::DICompileUnit *DICU = nullptr;
  std::map<unsigned, llvm::DIFile *> DIFiles;
  std::map<Type *, llvm::DIType *> DITypes;
  std::vector<llvm::DIScope *> DIScopes;

  void initDebugInfo();
  void finishDebugInfo();
  llvm::DIFile *debugFileFor(SourceRange r);
  llvm::DIType *debugTypeFor(Type *t);
  llvm::DISubprogram *debugSubprogramFor(FunctionDecl *fn, llvm::Function *f);
  /// Points the builder at `r`, so instructions emitted next carry it.
  void setDebugLocation(SourceRange r);
  void declareDebugVariable(VarDecl *v, llvm::Value *slot, unsigned argIndex);

  void applyTargetLayout();
  void initRuntimeTypes();
  llvm::FunctionCallee runtimeFn(const char *name, llvm::Type *ret,
                                 std::vector<llvm::Type *> params,
                                 bool variadic = false);

  //=== Type lowering =====================================================//
  llvm::Type *lower(Type *t);
  llvm::Type *lowerReturn(Type *t);
  llvm::StructType *layoutOf(NominalDecl *nd, Type *t);
  llvm::FunctionType *functionTypeFor(FunctionDecl *fn);
  /// Byte size of an enum's largest payload.
  uint64_t enumPayloadSize(EnumDecl *e);
  llvm::Type *variantPayloadType(EnumDecl *e, unsigned variantIndex);
  /// Every field of a class, base classes first, matching Sema's indices.
  std::vector<FieldDecl *> allFieldsOf(NominalDecl *nd);

  //=== Reference counting ================================================//
  void emitRetain(llvm::Value *v, Type *t);
  void emitRelease(llvm::Value *v, Type *t);

  //=== Ownership under `--memory zombie` =================================//
  // Nothing is counted: every owned value has one owner, and handing it on
  // is a move. Where ARC retains, Zombie either empties the place the value
  // came from or takes a fresh temporary away from the statement's cleanup.
  bool zombie() const { return Opts.Memory == MemoryMode::Zombie; }
  /// `v`, just produced from `e` of type `t`, is being kept by something
  /// (a slot, a parameter, a field). ARC: retain. Zombie: the source gives
  /// it up — a place is emptied, a temporary is adopted.
  void takeOwnership(Expr *e, llvm::Value *v, Type *t);
  /// Takes a tracked temporary out of the statement's cleanup.
  void adopt(llvm::Value *v);
  /// Empties the place `e` names once its value has been moved out.
  void emptyPlace(Expr *e, Type *t);
  /// The place expression a consuming read of `e` actually moves from —
  /// through casts and blocks — or null when `e` produces a fresh value.
  Expr *movedPlaceOf(Expr *e);
  /// `&T` for a class, `String`, closure or mark object is the handle itself,
  /// not the address of a slot holding it. A `&var T` is the slot.
  bool handleBorrow(Type *t) const;
  /// Releases every reference held inside an aggregate stored at `addr`.
  void emitReleaseFields(llvm::Value *addr, Type *t);

  //=== Value destructors =================================================//
  // A struct or an enum may run a `deinit` of its own. Unlike a class's,
  // which the runtime calls when the count reaches zero, a value's is called
  // here: at the end of the scope that holds it, at the end of the statement
  // that made it, and from inside whatever contains it.
  //
  /// The `deinit` `t` runs, or null. Looks through borrows.
  FunctionDecl *valueDeinit(Type *t);
  /// True when a value of `t` has a `deinit` somewhere inside it.
  bool hasValueDeinit(Type *t);
  /// True when destroying a value of `t` does anything at all — a reference
  /// to release, or a `deinit` to run.
  bool needsDestruction(Type *t) { return t && (t->isRefCounted() ||
                                                hasValueDeinit(t)); }
  /// Runs `t`'s `deinit`, and those of everything inside it, on the value at
  /// `addr`. Releases nothing: reference counting is a separate question, and
  /// a tracked temporary answers it elsewhere.
  void emitRunDeinits(llvm::Value *addr, Type *t);
  /// Destroys the value at `addr` outright: its destructors, then the
  /// references it holds. `live`, when given, is an `i1` guarding the whole
  /// thing — false where the value has already been handed on.
  void emitDestroy(llvm::Value *addr, Type *t, llvm::Value *live = nullptr);
  /// The same for a value in a register: spills it first, because a `deinit`
  /// takes `&self` and there is nothing to point at otherwise.
  void emitDestroyValue(llvm::Value *v, Type *t);
  /// The `i1` flag saying whether `v`'s slot still owns its value, or null
  /// when nothing in this function can take it away.
  llvm::Value *liveFlagFor(VarDecl *v);
  /// True when `init` builds a fresh object for a local that never leaves
  /// this scope, so the slot can take the allocation's count as its own.
  bool adoptsFreshConstruction(VarDecl *v, Expr *init);
  /// Retains or releases the payload of whichever enum variant is active.
  void emitEnumRefCount(llvm::Value *v, Type *t, bool retain);
  /// Registers a +1 temporary so the current statement releases it.
  llvm::Value *track(llvm::Value *v, Type *t);
  /// Releases the statement's temporaries. `consume` is false on an early
  /// exit (`?`, `return` inside a larger expression), where the temporaries
  /// still have to be released on the path that carries on.
  void emitStatementCleanup(bool consume = true);

  /// Statements nested inside a construct own their temporaries. The enclosing
  /// expression's are set aside for the duration: a block or a branch used as
  /// an operand must not release values the expression around it still holds.
  struct TempScope {
    CodeGen &G;
    std::vector<std::pair<llvm::Value *, Type *>> Saved;
    explicit TempScope(CodeGen &g) : G(g), Saved(std::move(g.fs().Temps)) {
      g.fs().Temps.clear();
    }
    ~TempScope() { G.fs().Temps = std::move(Saved); }
    TempScope(const TempScope &) = delete;
    TempScope &operator=(const TempScope &) = delete;
  };
  void emitScopeCleanup(size_t scopeIndex, bool runDeferred = true);
  void emitAllScopeCleanups(size_t downTo);

  /// The name a type answers to at run time — the instantiation's, with its
  /// arguments, so two instantiations of one template are told apart.
  std::string typeDisplayName(NominalDecl *nd);
  /// That name, made into a linker symbol. Used for every per-type global,
  /// all of which have one-definition linkage across object files.
  std::string typeSymbolFor(NominalDecl *nd);

  //=== Declarations ======================================================//
  llvm::Function *declareFunction(FunctionDecl *fn);
  void setMergeableLinkage(llvm::GlobalObject *gv, const std::string &name);
  void setDiscardableLinkage(llvm::GlobalObject *gv, const std::string &name);
  /// True when `d` was declared by the standard library or by an imported
  /// `.rul` — code this artefact carries a copy of rather than owns.
  bool isAncillary(const Decl *d) const;
  /// Drops every definition nothing in the module reaches. What "reaches"
  /// means is decided by linkage: see `setDiscardableLinkage`.
  void pruneUnreachable();
  bool abiRejectsByValue(Type *t);
  bool isSharedRefType(Type *t);
  const char *retainFnFor(Type *t);
  const char *releaseFnFor(Type *t);
  void checkForeignABI(FunctionDecl *fn);
  void emitFunctionBody(FunctionDecl *fn);
  void emitClosureBody(FunctionDecl *lifted);
  llvm::GlobalVariable *emitTypeInfo(NominalDecl *nd);
  llvm::Function *emitClassDeinit(ClassDecl *c);
  /// A copy of `v` that owns its own everything: `$clone()`.
  llvm::Value *emitClone(llvm::Value *v, Type *t);
  llvm::Function *cloneFnFor(Type *t);
  llvm::GlobalVariable *declareGlobal(GlobalVarDecl *g);
  /// Lowers `std::asm`'s two intrinsics. `resultType` is null for the one that
  /// returns nothing; `hasSideEffects` marks the assembly as something that
  /// must run whether or not anything reads it.
  llvm::Value *emitInlineAsm(CallExpr *c, Type *resultType,
                             bool hasSideEffects);
  void emitGlobalInitialisers();
  /// Releases reference-counted globals at exit, so a program that keeps a
  /// String in a global does not look like it leaked.
  void emitDecoratorCalls();
  void emitGlobalTeardown();
  void emitEntryPoint();
  llvm::Function *thunkFor(llvm::Function *target, Type *fnType);
  llvm::StructType *envTypeFor(ClosureExpr *c);
  llvm::GlobalVariable *envTypeInfoFor(ClosureExpr *c);
  /// Builds the argument list for a call, honouring labels and defaults.
  std::vector<llvm::Value *> buildArguments(CallExpr *c, FunctionDecl *fn,
                                            const std::vector<Type *> &paramTypes,
                                            bool variadic);
  /// An i1 for patterns that only test, or null when the pattern binds.
  llvm::Value *simplePatternCondition(Pattern *pat, llvm::Value *addr, Type *t);

  //=== Statements ========================================================//
  void emitStmt(Stmt *s);
  void emitBlock(BlockExpr *b, llvm::Value *resultSlot, Type *resultType);

  //=== Expressions =======================================================//
  /// Borrowed (+0) value of `e`.
  llvm::Value *emitRValue(Expr *e);
  /// Address of an assignable expression.
  llvm::Value *emitLValue(Expr *e);
  /// Evaluates `e` and stores it into `slot`, retaining as needed.
  void emitInto(Expr *e, llvm::Value *slot, Type *slotType, bool raw = false);

  llvm::Value *emitCall(CallExpr *c);
  llvm::Value *emitBinary(BinaryExpr *b);
  llvm::Value *emitUnary(UnaryExpr *u);
  llvm::Value *emitAssign(AssignExpr *a);
  llvm::Value *emitAssignInto(AssignExpr *a, llvm::Value *slot, Type *lt);
  llvm::Value *emitIf(IfExpr *i, llvm::Value *slot, Type *slotType);
  llvm::Value *emitMatch(MatchExpr *m, llvm::Value *slot, Type *slotType);
  llvm::Value *emitFor(ForExpr *f);
  /// `for` over an `Iterator`, or over a `Sequence` that hands one out.
  llvm::Value *emitIteratorFor(ForExpr *f);
  /// The receiver a method wants, given a slot holding the value.
  llvm::Value *selfArgumentFor(FunctionDecl *m, llvm::Value *slot,
                               Type *slotType);
  llvm::Value *emitWhile(WhileExpr *w);
  llvm::Value *emitLoop(LoopExpr *l, llvm::Value *slot, Type *slotType);
  llvm::Value *emitClosureValue(ClosureExpr *c);
  llvm::Value *emitStructLit(StructLitExpr *s);
  llvm::Value *emitArrayLit(ArrayLitExpr *a);
  llvm::Value *emitStringLiteral(const std::string &text, bool asCString);

  // Reflection: what the compiler knows about a type, as a constant.
  llvm::Value *emitKindOf(Type *t, Type *kindType);
  uint64_t reflectFieldCount(Type *t);
  std::string reflectFieldText(Type *t, int64_t index, bool wantType);
  llvm::Value *emitOffsetOf(CallExpr *c, Type *t, const std::string &field);
  bool reflectConforms(Type *t, Type *markType, SourceRange where);
  llvm::Value *emitDescribe(llvm::Value *v, Type *t);
  FunctionDecl *displayMethodFor(Type *t);
  std::string shortNameOf(Type *t);
  llvm::Value *emitBuiltinMethod(CallExpr *c);
  llvm::Value *emitIndexAddress(IndexExpr *i);
  llvm::Value *emitMemberAddress(MemberExpr *m);
  llvm::Value *emitCast(CastExpr *c);
  llvm::Value *emitInto(IntoExpr *e);
  llvm::Value *emitTry(TryExpr *t);
  llvm::Value *emitClassConstruction(CallExpr *c);
  llvm::Value *emitEnumConstruction(CallExpr *c);

  /// Emits the tests and bindings for `pat`, branching to `fail` on mismatch.
  void emitPatternTest(Pattern *pat, llvm::Value *valueAddr, Type *valueType,
                       llvm::BasicBlock *fail);
  /// Binds an irrefutable pattern with no test.
  void emitPatternBind(Pattern *pat, llvm::Value *valueAddr, Type *valueType);
  /// Takes an array or slice at `addr` apart for a slice pattern: tests the
  /// length against `fail` when the value is a slice (a fixed array's length
  /// was settled by Sema), then matches each named element and binds the
  /// `..rest` slice. With `fail` null it only binds.
  void emitSlicePattern(SlicePattern *pat, llvm::Value *addr, Type *t,
                        llvm::BasicBlock *fail);

  //=== Helpers ===========================================================//
  llvm::Value *createEntryAlloca(llvm::Type *ty, const std::string &name);
  /// The same, zeroed where it is allocated — so every path sees an empty
  /// slot, including one that never reaches the declaration.
  llvm::Value *createEntryAllocaZeroed(llvm::Type *ty, const std::string &name);
  llvm::Value *declareLocalSlot(VarDecl *v, const std::string &name);
  /// Numeric / pointer conversion inserted for an implicit coercion.
  llvm::Value *coerce(llvm::Value *v, Type *from, Type *to);
  /// Builds an enum value for `variantIndex` with the given payload fields.
  llvm::Value *emitEnumVariant(Type *enumType, int variantIndex,
                               const std::vector<llvm::Value *> &payload);
  /// Wraps a value as `Option::Some`.
  llvm::Value *emitOptionSome(llvm::Value *v, Type *valueType, Type *optType);
  llvm::Value *emitResultOf(llvm::Value *v, Type *valueType, Type *resultType);
  /// Structural hash of `v`, mixed into `acc`. Needs nothing from the type.
  llvm::Value *emitHash(llvm::Value *v, Type *t, llvm::Value *acc);
  /// Structural equality of `a` and `b`, consistent with `emitHash`.
  llvm::Value *emitEquals(llvm::Value *a, llvm::Value *b, Type *t);
  llvm::Value *emitEnumTag(llvm::Value *enumValue, Type *enumType);
  llvm::Value *emitVariantPayload(llvm::Value *enumValue, Type *enumType,
                                  int variantIndex, unsigned field,
                                  Type *fieldType);
  llvm::Value *emitVariantTest(llvm::Value *enumValue, Type *enumType,
                               const char *name);

  //=== Mark objects (`dyn Mark`) =========================================//
  /// Wraps `v` so a mark object can hold it: classes are already objects,
  /// anything else is copied into a reference-counted box.
  llvm::Value *emitDynBox(llvm::Value *v, Type *concrete);
  /// Type metadata for a boxed value, so releasing the box releases what it
  /// holds.
  llvm::GlobalVariable *boxInfoFor(Type *concrete);
  /// The dispatch table for `concrete` viewed as `mark`.
  llvm::GlobalVariable *markVTableFor(MarkDecl *mark, Type *concrete);
  /// A `(ptr self, ...)` wrapper around a concrete implementation.
  llvm::Function *markThunkFor(FunctionDecl *impl, Type *concrete);
  /// Builds the `{ object, vtable }` pair for a coercion into `dyn Mark`.
  llvm::Value *emitDynCoerce(llvm::Value *v, Type *from, Type *dynType);

  //=== `Any` =============================================================//
  /// The descriptor that identifies `t` at run time: a class's own type info,
  /// so subclasses still answer to their base, and the box descriptor for
  /// everything else. One symbol per type across the whole program, so the
  /// comparison is a pointer comparison.
  llvm::GlobalVariable *typeDescriptorFor(Type *t);
  /// Wraps `v` into an `Any`. Shares its representation with `dyn Mark`: a
  /// class is already an object, anything else is copied into a box.
  llvm::Value *emitAnyBox(llvm::Value *v, Type *concrete);
  /// `any is T`, true when the value inside really is a `T`.
  llvm::Value *emitAnyIs(llvm::Value *any, Type *target);
  /// The value inside, read as `target`. Only correct where `emitAnyIs` has
  /// already said yes; the caller owns the reference that comes back.
  llvm::Value *emitAnyUnbox(llvm::Value *any, Type *target);
  /// `any.typeName()`: the descriptor's name as a `String`.
  llvm::Value *emitAnyTypeName(llvm::Value *any);
  /// Lowers one of the `std::any` intrinsics; returns null if `which` is not
  /// one of them.
  llvm::Value *emitAnyIntrinsic(CallExpr *c, const std::string &which,
                                Type *typeArg);
  llvm::Value *emitBoundsCheck(llvm::Value *index, llvm::Value *length,
                               SourceRange range);
  /// How an integer `+`, `-` or `*` treats a result that does not fit.
  enum class OverflowMode {
    Checked,    ///< trap — or wrap, when the build turned checking off
    Wrapping,   ///< always two's-complement wrap
    Saturating, ///< clamp to the type's limits
  };
  /// `l op r` on integers of type `t`. `Checked` consults the build's
  /// overflow setting and traps through `rune_panic_overflow` when it is on.
  llvm::Value *emitIntArith(BinaryOp op, llvm::Value *l, llvm::Value *r,
                            Type *t, OverflowMode mode, SourceRange range);
  /// `l op r` with an overflow flag rather than a trap: `{result, i1}`.
  std::pair<llvm::Value *, llvm::Value *>
  emitIntArithWithOverflow(BinaryOp op, llvm::Value *l, llvm::Value *r,
                           Type *t);
  /// `-v` on an integer, trapping on the one value that has no negation.
  llvm::Value *emitIntNeg(llvm::Value *v, Type *t, SourceRange range);
  /// A `"file:line:col"` constant used by runtime panic messages.
  llvm::Value *locationString(SourceRange range);
  llvm::Value *emitLengthOf(llvm::Value *addr, Type *t);
  /// Terminates the current block if it has no terminator yet.
  void ensureTerminated(llvm::BasicBlock *target);
  bool blockIsTerminated() const;
  void reportUnsupported(SourceRange range, const std::string &what);
};

/// Runs the LLVM optimisation pipeline at `level` over `m`.
void optimizeModule(llvm::Module &m, unsigned level);

/// Registers every target LLVM was built with, once per process.
///
/// Three places need a target before they can ask anything of one — working
/// out how wide a pointer is, installing the data layout, emitting machine
/// code — and each used to register the whole set for itself. Registration is
/// idempotent but not free: it runs the constructor of every target LLVM
/// knows, and this build knows all of them so that a cross compile can name
/// any of them.
void initialiseTargets();

} // namespace rune

#endif
