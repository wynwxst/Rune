# C++'s own scalar names

`long` is 64 bits on Linux and 32 on Windows, and `int64_t` is `long` on one and `long long` on the other — a distinction Rune's `i64` cannot make, and one the symbol depends on. So the C++ spellings exist as names of their own. Each is the Rune type it is on the target being built for, and in an `extern "C++"` signature it also fixes how the parameter mangles.

| Written | C++ | Is, on a 64-bit Linux target |
| --- | --- | --- |
| `c_char` | `char` | `i8` — `u8` where `char` is unsigned |
| `c_schar` / `c_uchar` | `signed char` / `unsigned char` | `i8` / `u8` |
| `c_short` / `c_ushort` | `short` / `unsigned short` | `i16` / `u16` |
| `c_int` / `c_uint` | `int` / `unsigned` | `i32` / `u32` |
| `c_long` / `c_ulong` | `long` / `unsigned long` | `i64` / `u64` |
| `c_longlong` / `c_ulonglong` | `long long` / `unsigned long long` | `i64` / `u64` |
| `c_float` / `c_double` | `float` / `double` | `f32` / `f64` |
| `c_bool` | `bool` | `bool` |
| `c_size_t` / `c_ssize_t` | `size_t` / `ssize_t` | `u64` / `i64` |
| `c_ptrdiff_t` | `ptrdiff_t` | `i64` |
| `c_intptr_t` / `c_uintptr_t` | `intptr_t` / `uintptr_t` | `i64` / `u64` |
| `c_int8_t` … `c_int64_t` | `int8_t` … `int64_t` | `i8` … `i64` |
| `c_uint8_t` … `c_uint64_t` | `uint8_t` … `uint64_t` | `u8` … `u64` |
| `c_wchar_t` | `wchar_t` | `i32` — `u16` on Windows |
| `c_void` | `void` | `u8`; only useful behind a pointer |

*Rune's own `i8`…`i64`, `u8`…`u64`, `f32`, `f64` and `bool` are accepted too, and mangle as the target's `intN_t` family — which is what a header that uses those means.*
