# Types

## A list of all existing types
| Category | Written | Notes |
|---|---|---|
| Signed integers | `i8` `i16` `i32` `i64` `isize` | `int` is an alias for `i64` |
| Unsigned integers | `u8` `u16` `u32` `u64` `usize` | `uint` aliases `u64`; `byte` and `Byte` both alias `u8` |
| Floating point | `f32` `f64` | `float` aliases `f32`, `double` aliases `f64` |
| Boolean | `bool` | no implicit conversion to or from integers |
| Character | `Character` | exactly one Unicode scalar |
| Foreign text | `CString` | a borrowed NUL-terminated byte pointer |
| Text | `String` | owned, reference counted, UTF-8 |
| Array | `[5:i64]` | fixed length, a constant expression |
| Slice | `[i64]` | a pointer and a length |
| Tuple | `(i64, String)` | `()` is the unit type |
| Optional | `i64?` | sugar for `Option<i64>` |
| Borrow | `&T` `&var T` | checked, non-owning |
| Raw pointer | `*T` `*var T` | unchecked; every use is unsafe |
| Function | `@function(i64) -> bool` | parameters in the parentheses, result after the arrow; no arrow means `()` |
| Opaque result | `some Show` | a result whose concrete type the function's body decides |
| Nominal | `struct` `enum` `class` `mark` | declared types |
| Mark object | `dyn Show` | a value plus a dispatch table |
| Dynamic | `Any` | one value of any type, asked at run time what it is |
| Never | `Never` | the type of an expression that does not return |

This might not make sense, just yet but it will later on.