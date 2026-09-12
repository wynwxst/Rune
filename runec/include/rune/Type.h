//===- Type.h - Rune's canonical type representation -----------*- C++ -*-===//
//
// Types are interned in a TypeContext, so two structurally identical types are
// the same pointer and `==` is a pointer comparison.
//
// Memory model
// ------------
// Value types (integers, floats, structs, tuples, arrays, enums without
// reference payloads) are copied. Reference types (classes, String, closures)
// live on the heap behind a RuneObject header and are managed by automatic
// reference counting; `isRefCounted()` is what drives retain/release emission.
//
//===----------------------------------------------------------------------===//
#ifndef RUNE_TYPE_H
#define RUNE_TYPE_H

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace rune {

struct Decl;
struct FunctionDecl;
struct StructDecl;
struct EnumDecl;
struct ClassDecl;
struct MarkDecl;
struct NominalDecl;

enum class TypeKind : uint8_t {
  Error,     ///< Poisoned; suppresses cascading diagnostics.
  Void,      ///< `()`, the empty tuple.
  Never,     ///< The type of `return`, `break`, and diverging calls.
  Bool,
  Int,
  Float,
  Char,      ///< `Character`: one Unicode scalar.
  CString,   ///< `CString`: a borrowed, NUL-terminated `*u8` for FFI.
  String,    ///< `String`: heap allocated, reference counted, UTF-8.
  Pointer,   ///< `&T`, `&var T`, `*T`, `*var T`
  Array,     ///< `[N:T]`
  Slice,     ///< `[T]`
  Tuple,
  Function,
  CFunction, ///< `@cfunction(...)`: a bare pointer, no environment
  Struct,
  Enum,
  Class,
  Mark,      ///< The mark itself, used as a bound.
  DynMark,   ///< `dyn Mark`: an existential with a vtable.
  Any,       ///< `Any`: a value of unknown type, carrying its own descriptor.
  Generic,   ///< An unsubstituted type parameter.
  /// `some Mark` before the function that returns it has been checked: the
  /// concrete type is not yet known. Once it is, the type takes on that
  /// type's kind — see `TypeContext::resolveOpaque` — and this kind is never
  /// seen again. `isOpaque()` is what stays true throughout.
  Opaque,
};

class TypeContext;

class Type {
public:
  TypeKind kind() const { return K; }

  bool is(TypeKind k) const { return K == k; }
  bool isError() const { return K == TypeKind::Error; }
  bool isVoid() const { return K == TypeKind::Void; }
  bool isNever() const { return K == TypeKind::Never; }
  bool isBool() const { return K == TypeKind::Bool; }
  bool isInt() const { return K == TypeKind::Int; }
  bool isFloat() const { return K == TypeKind::Float; }
  bool isNumeric() const { return isInt() || isFloat(); }
  bool isNominal() const {
    return K == TypeKind::Struct || K == TypeKind::Enum ||
           K == TypeKind::Class || K == TypeKind::Mark;
  }
  bool isGeneric() const { return K == TypeKind::Generic; }
  bool isAny() const { return K == TypeKind::Any; }

  //=== `some Mark` =======================================================//
  //
  // An opaque type is a concrete type wearing a veil. It is interned once
  // per function that returns it, so it compares equal to nothing but
  // itself; inside, it *is* the concrete type — same kind, same layout, same
  // reference counting — which is what lets every question about
  // representation be answered without knowing it was ever opaque. The
  // checker is the one place the veil matters: name lookup on the value
  // reaches only what the mark declares, and nothing but the defining
  // function may treat it as the type behind it.
  /// True for `some Mark`, resolved or not.
  bool isOpaque() const { return Opaque; }
  /// The function whose result this is.
  FunctionDecl *opaqueOwner() const { return OpaqueOwner; }
  /// The mark the caller sees.
  MarkDecl *opaqueMark() const { return OpaqueMark; }
  /// The type behind the veil, or null until the owner's body has fixed it.
  Type *opaqueUnderlying() const { return OpaqueUnder; }
  /// The type as the code generator wants it: the one behind a `some`, and
  /// `t` itself otherwise.
  Type *canonical() { return Opaque && OpaqueUnder ? OpaqueUnder : this; }

  /// True when values need retain/release. Classes, String and closures own a
  /// heap allocation; aggregates inherit the property from their elements.
  bool isRefCounted() const;

  /// True when the type is represented as a single machine pointer.
  bool isPointerLike() const {
    return K == TypeKind::Class || K == TypeKind::String ||
           K == TypeKind::Pointer || K == TypeKind::CString ||
           K == TypeKind::Any;
  }

  /// True when a bitwise copy is a valid copy (no ARC traffic needed).
  bool isTriviallyCopyable() const { return !isRefCounted(); }

  /// True when a value of this type is a handle to something on the heap
  /// that stays put when the handle moves: a class, a `String`, a closure, a
  /// `dyn Mark` or an `Any` box. What such a handle owns is what an internal
  /// reference (`&T from self.field`) may point into.
  bool isHeapHandle() const {
    return K == TypeKind::Class || K == TypeKind::String ||
           K == TypeKind::Function || K == TypeKind::DynMark ||
           K == TypeKind::Any;
  }

  /// True when the type contains a generic parameter anywhere inside it.
  bool containsGenericParam() const;

  std::string toString() const;

  //=== Accessors, valid for the matching kind only ======================//
  unsigned intWidth() const { return Width; }
  bool isSigned() const { return Signed; }
  unsigned floatWidth() const { return Width; }

  Type *pointee() const { return Elem; }
  bool isMutablePointer() const { return Mutable; }
  bool isRawPointer() const { return Raw; }
  bool isWeakPointer() const { return Weak; }

  /// True for `uniq T`: a class reference that can be moved or borrowed but
  /// never copied, so its count never exceeds one and it cannot join a ring.
  bool isUniq() const { return Uniq; }

  Type *element() const { return Elem; }
  uint64_t arraySize() const { return ArraySize; }

  const std::vector<Type *> &tupleElements() const { return Elems; }

  const std::vector<Type *> &params() const { return Elems; }
  Type *result() const { return Elem; }
  bool isVariadicFunction() const { return Variadic; }

  NominalDecl *nominal() const { return Nominal; }
  MarkDecl *mark() const;
  const std::vector<Type *> &typeArguments() const { return Elems; }

  const std::string &genericName() const { return Name; }
  unsigned genericIndex() const { return Width; }

private:
  friend class TypeContext;
  explicit Type(TypeKind k) : K(k) {}

  TypeKind K;
  bool Signed = false;
  bool Mutable = false;
  bool Raw = false;
  bool Weak = false;
  bool Uniq = false;
  bool Variadic = false;
  bool Opaque = false;
  FunctionDecl *OpaqueOwner = nullptr;
  MarkDecl *OpaqueMark = nullptr;
  Type *OpaqueUnder = nullptr;
  unsigned Width = 0;
  uint64_t ArraySize = 0;
  Type *Elem = nullptr;              ///< pointee / element / function result
  std::vector<Type *> Elems;         ///< tuple, params, generic arguments
  NominalDecl *Nominal = nullptr;
  std::string Name;                  ///< generic parameter name
  mutable int RefCountedCache = -1;
};

/// Owns and interns every Type. One per compilation.
class TypeContext {
public:
  /// `pointerBits` is the target's pointer width, which is what `usize` and
  /// `isize` are. It comes from the target machine's data layout, so a cross
  /// build sizes them for the machine being built for rather than this one.
  explicit TypeContext(unsigned pointerBits = 64);

  //=== Primitives =======================================================//
  Type *errorType() const { return TyError; }
  Type *voidType() const { return TyVoid; }
  Type *neverType() const { return TyNever; }
  Type *boolType() const { return TyBool; }
  Type *charType() const { return TyChar; }
  Type *cstringType() const { return TyCString; }
  Type *stringType() const { return TyString; }
  Type *anyType() const { return TyAny; }

  Type *intType(unsigned width, bool isSigned);
  Type *floatType(unsigned width);

  Type *i8() { return intType(8, true); }
  Type *i16() { return intType(16, true); }
  Type *i32() { return intType(32, true); }
  Type *i64() { return intType(64, true); }
  Type *u8() { return intType(8, false); }
  Type *u32() { return intType(32, false); }
  Type *u64() { return intType(64, false); }
  Type *usize() { return intType(PointerBits, false); }
  Type *isize() { return intType(PointerBits, true); }

  /// The target's pointer width in bits — the width of `usize` and `isize`.
  unsigned pointerBits() const { return PointerBits; }
  Type *f32() { return floatType(32); }
  Type *f64() { return floatType(64); }

  //=== Constructors =====================================================//
  Type *pointerTo(Type *pointee, bool isMutable, bool isRaw, bool isWeak = false);
  Type *arrayOf(Type *element, uint64_t size);
  Type *sliceOf(Type *element);
  Type *tupleOf(std::vector<Type *> elements);
  Type *functionOf(std::vector<Type *> params, Type *result, bool variadic = false);
  /// A bare function pointer — what C calls a function pointer, with no
  /// captured environment. Convertible from a raw pointer, callable directly.
  Type *cfunctionOf(std::vector<Type *> params, Type *result,
                    bool variadic = false);
  Type *nominalOf(NominalDecl *decl, std::vector<Type *> typeArgs = {});
  /// `uniq T` for a class `T`. Interned separately from `T`, so the two are
  /// distinguishable by pointer, but lowered identically.
  Type *uniqOf(Type *classType);
  /// `T` for `uniq T`, and `t` unchanged otherwise. Use wherever the question
  /// is about the class itself — method lookup, conformance, layout.
  static Type *stripUniq(Type *t);
  Type *dynMarkOf(MarkDecl *decl);
  Type *genericParam(const std::string &name, unsigned index);
  /// `some mark`, as returned by `owner`. One per owner: every call to the
  /// function yields the same type, and no other function's `some mark` is
  /// it. Starts out `TypeKind::Opaque`; `resolveOpaque` fills it in.
  Type *opaqueOf(FunctionDecl *owner, MarkDecl *mark);
  /// Fixes the type behind `opaque` to `under`: from here the opaque type
  /// answers every representational question exactly as `under` does, while
  /// remaining a distinct type. Called once, by the owner's body check.
  void resolveOpaque(Type *opaque, Type *under);

  /// Looks up a builtin type by its source spelling (`i64`, `int`, `float`...).
  /// Returns null when `name` is not a builtin.
  Type *builtinNamed(const std::string &name);

  /// Substitutes generic parameters using `map` (keyed by parameter name).
  Type *substitute(Type *t, const std::map<std::string, Type *> &map);

  /// Structural match of `pattern` (which may contain generic params) against
  /// `concrete`, accumulating bindings. Returns false on conflict.
  bool unify(Type *pattern, Type *concrete, std::map<std::string, Type *> &out);

private:
  unsigned PointerBits;
  std::vector<std::unique_ptr<Type>> Owned;

  Type *make(TypeKind k) {
    Owned.push_back(std::unique_ptr<Type>(new Type(k)));
    return Owned.back().get();
  }

  Type *TyError, *TyVoid, *TyNever, *TyBool, *TyChar, *TyCString, *TyString;
  Type *TyAny;
  std::map<std::pair<unsigned, bool>, Type *> Ints;
  std::map<unsigned, Type *> Floats;
  std::map<std::string, Type *> Interned; ///< keyed by a structural signature
  std::unordered_map<std::string, Type *> Builtins;

  static std::string signatureOf(const Type *t);
};

/// The name `Any` reports for `t` at run time: fully qualified for a nominal
/// type, structural for everything else. Unlike `toString`, which is written
/// for diagnostics, this is a canonical identity — two types share it only if
/// they are the same type.
std::string runtimeTypeName(const Type *t);

/// True when a value of `t` can be stored in an `Any`.
bool isStorableInAny(const Type *t);

/// True when `t` is the standard library's `Option<T>` / `Result<T, E>`.
bool isOptionType(const Type *t);
bool isResultType(const Type *t);
/// The `T` of an `Option<T>`, or null.
Type *optionPayload(const Type *t);
/// The `T` and `E` of a `Result<T, E>`, or null.
Type *resultValue(const Type *t);
Type *resultError(const Type *t);
/// Index of a variant by name, or -1.
int variantIndexNamed(const Type *t, const char *name);

/// True when a value of `from` may be used where `to` is expected without an
/// explicit cast (identity, never-to-anything, and safe widening).
bool isImplicitlyConvertible(Type *from, Type *to);
/// True when values of `t` carry a `deinit` somewhere inside.
bool typeHasDeinit(Type *t);

/// True when `as` may convert between the two (numeric conversions, pointer
/// casts, enum/integer conversions).
bool isExplicitlyCastable(Type *from, Type *to);

} // namespace rune

#endif
