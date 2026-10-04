# Documentation

`rune doc` writes one page out of two halves that do not know about each other: what the compiler saw, and what you wrote under `docs/`.

Prose attaches to a declaration in one of two ways. `#Doc("...")` is the explicit form and takes a triple-quoted block for anything longer than a line; a `///` comment above the declaration does the same with less ceremony. Both are read at compile time and neither runs, so a library that is only ever linked against can still describe itself.

**Two ways to say the same thing**

```rune
import std::io

/// Greets a person in whichever language you ask for.
pub class hello {
    pub name: String
    fn init(self, name: String) { self.name = name }

    #Doc("""
    Greets in Spanish.

    `Hola` is the everyday greeting, used at any hour and with anyone.
    """)
    pub fn spanish(&self) -> String { "Hola, " + self.name }

    /// Greets in French, described by a comment rather than a decorator.
    pub fn french(&self) -> String { "Bonjour, " + self.name }

    pub fn korean(&self) -> String { "Annyeong, " + self.name }
}

fn main() -> i64 {
    io::println(hello("Ada").spanish())
    0
}
```

> [!NOTE]
> **Which one is used**
>
> When a declaration has both, `#Doc` wins: the decorator was written for the reader, and the comment may only have been written for whoever is editing the code.

## Pages

- [The two halves](the_two_halves.md)
- [What the reference reports](what_the_reference_reports.md)
- [Reading it](reading_it.md)
- [Running it](running_it.md)
