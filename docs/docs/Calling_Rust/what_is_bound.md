# What is bound

The crate's source is read for what it exports over the C ABI, and written as a Rune module named after the dependency — in `target/<profile>/cargo/<name>.rune`, worth reading once. Its `///` docs come across with it.

| Rust | Rune |
| --- | --- |
| `#[no_mangle] pub extern "C" fn` | a function; `#[unsafe(no_mangle)]` and `#[export_name = "..."]` too |
| `#[repr(C)] struct` | a `#Convention("C")` struct, fields and order kept |
| `#[repr(u8)]` … `#[repr(C)]` fieldless `enum` | a type of that width, and a global `Name_Variant` per variant |
| `pub const` with a literal value | a `pub global` |
| `struct Name { _private: [u8; 0] }`, or a type only ever behind a pointer | `pub type Name = u8`: an opaque handle |
| `i8`…`u64`, `isize`, `usize`, `f32`, `f64`, `bool` | the same |
| `c_int`, `c_long`, `size_t` and the rest | their width on the target |
| `*const T`, `*mut T`, `&T`, `&mut T`, `NonNull<T>`, `Box<T>`, `Option<&T>` | `*T` or `*var T` |
| `*const c_char`, `*mut c_char` | `CString` |
| `*mut c_void` | `*var u8` |
| `extern "C" fn(A) -> R`, and its `Option` | `@cfunction(A) -> R` |
| `-> !` | `-> Never` |

A function the crate marks safe and that takes no address — no pointer, no reference, no C string — is safe in Rune too, and is called like one of Rune's own. Everything else is declared as a C function is, and called from `unsafe`: the Rust author's `# Safety` section, carried over as its doc comment, says what the caller has to promise.

**src/main.rune**

```text
import std::io
import geometry

fn main() -> i64 {
    io::println(geometry::add(40, 2).$str())             // safe: plain call
    let p = geometry::Point { x: 3.0, y: 4.0 }
    io::println(geometry::distance(p, p).$str())
    let raw = geometry::greeting(2)                       // a string Rust made
    io::println(raw.$str())
    unsafe { geometry::free_text(raw) }                   // given back to Rust
    0
}
```

What cannot be said in Rune is left out, with the reason at the end of the generated file and a note in the build's output: a generic, an `enum` with data, a `union`, a tuple struct, `String`, `Vec` or a slice by value, a Rust-ABI `fn` pointer, a method taking `self`. The answer is the usual one for a C boundary — a pointer and a length, or a small `extern "C"` wrapper in the crate.
