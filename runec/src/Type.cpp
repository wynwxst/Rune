#include "rune/Type.h"

#include "rune/AST.h"

namespace rune {

//===----------------------------------------------------------------------===//
// Queries
//===----------------------------------------------------------------------===//

MarkDecl *Type::mark() const {
  return (K == TypeKind::Mark || K == TypeKind::DynMark)
             ? reinterpret_cast<MarkDecl *>(Nominal)
             : nullptr;
}

bool Type::isRefCounted() const {
  if (RefCountedCache >= 0)
    return RefCountedCache != 0;
  bool result = false;
  switch (K) {
  case TypeKind::Class:
  case TypeKind::String:
  case TypeKind::DynMark:
  case TypeKind::Any:
    result = true;
    break;
  case TypeKind::Array:
    result = Elem && Elem->isRefCounted();
    break;
  case TypeKind::Tuple:
    for (Type *e : Elems)
      if (e->isRefCounted()) { result = true; break; }
    break;
  case TypeKind::Struct:
  case TypeKind::Enum: {
    // Guard against a struct that (indirectly) contains itself: seed the cache
    // with "not counted" before recursing, then refine.
    RefCountedCache = 0;
    if (auto *nd = Nominal) {
      for (const auto &f : nd->Fields)
        if (f->Ty && f->Ty->isRefCounted()) { result = true; break; }
      if (!result && nd->Kind == NodeKind::Enum) {
        const auto *ed = static_cast<const EnumDecl *>(
            static_cast<const Decl *>(nd));
        for (const auto &v : ed->Variants) {
          // Both shapes carry payloads: `Word(String)` keeps its types in
          // TupleTypes, `Rect { .. }` in Fields.
          for (const auto &tt : v->TupleTypes)
            if (tt->Resolved && tt->Resolved->isRefCounted()) { result = true; break; }
          if (!result)
            for (const auto &f : v->Fields)
              if (f->Ty && f->Ty->isRefCounted()) { result = true; break; }
          if (result)
            break;
        }
      }
    }
    break;
  }
  case TypeKind::Function:
    // Function values are closures: {fn pointer, refcounted environment}.
    result = true;
    break;
  default:
    result = false;
    break;
  }
  RefCountedCache = result ? 1 : 0;
  return result;
}

bool Type::containsGenericParam() const {
  switch (K) {
  case TypeKind::Generic:
    return true;
  case TypeKind::Pointer:
  case TypeKind::Array:
  case TypeKind::Slice:
    return Elem && Elem->containsGenericParam();
  case TypeKind::Tuple:
    for (Type *e : Elems)
      if (e->containsGenericParam())
        return true;
    return false;
  case TypeKind::Function:
  case TypeKind::CFunction:
    for (Type *e : Elems)
      if (e->containsGenericParam())
        return true;
    return Elem && Elem->containsGenericParam();
  case TypeKind::Struct:
  case TypeKind::Enum:
  case TypeKind::Class:
  case TypeKind::Mark:
    for (Type *e : Elems)
      if (e->containsGenericParam())
        return true;
    return false;
  default:
    return false;
  }
}

std::string Type::toString() const {
  if (Opaque)
    return "some " + (OpaqueMark ? static_cast<const Decl *>(
                                       reinterpret_cast<const NominalDecl *>(
                                           OpaqueMark))->Name
                                 : std::string("?"));
  switch (K) {
  case TypeKind::Error: return "<error>";
  case TypeKind::Void: return "()";
  case TypeKind::Never: return "Never";
  case TypeKind::Bool: return "bool";
  case TypeKind::Char: return "Character";
  case TypeKind::CString: return "CString";
  case TypeKind::String: return "String";
  case TypeKind::Int:
    return (Signed ? "i" : "u") + std::to_string(Width);
  case TypeKind::Float:
    return "f" + std::to_string(Width);
  case TypeKind::Pointer: {
    std::string s = Raw ? "*" : "&";
    if (Weak) s += "weak ";
    if (Mutable) s += "var ";
    return s + (Elem ? Elem->toString() : "?");
  }
  case TypeKind::Array:
    return "[" + std::to_string(ArraySize) + ":" +
           (Elem ? Elem->toString() : "?") + "]";
  case TypeKind::Slice:
    return "[" + (Elem ? Elem->toString() : "?") + "]";
  case TypeKind::Tuple: {
    std::string s = "(";
    for (size_t i = 0; i < Elems.size(); ++i) {
      if (i) s += ", ";
      s += Elems[i]->toString();
    }
    return s + ")";
  }
  case TypeKind::CFunction:
  case TypeKind::Function: {
    std::string s = K == TypeKind::CFunction ? "@cfunction(" : "@function(";
    for (size_t i = 0; i < Elems.size(); ++i) {
      if (i) s += ", ";
      s += Elems[i]->toString();
    }
    if (Variadic) s += Elems.empty() ? "..." : ", ...";
    s += ")";
    if (Elem && !Elem->isVoid())
      s += " -> " + Elem->toString();
    return s;
  }
  case TypeKind::Struct:
  case TypeKind::Enum:
  case TypeKind::Class:
  case TypeKind::Mark: {
    std::string s = Nominal ? static_cast<const Decl *>(Nominal)->Name : "?";
    if (!Elems.empty()) {
      s += "<";
      for (size_t i = 0; i < Elems.size(); ++i) {
        if (i) s += ", ";
        s += Elems[i]->toString();
      }
      s += ">";
    }
    return Uniq ? "Unique<" + s + ">" : s;
  }
  case TypeKind::DynMark:
    return "dyn " + (Nominal ? static_cast<const Decl *>(Nominal)->Name
                             : std::string("?"));
  case TypeKind::Any:
    return "Any";
  case TypeKind::Generic:
    return Name;
  case TypeKind::Opaque:
    return "some ?";
  }
  return "?";
}

//===----------------------------------------------------------------------===//
// TypeContext
//===----------------------------------------------------------------------===//

TypeContext::TypeContext(unsigned pointerBits) : PointerBits(pointerBits) {
  TyError = make(TypeKind::Error);
  TyVoid = make(TypeKind::Void);
  TyNever = make(TypeKind::Never);
  TyBool = make(TypeKind::Bool);
  TyChar = make(TypeKind::Char);
  TyCString = make(TypeKind::CString);
  TyString = make(TypeKind::String);
  TyAny = make(TypeKind::Any);

  Builtins["Any"] = TyAny;
  Builtins["bool"] = TyBool;
  Builtins["Character"] = TyChar;
  Builtins["CString"] = TyCString;
  Builtins["String"] = TyString;
  Builtins["Void"] = TyVoid;
  Builtins["Never"] = TyNever;

  for (unsigned w : {8u, 16u, 32u, 64u}) {
    Builtins["i" + std::to_string(w)] = intType(w, true);
    Builtins["u" + std::to_string(w)] = intType(w, false);
  }
  // Pointer-sized, so they follow the target: `usize` is `u32` on a 32-bit
  // machine and `u64` on a 64-bit one. Everything the standard library sizes
  // with them — allocations, `mem::offset`, container indices — follows.
  Builtins["isize"] = intType(PointerBits, true);
  Builtins["usize"] = intType(PointerBits, false);
  Builtins["f32"] = floatType(32);
  Builtins["f64"] = floatType(64);

  // Spelling aliases called out in the language description.
  Builtins["int"] = intType(64, true);
  Builtins["uint"] = intType(64, false);
  Builtins["float"] = floatType(32);
  Builtins["double"] = floatType(64);
  Builtins["byte"] = intType(8, false);
  // `Byte` is `u8` under the capitalised spelling the other named builtins
  // use, so a buffer reads as bytes rather than as small numbers.
  Builtins["Byte"] = intType(8, false);
}

Type *TypeContext::intType(unsigned width, bool isSigned) {
  auto key = std::make_pair(width, isSigned);
  auto it = Ints.find(key);
  if (it != Ints.end())
    return it->second;
  Type *t = make(TypeKind::Int);
  t->Width = width;
  t->Signed = isSigned;
  Ints[key] = t;
  return t;
}

Type *TypeContext::floatType(unsigned width) {
  auto it = Floats.find(width);
  if (it != Floats.end())
    return it->second;
  Type *t = make(TypeKind::Float);
  t->Width = width;
  Floats[width] = t;
  return t;
}

std::string TypeContext::signatureOf(const Type *t) {
  if (!t)
    return "!";
  char buf[32];
  snprintf(buf, sizeof(buf), "%p", static_cast<const void *>(t));
  return buf;
}

Type *TypeContext::pointerTo(Type *pointee, bool isMutable, bool isRaw,
                             bool isWeak) {
  std::string key = "ptr:" + signatureOf(pointee) + (isMutable ? "m" : "") +
                    (isRaw ? "r" : "") + (isWeak ? "w" : "");
  auto it = Interned.find(key);
  if (it != Interned.end())
    return it->second;
  Type *t = make(TypeKind::Pointer);
  t->Elem = pointee;
  t->Mutable = isMutable;
  t->Raw = isRaw;
  t->Weak = isWeak;
  Interned[key] = t;
  return t;
}

Type *TypeContext::arrayOf(Type *element, uint64_t size) {
  std::string key = "arr:" + signatureOf(element) + ":" + std::to_string(size);
  auto it = Interned.find(key);
  if (it != Interned.end())
    return it->second;
  Type *t = make(TypeKind::Array);
  t->Elem = element;
  t->ArraySize = size;
  Interned[key] = t;
  return t;
}

Type *TypeContext::sliceOf(Type *element) {
  std::string key = "slice:" + signatureOf(element);
  auto it = Interned.find(key);
  if (it != Interned.end())
    return it->second;
  Type *t = make(TypeKind::Slice);
  t->Elem = element;
  Interned[key] = t;
  return t;
}

Type *TypeContext::tupleOf(std::vector<Type *> elements) {
  if (elements.empty())
    return TyVoid;
  std::string key = "tup:";
  for (Type *e : elements)
    key += signatureOf(e) + ",";
  auto it = Interned.find(key);
  if (it != Interned.end())
    return it->second;
  Type *t = make(TypeKind::Tuple);
  t->Elems = std::move(elements);
  Interned[key] = t;
  return t;
}

Type *TypeContext::functionOf(std::vector<Type *> params, Type *result,
                              bool variadic) {
  std::string key = "fn:" + signatureOf(result) + ":";
  for (Type *p : params)
    key += signatureOf(p) + ",";
  if (variadic)
    key += "...";
  auto it = Interned.find(key);
  if (it != Interned.end())
    return it->second;
  Type *t = make(TypeKind::Function);
  t->Elems = std::move(params);
  t->Elem = result;
  t->Variadic = variadic;
  Interned[key] = t;
  return t;
}

Type *TypeContext::cfunctionOf(std::vector<Type *> params, Type *result,
                               bool variadic) {
  std::string key = "cfn:" + signatureOf(result) + ":";
  for (Type *p : params)
    key += signatureOf(p) + ",";
  if (variadic)
    key += "...";
  auto it = Interned.find(key);
  if (it != Interned.end())
    return it->second;
  Type *t = make(TypeKind::CFunction);
  t->Elems = std::move(params);
  t->Elem = result;
  t->Variadic = variadic;
  Interned[key] = t;
  return t;
}

Type *TypeContext::nominalOf(NominalDecl *decl, std::vector<Type *> typeArgs) {
  if (!decl)
    return TyError;
  TypeKind k;
  switch (static_cast<Decl *>(decl)->Kind) {
  case NodeKind::Struct: k = TypeKind::Struct; break;
  case NodeKind::Enum: k = TypeKind::Enum; break;
  case NodeKind::Class: k = TypeKind::Class; break;
  case NodeKind::Mark: k = TypeKind::Mark; break;
  default: return TyError;
  }
  char buf[32];
  snprintf(buf, sizeof(buf), "%p", static_cast<void *>(decl));
  std::string key = std::string("nom:") + buf + ":";
  for (Type *a : typeArgs)
    key += signatureOf(a) + ",";
  auto it = Interned.find(key);
  if (it != Interned.end())
    return it->second;
  Type *t = make(k);
  t->Nominal = decl;
  t->Elems = std::move(typeArgs);
  Interned[key] = t;
  return t;
}

Type *TypeContext::uniqOf(Type *classType) {
  if (!classType || !classType->is(TypeKind::Class))
    return classType ? classType : TyError;
  if (classType->Uniq)
    return classType;
  char buf[32];
  snprintf(buf, sizeof(buf), "%p", static_cast<const void *>(classType));
  std::string key = std::string("uniq:") + buf;
  auto it = Interned.find(key);
  if (it != Interned.end())
    return it->second;
  Type *t = make(TypeKind::Class);
  t->Nominal = classType->Nominal;
  t->Elems = classType->Elems;
  t->Elem = classType;          // the plain form, for stripUniq
  t->Uniq = true;
  Interned[key] = t;
  return t;
}

Type *TypeContext::stripUniq(Type *t) {
  if (!t || !t->Uniq)
    return t;
  // The plain form was stashed in Elem when the uniq type was interned.
  return t->Elem ? t->Elem : t;
}

Type *TypeContext::dynMarkOf(MarkDecl *decl) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%p", static_cast<void *>(decl));
  std::string key = std::string("dyn:") + buf;
  auto it = Interned.find(key);
  if (it != Interned.end())
    return it->second;
  Type *t = make(TypeKind::DynMark);
  t->Nominal = reinterpret_cast<NominalDecl *>(decl);
  Interned[key] = t;
  return t;
}

