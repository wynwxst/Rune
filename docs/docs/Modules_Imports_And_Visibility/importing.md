# Importing

`import path::to::module` brings a module into scope under its last name; members are reached with `::`. `import x as y` renames it. The standard library is implicitly available but still needs importing by name.

**Importing and renaming**

```rune
import std::io
import std::math as m

fn main() -> i64 {
    io::println(m::squareRoot(144.0).$str())
    io::println(m::PI.$str())
    0
}
```

A handful of names need no import at all, because the language itself is defined in terms of them: `Option`, `Result`, and their variants `Some`, `None`, `Ok` and `Err`.

**The prelude**

```rune
fn parse(text: String) -> Result<i64, String> {
    // No import: the prelude puts these in every module's scope.
    match text.$toInt() {
        Some(n) => Ok(n)
        None => Err("not a number: " + text)
    }
}
```
