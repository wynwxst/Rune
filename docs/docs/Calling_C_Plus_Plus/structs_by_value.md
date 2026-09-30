# Structs, by value

A `struct` written inside the block is a C++ struct: Rune lays it out identically and hands it over the way C++ would. Three shapes that are passed three different ways on one machine, and differently again on the next, are written the same here.

**Three shapes, one spelling**

```rune
extern "C++" {
    namespace shim {
        struct Pair { a: c_int, b: c_int }              // one register
        struct Vec2 { x: f64, y: f64 }                  // two, or an HFA
        struct Wide { a: c_long, b: c_long, c: c_long } // through memory

        fn swapped(p: Pair) -> Pair
        fn scaled(v: Vec2, k: f64) -> Vec2
        fn tripled(w: Wide) -> Wide
    }
}
```

> [!NOTE]
> **Why by value here**
>
> This is the opposite of the advice for C, where a struct crosses by pointer because nothing tells the compiler which convention the other side used. Inside an `extern "C++"` block there is no such doubt: the C++ ABI for the target is what both sides follow.