Type *TypeContext::genericParam(const std::string &name, unsigned index) {
  std::string key = "gen:" + name;
  auto it = Interned.find(key);
  if (it != Interned.end())
    return it->second;
  Type *t = make(TypeKind::Generic);
  t->Name = name;
  t->Width = index;
  Interned[key] = t;
  return t;
}

Type *TypeContext::opaqueOf(FunctionDecl *owner, MarkDecl *mark) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%p", static_cast<void *>(owner));
  std::string key = std::string("some:") + buf;
  auto it = Interned.find(key);
  if (it != Interned.end())
    return it->second;
  Type *t = make(TypeKind::Opaque);
  t->Opaque = true;
  t->OpaqueOwner = owner;
  t->OpaqueMark = mark;
  Interned[key] = t;
  return t;
}

void TypeContext::resolveOpaque(Type *opaque, Type *under) {
  if (!opaque || !opaque->Opaque || !under)
    return;
  // Take on the concrete type's whole representation, keeping only the
  // veil: the flag, the owner and the mark. Nothing that asks about kind,
  // width, elements, or reference counting can tell the two apart from
  // here — only identity, which is the point.
  FunctionDecl *owner = opaque->OpaqueOwner;
  MarkDecl *mark = opaque->OpaqueMark;
  opaque->K = under->K;
  opaque->Signed = under->Signed;
  opaque->Mutable = under->Mutable;
  opaque->Raw = under->Raw;
  opaque->Weak = under->Weak;
  opaque->Uniq = under->Uniq;
  opaque->Variadic = under->Variadic;
  opaque->Width = under->Width;
  opaque->ArraySize = under->ArraySize;
  opaque->Elem = under->Elem;
  opaque->Elems = under->Elems;
  opaque->Nominal = under->Nominal;
  opaque->Name = under->Name;
  opaque->RefCountedCache = -1;
  opaque->Opaque = true;
  opaque->OpaqueOwner = owner;
  opaque->OpaqueMark = mark;
  opaque->OpaqueUnder = under;
}

