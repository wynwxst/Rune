# Enums and variables

**An enum and a variable**

```rune
extern "C++" {
    namespace shim {
        enum Colour { Red = 1, Green = 2, Blue = 4 }
        fn brighter(c: Colour) -> Colour
        /// A namespaced variable has a mangled symbol too.
        var liveCounters: c_int
    }
}
```
