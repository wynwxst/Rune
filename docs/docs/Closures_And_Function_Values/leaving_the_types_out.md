# Leaving the types out

Where the closure is going already says what it takes, it need not be said again: a parameter may be written as a bare name, and the result follows from the body.

**Written short**

```rune
import std::io
import std::iter
import std::collections::vector

fn applyTwice(f: @function(i64) -> i64, n: i64) -> i64 { f(f(n)) }

fn main() -> i64 {
    // The parameter's type comes from what `applyTwice` says it takes.
    io::println(applyTwice(||(n) { n + 5 }, 2))

    // And from an annotated binding.
    let double: @function(i64) -> i64 = ||(n) { n * 2 }
    io::println(double(21))

    // And through a call still being inferred: `map` says what the closure
    // is handed while what it hands back is the thing being worked out.
    for n in vec![1, 2, 3].map(||(n) { n * 100 }) {
        io::println(n)
    }
    0
}
```

Nothing about this is special to a particular function. Any place with a written-down function type — an argument, an annotated binding, a declared result — supplies the parameters, and a place that says nothing does not:

**Nothing here says what `n` is**

```rune
fn main() -> i64 {
    let orphan = ||(n) { n + 1 }
    0
}
```

> [!NOTE]
> **Still allowed**
>
> The types may always be written, and mixing the two is fine: `||(n: i64) { n + 1 }` leaves only the result to be worked out.
