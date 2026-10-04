# `#alias` and `#as`

`#alias` *adds* a name: the declaration answers to both. `#as` *replaces* one, and only inside an `extern` block — the C library goes on exporting the name that was written, and Rune sees the new one. That is what lets a program import C's `bind` and still declare a `bind` of its own.

**Two functions called `abs`**

```rune
import std::io

extern "C" {
    #as("cAbs")
    fn abs(v: i32) -> i32        // links against C's `abs`
}

/// A Rune function that keeps the name C also uses.
fn abs(v: i64) -> i64 { if v < 0 { 0 - v } else { v } }

#safe("abs touches no memory")
fn main() -> i64 {
    io::println(abs(-7))         // this one
    io::println(cAbs(-9i32))     // C's
    0
}
```

**`#as` outside an `extern` block**

```rune
#as("other")
fn ordinary() -> i64 { 1 }

fn main() -> i64 { ordinary() }
```
