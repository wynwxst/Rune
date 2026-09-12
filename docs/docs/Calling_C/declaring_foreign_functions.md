# Declaring foreign functions

**Three from libm**

```rune
@link("m")

import std::io

extern "C" {
    fn fmod(a: f64, b: f64) -> f64
    fn atan2(y: f64, x: f64) -> f64
    fn ldexp(value: f64, exponent: i32) -> f64
}

@safe("libm's contract for these is total over the values passed below")
fn main() -> i64 {
    io::println(unsafe { fmod(10.0, 3.0) }.$str())
    io::println(unsafe { ldexp(1.5, 3) }.$str())
    let quadrant = unsafe { atan2(1.0, 1.0) }
    io::println((quadrant > 0.78 && quadrant < 0.79).$str())
    0
}
```

**Three C functions**

```rune
import std::io

extern "C" {
    fn abs(value: i32) -> i32
    fn strlen(text: CString) -> u64
    // `...` marks a variadic C function.
    printf(content: CString, ...) -> i32
}

fn main() -> i64 {
    io::println(abs(-17).$str())
    io::println(strlen("hello").$str())
    printf("printf reached us: %d\n", 42)
    0
}
```

| Rune | C | Notes |
| --- | --- | --- |
| `i8` … `i64` | `int8_t` … `int64_t` | exact widths |
| `u8` … `u64` | `uint8_t` … `uint64_t` |  |
| `isize` / `usize` | `intptr_t` / `uintptr_t` | pointer width |
| `f32` / `f64` | `float` / `double` |  |
| `bool` | `bool` | one byte |
| `CString` | `const char *` | borrowed, NUL terminated |
| `*T` / `*var T` | `const T *` / `T *` | unchecked |
| `@cfunction(A) -> B` | `B (*)(A)` | a bare function pointer |
| `struct` | same layout | cross it **by pointer**; see below |
| `String` | — | **not** a C type; use `.$cstr()` |
| `@function(A) -> B` | — | a closure; use `@cfunction` |
| `class` | — | never crosses the boundary |
