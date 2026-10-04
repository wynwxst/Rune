# Pointers and out-parameters

A C function has one result, so a second comes back through a pointer the caller supplies. `&var x as *var T` turns a Rune variable's address into one; the cast is the step out of the checked world, which is what needs `unsafe`.

**Results written through pointers**

```rune
import std::io

extern "C" {
    // frexp splits a double into a fraction and an exponent. The fraction is
    // returned; the exponent is written through the pointer.
    fn frexp(value: f64, exponent: *var i32) -> f64
    fn strtol(text: CString, end: *var u64, base: i32) -> i64
}

#safe("frexp is total over finite doubles")
fn main() -> i64 {
    var exponent: i32 = 0
    let fraction = frexp(12.0, &var exponent as *var i32)
    io::println("12.0 = " + fraction.$str() + " * 2^" + exponent.$str())

    var rest: u64 = 0
    let parsed = unsafe { strtol("2026 and more", &var rest as *var u64, 10) }
    io::println("parsed " + parsed.$str())
    0
}
```

An array or slice hands C the two halves it expects — a pointer to the first element and a count: `&values[0] as *i64` with `values.$length()`.