Type *TypeContext::builtinNamed(const std::string &name) {
  auto it = Builtins.find(name);
  return it == Builtins.end() ? nullptr : it->second;
}

Type *TypeContext::substitute(Type *t, const std::map<std::string, Type *> &map) {
  if (!t || map.empty() || !t->containsGenericParam())
    return t;
  // A `some` is already the one belonging to a particular function, or a
  // particular instantiation of one; there is nothing in it to substitute.
  if (t->isOpaque())
    return t;
  switch (t->kind()) {
  case TypeKind::Generic: {
    auto it = map.find(t->genericName());
    return it == map.end() ? t : it->second;
  }
  case TypeKind::Pointer:
    return pointerTo(substitute(t->pointee(), map), t->isMutablePointer(),
                     t->isRawPointer(), t->isWeakPointer());
  case TypeKind::Array:
    return arrayOf(substitute(t->element(), map), t->arraySize());
  case TypeKind::Slice:
    return sliceOf(substitute(t->element(), map));
  case TypeKind::Tuple: {
    std::vector<Type *> elems;
    for (Type *e : t->tupleElements())
      elems.push_back(substitute(e, map));
    return tupleOf(std::move(elems));
  }
  case TypeKind::Function:
  case TypeKind::CFunction: {
    std::vector<Type *> ps;
    for (Type *p : t->params())
      ps.push_back(substitute(p, map));
    return t->is(TypeKind::CFunction)
               ? cfunctionOf(std::move(ps), substitute(t->result(), map),
                             t->isVariadicFunction())
               : functionOf(std::move(ps), substitute(t->result(), map),
                            t->isVariadicFunction());
  }
  case TypeKind::Struct:
  case TypeKind::Enum:
  case TypeKind::Class:
  case TypeKind::Mark: {
    std::vector<Type *> args;
    for (Type *a : t->typeArguments())
      args.push_back(substitute(a, map));
    return nominalOf(t->nominal(), std::move(args));
  }
  default:
    return t;
  }
}

