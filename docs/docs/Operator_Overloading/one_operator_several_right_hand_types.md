# One operator, several right-hand types

An operator is overloaded **once per right-hand type**. A type may therefore define `+` against several others, and which one runs is decided by what is on the right — including for compound assignment, where the two sides need not be the same type at all.

**`+` three ways on one type**

```rune
import std::io

struct Money { pub cents: i64 }
struct Rate  { pub percent: i64 }

// Three overloads of `+`, told apart by what is on the right.
bind operator::add to Money {
    fn add(&self, rhs: &Money) -> Money { Money { cents: self.cents + rhs.cents } }
}
bind operator::add to Money {
    fn add(&self, rhs: i64) -> Money { Money { cents: self.cents + rhs } }
}
bind operator::add to Money {
    fn add(&self, rhs: &Rate) -> Money {
        Money { cents: self.cents + self.cents * rhs.percent / 100 }
    }
}

fn main() -> i64 {
    let m = Money { cents: 100 }
    io::println((m + Money { cents: 50 }).cents.$str())
    io::println((m + 7).cents.$str())
    io::println((m + Rate { percent: 10 }).cents.$str())

    // Compound assignment picks the same way, so the right side need not
    // be the type on the left.
    var running = Money { cents: 100 }
    running += Rate { percent: 50 }
    io::println(running.cents.$str())
    0
}
```

> [!NOTE]
> **How one is chosen**
>
> An exact match on the right-hand type wins. Failing that, the first overload whose parameter would accept the value is used, so an overload taking `i64` also serves an `i32` on the right.

This is not special to arithmetic. Every method a `bind` supplies may be written more than once and told apart by what it takes — `index` and `indexSet` above, and any mark requirement too. See [One name, several parameter lists](#marks).
