# `#link` and `#linkpath`: what a file needs

These two belong to the *file*, not to any declaration in it, so they go at the very top — before the imports. A file that calls into a native library says so beside the `extern` block that declares it, instead of leaving the caller to pass `-l` and hope.

**A file that links libm**

```rune
#link("m")
#linkpath("/usr/lib")

import std::io

extern "C" {
    fn cbrt(x: f64) -> f64
    fn hypot(a: f64, b: f64) -> f64
}

#safe("libm is linked by the directives at the top of this file")
fn main() -> i64 {
    io::println(unsafe { cbrt(27.0) }.$str())
    io::println(unsafe { hypot(3.0, 4.0) }.$str())
    0
}
```

Each takes one or more strings, and both may appear more than once. What they name is merged with anything `-l` and `-L` asked for, and repeated names are passed once.

Left to itself, the linker picks: a shared library when there is one, else the archive. `type:` decides instead, and insists — the one file of that kind is found in the `#linkpath`s, the `-L` directories and the usual places, named to the linker in full, and its absence is an error (E0546) rather than a quiet substitute.

| Written | Links |
| --- | --- |
| `#link("raylib")` | whichever the linker finds first |
| `#link("Cocoa", type: framework)` | an Apple framework, `-framework Cocoa`, with the `#linkpath`s as framework paths too |
| `#link("raylib", type: static)` | `libraylib.a` — the code goes into the program, which then needs no library to run |
| `#link("raylib", type: dynamic)` | `libraylib.dylib`, `.so`, or `.dll.a`/`.dll` — the program loads it when it starts |

*`-l static:raylib` and a manifest's `link = ["static:raylib"]` say the same.*

They have to be at the top — unless a `#Config(...)` stands straight before them. Then they may be anywhere, and are the condition's: a library one platform needs is linked on that platform alone.

**Linked where the condition holds**

```rune
#Config(os == "macos")
#link("Cocoa", type: framework)

#Config(os == "linux")
#link("X11")

fn main() -> i64 { 0 }
```

**Too late to be a file directive**

```rune
import std::io

#link("m")

fn main() -> i64 { 0 }
```