bool TypeContext::unify(Type *pattern, Type *concrete,
                        std::map<std::string, Type *> &out) {
  if (!pattern || !concrete)
    return false;
  if (pattern == concrete)
    return true;

  if (pattern->isGeneric()) {
    auto it = out.find(pattern->genericName());
    if (it == out.end()) {
      out[pattern->genericName()] = concrete;
      return true;
    }
    return it->second == concrete;
  }

  // An array argument satisfies a slice parameter, exactly as it does for a
  // non-generic function, so `fn total<T>(v: [T])` accepts `[4:i64]`.
  if (pattern->is(TypeKind::Slice) && concrete->is(TypeKind::Array))
    return unify(pattern->element(), concrete->element(), out);

  // A plain `fn` used as a value types as a closure until something wants a
  // bare pointer. Inference only has to line the shapes up; whether the
  // conversion is actually allowed is settled afterwards, against the
  // parameter's concrete type.
  const bool functionShapes =
      (pattern->is(TypeKind::CFunction) && concrete->is(TypeKind::Function)) ||
      (pattern->is(TypeKind::Function) && concrete->is(TypeKind::CFunction));
  if (!functionShapes && pattern->kind() != concrete->kind())
    return false;

  switch (pattern->kind()) {
  case TypeKind::Pointer:
    return pattern->isRawPointer() == concrete->isRawPointer() &&
           unify(pattern->pointee(), concrete->pointee(), out);
  case TypeKind::Slice:
    return unify(pattern->element(), concrete->element(), out);
  case TypeKind::Array:
    return pattern->arraySize() == concrete->arraySize() &&
           unify(pattern->element(), concrete->element(), out);
  case TypeKind::Tuple: {
    if (pattern->tupleElements().size() != concrete->tupleElements().size())
      return false;
    for (size_t i = 0; i < pattern->tupleElements().size(); ++i)
      if (!unify(pattern->tupleElements()[i], concrete->tupleElements()[i], out))
        return false;
    return true;
  }
  case TypeKind::CFunction:
  case TypeKind::Function: {
    if (pattern->params().size() != concrete->params().size())
      return false;
    for (size_t i = 0; i < pattern->params().size(); ++i)
      if (!unify(pattern->params()[i], concrete->params()[i], out))
        return false;
    return unify(pattern->result(), concrete->result(), out);
  }
  case TypeKind::Struct:
  case TypeKind::Enum:
  case TypeKind::Class:
  case TypeKind::Mark: {
    // A signature says `Holder<A, B>`, which is the template; the argument is
    // `Holder<i64, String>`, which is an instantiation cloned from it. They
    // are different declarations, and they have to match.
    NominalDecl *pn = pattern->nominal();
    NominalDecl *cn = concrete->nominal();
    if (pn != cn) {
      NominalDecl *pRoot = pn && pn->GenericTemplate ? pn->GenericTemplate : pn;
      NominalDecl *cRoot = cn && cn->GenericTemplate ? cn->GenericTemplate : cn;
      if (!pRoot || pRoot != cRoot)
        return false;
    }
    if (pattern->typeArguments().size() != concrete->typeArguments().size())
      return false;
    for (size_t i = 0; i < pattern->typeArguments().size(); ++i)
      if (!unify(pattern->typeArguments()[i], concrete->typeArguments()[i], out))
        return false;
    return true;
  }
  default:
    return false;
  }
}

