# Macros a library exports

A procedural macro is exported with the library it is written in, like a `pub fn`. The `.rul` carries it as source — the library's `#type(Macros)` files and every `#macro fn` written among its code — and a package that imports the library builds those into its own macro package, for the machine doing the compiling. Nothing else is needed:

**using a library's macros**

```text
import std::io
import shapes                       // a library with `square!` and `greet!`

fn main() -> i64 {
    io::println(greet!(world))      // hello, world
    io::println((square!(5)).$str()) // 25
    0
}
```

The library's own source is re-read by everyone who imports it, and expands with the same macros, so a library is free to use the macros it exports.
