# When a pattern is not enough

A pattern matches a shape. It cannot *compute* one: it has no way to count what it was given, read a name and derive another from it, or build a table out of its own entries. A macro that has to do any of that is written as **code** — an ordinary Rune function marked `@macro`, in a file that says it is a macro package.

**macros.rune**

```text
@type(Macros)

import std::Macro
import std::collections::vector

@macro
pub fn twice(input: Macro::Tokens) -> Macro::Tokens {
    let it = input.text()
    Macro::parse("((" + it + ") + (" + it + "))")
}
```

**anywhere in the program**

```text
fn main() -> i64 { twice!(3) }        // 6
```

A `@type(Macros)` file is a package of its own. The compiler builds it **first** — for the machine doing the compiling, whatever the program is being built for — and then runs it to expand each invocation. Two things follow, and they are why it is done this way:

|  | Because it is compiled |
| --- | --- |
| Everything works | a macro is ordinary Rune. Generics, marks, `std::collections`, files — whatever it needs. There is no subset of the language to learn |
| Nothing leaks | what the package imports and declares is its own business. The program sees only the tokens that come back |
| A crash is contained | the package is a separate program, so a macro that fails is a failed expansion rather than a failed compiler |
| It can be run by hand | set `RUNE_MACRO_REQUEST` and `RUNE_MACRO_ANSWER` and run the package, and you can see exactly what it produces |

> [!NOTE]
> **Cross builds**
>
> A cross build is no different: the macro package is built for the host and run there, and the program is built for the target.