//===----------------------------------------------------------------------===//
// Standard library types the compiler knows by name
//===----------------------------------------------------------------------===//

static LangItem langItemOf(const Type *t) {
  if (!t || t->kind() != TypeKind::Enum || !t->nominal())
    return LangItem::NotSpecial;
  return t->nominal()->Lang;
}

bool isOptionType(const Type *t) { return langItemOf(t) == LangItem::Option; }
bool isResultType(const Type *t) { return langItemOf(t) == LangItem::Result; }

Type *optionPayload(const Type *t) {
  if (!isOptionType(t) || t->typeArguments().empty())
    return nullptr;
  return t->typeArguments()[0];
}

Type *resultValue(const Type *t) {
  if (!isResultType(t) || t->typeArguments().empty())
    return nullptr;
  return t->typeArguments()[0];
}

Type *resultError(const Type *t) {
  if (!isResultType(t) || t->typeArguments().size() < 2)
    return nullptr;
  return t->typeArguments()[1];
}

int variantIndexNamed(const Type *t, const char *name) {
  if (!t || t->kind() != TypeKind::Enum || !t->nominal())
    return -1;
  const auto *e = static_cast<const EnumDecl *>(
      static_cast<const Decl *>(t->nominal()));
  for (const auto &v : e->Variants)
    if (v->Name == name)
      return static_cast<int>(v->Index);
  return -1;
}

