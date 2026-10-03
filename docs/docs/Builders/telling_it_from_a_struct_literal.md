# Telling it from a struct literal

The first item decides. A **field** is a name followed by `:`, `,` or the closing brace; anything else is a **value**. So a single bare name is read as a field shorthand, and a trailing `;` is how a block holding one variable is written.

| Written | Read as |
| --- | --- |
| `Body { children: v }` | a struct literal, one field |
| `Body { child }` | a struct literal, shorthand for `child: child` |
| `Body { child; }` | a builder block, one item |
| `Body { Text("hi") }` | a builder block: `Text("hi")` is not a field name |
| `Body { }` | a struct literal with no fields |
| `Body { ..other }` | a struct literal with a base |

**A block of values on something that is not a builder**

```rune
struct Point { x: i64, y: i64 }

fn main() -> i64 {
    let p = Point { 1; 2 }
    p.x
}
```
