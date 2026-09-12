# Conversions that happen on their own

Widening that cannot lose information is implicit. Everything else needs `as`.

| From | To | When |
| --- | --- | --- |
| `i32` | `i64` | same signedness, not narrower |
| `u16` | `i32` | unsigned to a strictly wider signed type |
| `f32` | `f64` | float to a wider float |
| `&var T` | `&T` | a mutable borrow where a shared one is wanted |
| `[N:T]` | `[T]` | an array where a slice is wanted |
| `Derived` | `Base` | a subclass where its base class is wanted |
| `T` | `T?` | wrapped as `Option::Some(value)` |
| `T` | `dyn Mark` | when `T` is bound to that mark |
| `some Mark` | `dyn Mark` | the hidden type is bound, so it boxes |
| `T` | `Any` | anything with a run-time representation |
| `Never` | anything | the expression never produced a value |

*Implicit conversions*

**Widening in argument position**

```rune
import std::io

fn wide(n: i64) -> i64 { n }
fn shared(values: [i64]) -> i64 { values.$length() }
fn optional(v: i64?) -> i64 { v.or(-1) }

fn main() -> i64 {
    let narrow: i32 = 7
    let single: f32 = 1.5
    let array: [3:i64] = [1, 2, 3]

    io::println(wide(narrow))              // i32 widens to i64
    io::println(shared(array))             // array decays to a slice
    io::println(optional(5))               // 5 wraps as Some(5)
    io::println((single as f64) + 0.25)    // f32 widens on its own too
    0
}
```