//===----------------------------------------------------------------------===//
// Conversions
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// `Any`
//===----------------------------------------------------------------------===//

bool isStorableInAny(const Type *t) {
  if (!t)
    return false;
  switch (t->kind()) {
  case TypeKind::Error:
  case TypeKind::Void:
  case TypeKind::Never:
  case TypeKind::Generic:
  case TypeKind::Opaque:
    return false;
  default:
    // A `uniq T` is the one owner of its object; boxing it would mint a second
    // reference and silently break that. Sema reports it rather than lying.
    return !t->isUniq();
  }
}

std::string runtimeTypeName(const Type *t) {
  if (!t)
    return "?";
  auto qualified = [](const NominalDecl *nd) -> std::string {
    if (!nd)
      return "?";
    const auto *d = static_cast<const Decl *>(nd);
    return d->ModulePath.empty() ? d->Name : d->ModulePath + "::" + d->Name;
  };
  switch (t->kind()) {
  case TypeKind::Pointer: {
    std::string s = t->isRawPointer() ? "*" : "&";
    if (t->isWeakPointer()) s += "weak ";
    if (t->isMutablePointer()) s += "var ";
    return s + runtimeTypeName(t->pointee());
  }
  case TypeKind::Array:
    return "[" + std::to_string(t->arraySize()) + ":" +
           runtimeTypeName(t->element()) + "]";
  case TypeKind::Slice:
    return "[" + runtimeTypeName(t->element()) + "]";
  case TypeKind::Tuple: {
    std::string s = "(";
    for (size_t i = 0; i < t->tupleElements().size(); ++i) {
      if (i) s += ", ";
      s += runtimeTypeName(t->tupleElements()[i]);
    }
    return s + ")";
  }
  case TypeKind::CFunction:
  case TypeKind::Function: {
    std::string s =
        t->is(TypeKind::CFunction) ? "@cfunction(" : "@function(";
    for (size_t i = 0; i < t->params().size(); ++i) {
      if (i) s += ", ";
      s += runtimeTypeName(t->params()[i]);
    }
    if (t->isVariadicFunction()) s += t->params().empty() ? "..." : ", ...";
    s += ")";
    if (t->result() && !t->result()->isVoid())
      s += " -> " + runtimeTypeName(t->result());
    return s;
  }
  case TypeKind::Struct:
  case TypeKind::Enum:
  case TypeKind::Class:
  case TypeKind::Mark: {
    std::string s = qualified(t->nominal());
    if (!t->typeArguments().empty()) {
      s += "<";
      for (size_t i = 0; i < t->typeArguments().size(); ++i) {
        if (i) s += ", ";
        s += runtimeTypeName(t->typeArguments()[i]);
      }
      s += ">";
    }
    return s;
  }
  case TypeKind::DynMark:
    return "dyn " + qualified(t->nominal());
  default:
    // Primitives already print unambiguously, and there is only one of each.
    return t->toString();
  }
}

