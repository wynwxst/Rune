# `@unsafe` and `unsafe { }`

`@unsafe` on a function says its body may perform unchecked operations and that calling it is itself unchecked. `unsafe { }` opens the same window for a single block. Both are visible at the call site, which is the point: unsafety is never inherited silently.

**An unsafe function and its window**

```rune
import std::io

@unsafe
fn reinterpret(bits: u64) -> f64 {
    // Only legal because the caller has been told this is unchecked.
    *(&bits as *u64 as *f64)
}

fn main() -> i64 {
    let asFloat = unsafe { reinterpret(4614256656552045848) }
    io::println(asFloat.$str())
    0
}
```

**Unsafety does not leak into safe code**

```rune
@unsafe
fn raw(p: *i64) -> i64 { *p }

fn caller(p: *i64) -> i64 {
    raw(p)          // calling it is itself an unsafe operation
}
```
