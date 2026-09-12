# Exhaustiveness

A `match` over an enum has to cover every variant. The compiler names the ones you missed and points at the declaration.

**A missing variant**

```rune
enum Colour { Red, Green, Blue }

fn name(c: Colour) -> String {
    match c {
        Colour::Red => "red",
        Colour::Green => "green",
    }
}

fn main() -> i64 { 0 }
```
