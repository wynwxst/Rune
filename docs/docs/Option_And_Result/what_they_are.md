# What they are

There is no compiler magic in the types themselves — they are declared in the standard library and you could have written them:

**The declarations, from std::option and std::result**

```text
pub enum Option<T> {
    None,
    Some(T),
}

pub enum Result<T, E> {
    Ok(T),
    Err(E),
}
```

| You write | Which means |
| --- | --- |
| `i64?` | `Option<i64>` |
| `nil` | `Option::None`, with `T` taken from context |
| `a ?? b` | the value in `a`, or `b` when it is `None` |
| `value?` | return early on `None` or `Err`, else the payload |
| `Some(v)` `None` | the variants, in scope without an import |
| `Ok(v)` `Err(e)` | likewise |

*The sugar and what it lowers to*
