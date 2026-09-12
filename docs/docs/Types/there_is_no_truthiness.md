# There is no truthiness

A condition must be a `bool`. Comparing explicitly is the only way, and the compiler says so when you forget.

**An integer is not a condition**

```rune
fn main() -> i64 {
    let count = 3
    if count {
        return 1
    }
    0
}
```
