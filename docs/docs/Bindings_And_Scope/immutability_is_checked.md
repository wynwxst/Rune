# Immutability is checked

Assigning to a binding that was never declared `var` is an error, and the diagnostic points at both the assignment and the declaration it conflicts with.

**Reassigning an immutable binding**

```rune
fn main() -> i64 {
    let total = 0
    total = 1
    total
}
```
