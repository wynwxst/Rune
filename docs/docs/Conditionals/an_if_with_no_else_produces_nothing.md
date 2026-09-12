# An `if` with no `else` produces nothing

The other half of the same rule. When the condition is false an `else`-less `if` has nothing to produce, so it is `()` — fine as a statement, and never usable as a value. The error names the `if`, not whatever was waiting on it:

**No `else`, but a value was wanted**

```rune
fn wants(b: bool) -> bool { b }

fn main() -> i64 {
    let v: i64? = 42
    // The block is an argument, so its value is required.
    wants({
        if v is Some(n) {
            if n > 0 { n == 42 } else { false }
        }
    })
    0
}
```

Three ways out, depending on what the code is really saying:

| Instead of | Write |
| --- | --- |
| nested `if`s over an Option | `match value { Some(v) => …, None => … }` |
| independent tests | `a.hasValue() && a.unwrap() == x` |
| a genuine two-way choice | an explicit `else` on every branch |
