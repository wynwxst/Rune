# When the arms need not agree

Every branch of an `if` or a `match` has to produce the same type — but only when something is going to *use* that type. In statement position nobody reads the result, so there is nothing for the arms to agree on, and each is checked on its own terms.

**Discarded branches, mixed arms**

```rune
import std::io

enum E { A, B, C }

fn classify(n: i64) -> String {
    // Used as this function's result, so the arms must still agree.
    if n > 0 { "positive" } else { "negative" }
}

fn main() -> i64 {
    let n = 5

    // A statement: these arms produce `()`, `i64` and `String`, and that is
    // fine, because nobody reads the result.
    if n > 3 { io::println("big") }
    elif n > 1 { 1 }
    else { "small" }

    match E::A {
        E::A => io::println("a"),
        E::B => 42,
        E::C => "c",
    }

    io::println(classify(1))
    0
}
```

| Where the branching expression sits | Arms must agree |
| --- | --- |
| a statement on its own | no |
| the tail of a loop body | no — `while` and `for` produce `()`, and a `loop` produces what `break` carries |
| a binding's initialiser | **yes** |
| a function's result | **yes** |
| an operand of something else | **yes** |

> [!NOTE]
> **It is about the value, not the shape**
>
> The rule follows the value, not the syntax: an `if` nested inside a discarded one is still checked when *its* value is used — bound to a name, say. Only positions whose value is genuinely thrown away are relaxed.
