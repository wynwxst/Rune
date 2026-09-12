# Macros that declare

A body is spliced where it is called, so it may be anything the grammar accepts at that point — including declarations. A macro can stand for a type and its methods:

**A macro standing for a type**

```rune
import std::io

macro Container {
    ($name: ident, $res: ty) => {
        struct $name { value: $res }
        extend $name {
            fn new(val: $res) -> Self { Self { value: val } }
        }
    }
}

Container!(Integer, i32)
Container!(Str, String)

fn main() -> i64 {
    io::println(Integer::new(42).value.$str())
    io::println(Str::new("hello").value)
    0
}
```

> [!WARNING]
> **Declare at file scope**
>
> Such a macro is called at file scope, not inside a body. Types are collected before any body is checked, so one declared in a body is found too late to register — which the compiler now says outright rather than failing at the first use.