bool isImplicitlyConvertible(Type *from, Type *to) {
  if (!from || !to)
    return false;
  if (from == to)
    return true;
  if (from->isError() || to->isError())
    return true; // already reported; do not pile on
  // `!` (from `return`, `break`, a panic) satisfies every expectation.
  if (from->isNever())
    return true;

  // A `some Mark` converts to nothing but itself — not to the type behind
  // it, and not along any of that type's own conversions, which would give
  // the veil away. Wrapping it is different: an `Option` of it, a `Result`
  // of it, a `dyn Mark` or an `Any` holding it all keep it whole.
  if (to->isOpaque())
    return false;
  if (from->isOpaque()) {
    if (isOptionType(to) && isImplicitlyConvertible(from, optionPayload(to)))
      return true;
    if (isResultType(to) &&
        (isImplicitlyConvertible(from, resultValue(to)) ||
         isImplicitlyConvertible(from, resultError(to))))
      return true;
    if (to->is(TypeKind::DynMark))
      return true;
    if (to->isAny() && isStorableInAny(from))
      return true;
    return false;
  }

  // `uniq T` reads as a plain `T` wherever the value is only being looked
  // through — a method receiver, a field access. Sema separately refuses to
  // *store* one without a `move`, which is what keeps the owner unique.
  if (from->isUniq() && !to->isUniq() &&
      isImplicitlyConvertible(TypeContext::stripUniq(from), to))
    return true;
  // A plain `T` becomes a `uniq T` only where Sema has checked the value is
  // freshly constructed or moved; the conversion itself is a no-op.
  if (to->isUniq() && !from->isUniq() &&
      isImplicitlyConvertible(from, TypeContext::stripUniq(to)))
    return true;

  // T -> Option<T>
  if (isOptionType(to) && !isOptionType(from) &&
      isImplicitlyConvertible(from, optionPayload(to)))
    return true;

  // T -> Result<T, E> and E -> Result<T, E>. A function that returns a Result
  // says which channel a value belongs to by its type, so the value can be
  // written on its own. When both sides would accept it, the success channel
  // wins — that is the one the return type leads with.
  if (isResultType(to) && !isResultType(from)) {
    if (isImplicitlyConvertible(from, resultValue(to)))
      return true;
    if (isImplicitlyConvertible(from, resultError(to)))
      return true;
  }

  // Integer widening that cannot lose information.
  if (from->isInt() && to->isInt()) {
    if (from->isSigned() == to->isSigned())
      return from->intWidth() <= to->intWidth();
    // unsigned -> signed is safe when the signed type is strictly wider
    if (!from->isSigned() && to->isSigned())
      return from->intWidth() < to->intWidth();
    return false;
  }
  // f32 -> f64
  if (from->isFloat() && to->isFloat())
    return from->floatWidth() <= to->floatWidth();

  // &var T -> &T
  if (from->is(TypeKind::Pointer) && to->is(TypeKind::Pointer))
    return from->pointee() == to->pointee() &&
           from->isRawPointer() == to->isRawPointer() &&
           (from->isMutablePointer() || !to->isMutablePointer());

  // Tuples convert element by element, so `(1, "x")` is a `(i64, Any)` when
  // that is what the context asked for.
  if (from->is(TypeKind::Tuple) && to->is(TypeKind::Tuple)) {
    const auto &f = from->tupleElements();
    const auto &t = to->tupleElements();
    if (f.size() != t.size())
      return false;
    for (size_t i = 0; i < f.size(); ++i)
      if (!isImplicitlyConvertible(f[i], t[i]))
        return false;
    return true;
  }

  // [N:T] -> [T]
  if (from->is(TypeKind::Array) && to->is(TypeKind::Slice))
    return from->element() == to->element();
  if (from->is(TypeKind::Pointer) && to->is(TypeKind::Slice))
    return from->pointee() && from->pointee()->is(TypeKind::Array) &&
           from->pointee()->element() == to->element();

  // A subclass is usable wherever its base class is.
  if (from->is(TypeKind::Class) && to->is(TypeKind::Class)) {
    auto *fc = reinterpret_cast<ClassDecl *>(from->nominal());
    auto *tc = reinterpret_cast<ClassDecl *>(to->nominal());
    while (fc) {
      if (fc == tc)
        return true;
      fc = fc->Super;
    }
    return false;
  }
  // Any conforming type may be boxed into `dyn Mark`; Sema verifies the bind.
  if (to->is(TypeKind::DynMark))
    return true;

  // Anything with a run-time representation may be stored in an `Any`. The
  // reverse never converts on its own: coming back out is a question with a
  // wrong answer, so it is asked through `get` and answered with an Option.
  if (to->isAny() && isStorableInAny(from))
    return true;

  return false;
}

