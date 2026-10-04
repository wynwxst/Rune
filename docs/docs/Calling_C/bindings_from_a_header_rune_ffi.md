# Bindings from a header: `rune ffi`

Writing an `extern "C"` block by hand is fine for three functions and a chore for three hundred. `rune ffi` writes it: it reads C headers with libclang — so they are preprocessed, parsed and laid out exactly as the C compiler does — and writes Rune bindings, as Rust's bindgen does.

```sh
$ rune ffi /usr/include/zlib.h --link z -o src/zlib.rune
$ rune ffi mylib.h -I include --allowlist-function 'mylib_*' -o src/mylib.rune
```

| C | What `rune ffi` writes |
| --- | --- |
| `struct s { ... }` | a `#Convention("C")` struct, every member defaulted to zero, so `s {}` is a zeroed `s` |
| a union, bit-fields, a packed struct | its bytes, with a getter and setter per member |
| a struct only declared | an empty struct, used behind pointers |
| `typedef`, `enum`, `#define N 42` | `pub type`, a constant per enumerator, `pub global N: i32 = 42` |
| functions and `extern` variables | a `pub extern "C"` block; `const char *` parameters are `CString` |

> [!NOTE]
> **Built on first use**
>
> libclang is not on every machine, so `rune ffi` is not built with the toolchain: the first run builds it into `~/.rune/bin` against the libclang it finds (`RUNE_LIBCLANG_DIR`, then `llvm-config`). `rune doc ffi` is its book.
