# How instantiation is reported

A generic body is checked once per set of arguments. When something inside it fails, the diagnostic carries both the failure and the call that caused it — the same shape as an unmet bound.

**The call site travels with the error**

```rune
fn lengthOf<T>(value: T) -> i64 {
    value.$length()
}

fn main() -> i64 {
    lengthOf("text")
    lengthOf(42)
    0
}
```