bool isExplicitlyCastable(Type *from, Type *to) {
  if (!from || !to)
    return false;
  if (isImplicitlyConvertible(from, to))
    return true;
  // `as` knows nothing about what is behind a `some`, and must not.
  if (from->isOpaque() || to->isOpaque())
    return false;
  if (from->isNumeric() && to->isNumeric())
    return true;
  if (from->isInt() && (to->is(TypeKind::Char) || to->isBool()))
    return true;
  if ((from->is(TypeKind::Char) || from->isBool()) && to->isInt())
    return true;
  // Enums with no payload are integers underneath.
  if (from->is(TypeKind::Enum) && to->isInt())
    return true;
  if (from->isInt() && to->is(TypeKind::Enum))
    return true;
  // Pointer punning, only legal in an unsafe context (checked by Sema).
  // A `@cfunction` is a bare address, so it puns with any other pointer.
  bool fromPtr = from->is(TypeKind::Pointer) || from->is(TypeKind::CString) ||
                 from->is(TypeKind::Class) || from->is(TypeKind::CFunction);
  bool toPtr = to->is(TypeKind::Pointer) || to->is(TypeKind::CString) ||
               to->is(TypeKind::Class) || to->is(TypeKind::CFunction);
  if (fromPtr && toPtr)
    return true;
  if (fromPtr && to->isInt())
    return true;
  if (from->isInt() && toPtr)
    return true;
  // String <-> CString goes through the runtime.
  if (from->is(TypeKind::String) && to->is(TypeKind::CString))
    return true;
  if (from->is(TypeKind::CString) && to->is(TypeKind::String))
    return true;
  return false;
}

} // namespace rune
