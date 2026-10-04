# Declaring a macro anywhere

A `#type(Macros)` file is the place for a family of macros and the helpers they share, but it is not the only place. A `#macro pub fn` may be written in **any** file, beside the code that uses it:

**main.rune**

```text
import std::io
import std::{Macro, text}

#macro
pub fn shout(input: Macro::Tokens) -> Macro::Tokens {
    Macro::parse("\"" + text::upper(input.text()) + "!\"")
}

fn main() -> i64 {
    io::println(shout!(hello))      // HELLO!
    0
}
```

It is gathered in the same pass that finds the `#type(Macros)` files, lifted out of its file, and built into the same macro package. The program never sees it. What it takes along is exactly this:

| Lifted with it | Left behind |
| --- | --- |
| the `#macro fn` itself | every other declaration in the file |
| the file's `import std::...` lines | its other imports — they name the program's modules, which the package does not have |
| its line and column, for diagnostics | file directives such as `#runtime(none)`: the package is built for the host, with the full runtime |

> [!NOTE]
> **Helpers**
>
> A macro that needs helper functions of its own belongs in a `#type(Macros)` file, where the helpers can live beside it.
