# Shared and mutable borrows

A borrow lets a function reach a value without taking it. `&value` creates one, `*p` reaches through it, and field and method access dereference on their own — so the `*` is only needed when the whole value is the target.

**Borrowing a struct**

```rune
import std::io

struct Counter { value: i64, step: i64 }

/// Reads through a shared borrow; the caller keeps the value.
fn peek(c: &Counter) -> i64 {
    c.value                 // no `*` needed for a field
}

/// Writes through a mutable borrow.
fn advance(c: &var Counter) {
    (*c).value += (*c).step
}

fn doubled(n: &i64) -> i64 {
    *n * 2                  // a scalar, so the `*` is required
}

fn main() -> i64 {
    var counter = Counter { value: 10, step: 4 }
    io::println("before: " + peek(&counter).$str())
    advance(&var counter)
    advance(&var counter)
    io::println("after:  " + peek(&counter).$str())
    io::println("doubled step: " + doubled(&counter.step).$str())
    0
}
```

> [!WARNING]
> **Mutability is checked**
>
> `&var` needs a mutable place. Borrowing a `let` binding mutably is an error, which is how the compiler knows a shared borrow cannot be written through.

**A `let` cannot be borrowed mutably**

```rune
import std::io

fn bump(n: &var i64) { *n += 1 }

fn main() -> i64 {
    let fixed = 1
    bump(&var fixed)
    0
}
```
