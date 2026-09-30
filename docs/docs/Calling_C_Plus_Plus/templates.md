# Templates

A generic `struct` in the block is a class template. Each instantiation is a different C++ type and gets the symbol that type gives it, so one declaration serves every element type the library was compiled for.

**A span, twice over**

```text
extern "C++" {
    namespace shim {
        struct Span<T> { data: *T, len: c_ulong }

        fn total(s: Span<c_long>) -> c_long     // shim::total(shim::Span<long>)
        fn totalD(s: Span<f64>) -> f64          // shim::totalD(shim::Span<double>)
    }
}

/// A slice's two halves are exactly what a span holds.
@safe("the pointer and the count come from one slice, so the extent is right")
pub fn totalOf(values: [c_long]) -> i64 {
    if values.$isEmpty() { return 0 }
    total(Span<c_long> { data: &values[0] as *c_long,
                         len: values.$length() as c_ulong }) as i64
}
```
