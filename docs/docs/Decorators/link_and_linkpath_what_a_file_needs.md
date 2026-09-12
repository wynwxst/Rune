# `@link` and `@linkpath`: what a file needs

These two belong to the *file*, not to any declaration in it, so they go at the very top — before the imports. A file that calls into a native library says so beside the `extern` block that declares it, instead of leaving the caller to pass `-l` and hope.

**A file that links libm**

```rune
@link("m")
@linkpath("/usr/lib")

import std::io

extern "C" {
    fn cbrt(x: f64) -> f64
    fn hypot(a: f64, b: f64) -> f64
}

@safe("libm is linked by the directives at the top of this file")
fn main() -> i64 {
    io::println(unsafe { cbrt(27.0) }.$str())
    io::println(unsafe { hypot(3.0, 4.0) }.$str())
    0
}
```

Each takes one or more strings, and both may appear more than once. What they name is merged with anything `-l` and `-L` asked for, and repeated names are passed once.

**Too late to be a file directive**

```rune
import std::io

@link("m")

fn main() -> i64 { 0 }
```
