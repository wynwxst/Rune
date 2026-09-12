# Adding an attribute

`@something` on a declaration. The parser accepts any `@name(args...)` and
hangs it on the declaration as an `Attribute`; what makes one *mean* anything
is a phase that looks for it.

## 1. Register the name

`Sema::isBuiltinDecorator` in `Sema.cpp` holds the set the compiler provides:

```cpp
static const std::set<std::string> kBuiltin = {
    "unsafe", "safe",  "inline", "noinline", "export",
    "alias",  "intrinsic", "link", "linkpath", "type",
    "Doc",    "doc",   "sync",   "as",     "resource",
};
```

Anything not in that set is looked up as a **user-defined decorator** — a
function whose last parameter is a function, called once before `main`. So an
unregistered `@name` does not give you "unknown attribute"; it gives you "no
decorator named 'name'", which is a confusing way to find out you forgot this
step.

## 2. Read it wherever it means something

```cpp
if (fn->hasAttr("inline"))
  f->addFnAttr(llvm::Attribute::AlwaysInline);

const Attribute *a = fn->findAttr("export");
if (a && !a->Args.empty())
  if (const auto *s = dyn_cast<StringLitExpr>(a->Args[0].get()))
    return s->Value;
```

`hasAttr` for a flag, `findAttr` for one with arguments. Arguments are parsed
expressions, so a string argument arrives as a `StringLitExpr` and has to be
cast.

Where to read it depends on what it affects:

| Affects | Read it in |
| --- | --- |
| Linkage or a function attribute | `CodeGen::declareFunction` |
| How a call is emitted | `CodeGenExpr.cpp` |
| Whether something is legal | `Sema.cpp`, near the other declaration checks |
| Documentation | `Sema::collectDoc`, and the `--emit-docs` writer |

## 3. Reject it where it does not apply

An attribute silently ignored in the wrong place is worse than one that errors.
`@as` is the model:

```cpp
if (a.Name == "as" && !fn->IsExtern) {
  Diags.error(a.Range, "`@as` only applies inside an `extern` block")
      .note("it renames a foreign declaration for Rune's side, leaving the "
            "symbol the C library exports alone")
      .note("to give a Rune declaration a second name, use `@alias`")
      .code(234);
  continue;
}
```

Note the second note: it says what to use *instead*. That is the house style —
a diagnostic that only says no is half-written.

## File-level directives are different

`@link`, `@linkpath` and `@type` are not attributes on declarations; they sit
at the top of a file and are handled by `Parser::parseFileDirectives`, which
recognises exactly those three names. Adding a fourth means editing that
function — including its ordering rule, since `@type` must precede the others.
