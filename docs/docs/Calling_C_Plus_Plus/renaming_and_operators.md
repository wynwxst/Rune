# Renaming, and operators

`#as` renames a declaration for Rune's side only, exactly as it does in an `extern "C"` block — which is also how two overloads of one C++ name are told apart, since Rune has one name per declaration. `#operator` says which C++ operator a member is.

**`#as` and `#operator`**

```rune
extern "C++" {
    namespace llvm {
        class Type {}
        class FunctionType : Type {
            // One of the overloads of `FunctionType::get`, under a Rune name
            // of its own.
            #as("functionType")
            fn get(result: *var Type, isVarArg: bool) -> *var FunctionType
        }
        #size(8)
        class Counter {
            #operator("[]")
            fn at(&self, i: c_int) -> c_int
            #operator("new")
            fn allocate(n: c_size_t, tag: c_int) -> *var c_void
        }
    }
}
```

`#operator` takes the operator as C++ writes it after the keyword: `"new"`, `"delete"`, `"[]"`, `"()"`, `"+"`, `"=="`, `"<=>"` and the rest.
