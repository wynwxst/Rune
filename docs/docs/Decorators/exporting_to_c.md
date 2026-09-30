# Exporting to C

`@export` fixes the symbol name so a C caller can find it. Without it, a public function's symbol is qualified by its module.

**A stable symbol name**

```rune
/// Callable from C as `rune_add`.
@export("rune_add")
pub fn add(a: i64, b: i64) -> i64 { a + b }
```
