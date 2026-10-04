# Classes

A C++ `class` is **opaque**: Rune never holds one by value, copies one, or destroys one, because only C++ knows how. It exists behind a pointer, and its members are reached through that. How the members are written says what they are:

| Written | Is |
| --- | --- |
| `fn init(&var self, ...)` | a constructor |
| `fn deinit(&var self)` | the destructor |
| `fn name(&self) -> T` | a `const` member function |
| `fn name(&var self) -> T` | a non-const member function |
| `fn name(args) -> T` | a `static` member function |
| `#operator("[]") fn at(&self, ...)` | `operator[]` |
| `class D : B` | single, non-virtual inheritance |
| `#size(N)` | `sizeof` on the C++ side; what `cxx::alloc` needs |

**A class, its destructor, and a class derived from it**

```rune
extern "C++" {
    namespace shim {
        struct Pair { a: c_int, b: c_int }

        #size(8)
        class Counter {
            fn init(&var self, start: c_int)
            fn deinit(&var self)
            fn next(&var self) -> c_int
            fn peek(&self) -> c_int
            fn setStep(&var self, step: c_int)
            fn state(&self) -> Pair
            fn make(start: c_int) -> *var Counter
            fn destroy(c: *var Counter)
        }

        /// `this` is one address for both halves, so the base's members are
        /// reached through a pointer to the derived class unchanged.
        #size(8)
        class Stepper : Counter {
            fn init(&var self, start: c_int, step: c_int)
            fn twice(&var self) -> c_int
        }
    }
}
```
