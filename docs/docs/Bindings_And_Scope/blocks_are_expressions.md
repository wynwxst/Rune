# Blocks are expressions

A block's value is its last expression, provided that expression has no semicolon. The same rule gives functions their result.

**The semicolon decides**

```rune
import std::io

fn main() -> i64 {
    let computed = {
        let base = 2 + 2
        base * base          // no semicolon: this is the block's value
    }

    let discarded = {
        let base = 2 + 2
        base * base;         // semicolon: the value is dropped
        99
    }

    io::println(computed)
    io::println(discarded)
    0
}
```

> [!NOTE]
> **Void functions**
>
> A function with no `->` returns `()`. A trailing expression in such a function is evaluated and its value discarded.
