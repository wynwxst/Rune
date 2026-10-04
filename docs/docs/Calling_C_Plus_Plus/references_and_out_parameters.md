# References and out-parameters

A C++ reference is a pointer that is not written with a star. `&T` is `const T &` and `&var T` is `T &`, and a raw `*T` or `*var T` is accepted where one is wanted — it is the same address either way.

**Reference parameters, kept inside a wrapper**

```text
extern "C++" {
    namespace shim {
        struct Pair { a: c_int, b: c_int }
        fn sumRef(p: &Pair) -> c_int      // int sumRef(const Pair &)
        fn bump(x: &var c_int, by: c_int) // void bump(int &, int)
    }
}

/// The borrows live for the call, which is all a reference needs.
#safe("both borrows name locals that outlive the call")
pub fn bumped(start: i64, by: i64) -> i64 {
    var x = start as c_int
    bump(&var x, by as c_int)
    x as i64
}
```
