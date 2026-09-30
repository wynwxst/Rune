# What crosses

| Rune | C++ | Notes |
| --- | --- | --- |
| `i8` … `i64`, `u8` … `u64` | `int8_t` … `uint64_t` | or the `c_` names above |
| `f32` / `f64` | `float` / `double` |  |
| `bool` | `bool` |  |
| `CString` | `const char *` | borrowed, NUL terminated |
| `*T` / `*var T` | `const T *` / `T *` | unchecked |
| `&T` / `&var T` | `const T &` / `T &` | a reference **is** a pointer |
| `struct` in the block | the same struct | **by value**, by the target's rules |
| `struct<T>` in the block | a class template | one symbol per instantiation |
| `enum` in the block | the same enum | an `int` |
| `class` in the block | the class | only ever behind a pointer |
| `@cfunction(A) -> B` | `B (*)(A)` | a bare function pointer |
| `String` | — | not a C++ type; use `.$cstr()` |
| `@function(A) -> B` | — | a closure; use `@cfunction` |
| a Rune `struct` or `class` | — | declare the C++ one in the block |
