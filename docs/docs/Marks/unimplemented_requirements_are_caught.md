# Unimplemented requirements are caught

**A missing requirement**

```rune
mark Show {
    fn show(&self) -> String
    fn describe(&self) -> String
}

struct Point { x: i64 }

bind Show to Point {
    fn show(&self) -> String { "point" }
}

fn main() -> i64 { 0 }
```
