# What can be formatted

`{}` renders through `io::Display`, so a type gains formatting by being bound to it — the same thing that lets `io::println` take it. There is no second mark to implement.

**A type that formats**

```rune
import std::io

struct Money { cents: i64 }

bind io::Display to Money {
    // A `display` body may itself use `format!`.
    fn display(&self) -> String {
        format!("${}.{:02}", self.cents / 100, self.cents % 100)
    }
}

fn main() -> i64 {
    let price = Money { cents: 1999 }
    println!("{}", price)
    println!("[{:>10}]", price)     // width applies to any Display
    0
}
```
