# Calling C++

An `extern "C++"` block declares what a C++ library exports. The compiler then does what a C++ compiler does at the call: spells the symbol the way the Itanium ABI spells it, and passes each argument the way that target's C++ ABI passes it. No `extern "C"` shim, no generated bindings.

The difference from `extern "C"` is not the syntax but what the compiler has to know. A C symbol is its own name; a C++ symbol folds in the namespace, the class, the const-ness of the member and every parameter type. A C struct crosses by pointer because the ABIs agree about pointers; a C++ struct crosses **by value** here, because the compiler knows how this target passes that particular struct — in two registers, packed into one, or through memory with the result written back through a hidden pointer.

**A namespace, a struct and two functions**

```rune
extern "C++" {
    namespace geometry {
        struct Vec2 { x: f64, y: f64 }

        fn lengthSq(v: Vec2) -> f64
        fn scaled(v: Vec2, k: f64) -> Vec2
    }
}
```

`namespace` nests as it does in C++ and affects the symbols only: the names it holds arrive in the module that wrote the block, so `lengthSq` above is called `lengthSq`, not `geometry::lengthSq`.

> [!WARNING]
> **Which C++ ABI**
>
> The mangling this compiler speaks is the Itanium C++ ABI, which is what Clang and GCC use on Linux, macOS, the BSDs and MinGW. A `-windows-msvc` target uses a different scheme, and an `extern "C++"` block for one is refused rather than mis-mangled.

## Pages

- [C++'s own scalar names](c_plus_plus_s_own_scalar_names.md)
- [What crosses](what_crosses.md)
- [Structs, by value](structs_by_value.md)
- [References and out-parameters](references_and_out_parameters.md)
- [Classes](classes.md)
- [Making one: `std::cxx`](making_one_std_cxx.md)
- [Templates](templates.md)
- [Enums and variables](enums_and_variables.md)
- [Renaming, and operators](renaming_and_operators.md)
- [Linking](linking.md)
- [What does not cross](what_does_not_cross.md)
