# Folding a repetition

A repetition that has to combine its rounds — a sum, a product, an `&&` across every argument — puts the operator **inside** `$( )`, starting from the identity. `$( )*` already means "once per round"; there is no separate slot for something between rounds, so the operator comes along with each round and the identity gives the first one a left-hand side.

**Folding with the operator inside the repetition**

```rune
import std::io

macro sum     { ($($item: expr),*) => { 0 $(+ $item)* } }
macro product { ($($item: expr),*) => { 1 $(* $item)* } }

fn main() -> i64 {
    // sum!(1, 2, 3)  ->  0 + 1 + 2 + 3
    io::println(sum!(1, 2, 3).$str())
    io::println(product!(2, 3, 4).$str())

    // Nothing to fold is just the identity, which is why it is there.
    io::println(sum!().$str())

    // The literal takes its type from the context, so this sums floats.
    let total: f64 = sum!(2.0, 4.0, 4.5)
    io::println(total.$str())
    0
}
```

> [!WARNING]
> **The pattern's separator is not the body's**
>
> Writing `$($item)*` with the separator in the *pattern* only is a common slip: the pattern's `,` says how to read the arguments, not what to put between them. Such a macro expands to `1 2 3`, which is three statements, not a sum.
