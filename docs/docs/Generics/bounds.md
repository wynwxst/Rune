# Bounds

A bound says what the parameter must be able to do. Without one, the body can only move the value around. `<T: Mark>` and a `where` clause say the same thing and are checked the same way; `where` exists for the bound whose subject is not a parameter name.

**`T: Mark`, `T: A + B`, and `where`**

```rune
import std::io
import std::iter

mark Weighed {
    fn weight(&self) -> i64
}

bind Weighed to i64 { fn weight(&self) -> i64 { self } }
bind Weighed to String { fn weight(&self) -> i64 { self.$length() } }

fn heaviest<T: Weighed>(a: T, b: T) -> T {
    if a.weight() >= b.weight() { a } else { b }
}

// Several bounds with `+`, or spelled out in a `where` clause.
fn describe<T: Weighed + io::Display>(value: T) -> String {
    value.display() + " weighs " + value.weight().$str()
}

fn compare<A, B>(a: A, b: B) -> i64
where A: Weighed, B: Weighed
{
    a.weight() - b.weight()
}

// What `where` adds is a subject that is not a parameter name. There is
// nowhere else to say this: `Iter::Item` is reached *through* a parameter.
fn firstWeight<S>(seq: S) -> i64
where S: iter::Sequence, S::Iter::Item: Weighed
{
    for item in seq { return item.weight() }
    0
}

fn main() -> i64 {
    io::println(heaviest(3, 9))
    io::println(heaviest("ab", "abcd"))
    io::println(describe(42))
    io::println(describe("hello"))
    io::println(compare("abc", 1))
    0
}
```

**An unmet bound points at both ends**

```rune
mark Weighed { fn weight(&self) -> i64 }

struct Feather { grams: f64 }

fn heaviest<T: Weighed>(a: T, b: T) -> T { a }

fn main() -> i64 {
    heaviest(Feather { grams: 0.1 }, Feather { grams: 0.2 })
    0
}
```

A bound may also name an *operator* rather than a mark. `T: operator::cmp` says the argument has to compare, whoever it got that from — which is often more direct than inventing a mark for it.

The question is whether the operator *works* on `T`, not whether somebody wrote it out. A builtin type therefore satisfies such a bound with no `bind` at all: `3 < 9` needs nobody's permission, and a generic that compares should accept `i64` for the same reason ordinary code does.

**Bounded by an operator**

```rune
import std::io

struct Money { cents: i64 }

bind operator::cmp to Money {
    fn cmp(&self, other: Money) -> i64 { self.cents - other.cents }
}
bind operator::add to Money {
    fn add(&self, other: Money) -> Money {
        Money { cents: self.cents + other.cents }
    }
}

fn largest<T: operator::cmp>(a: T, b: T) -> T { if a > b { a } else { b } }
fn total<T>(a: T, b: T) -> T where T: operator::add { a + b }

fn main() -> i64 {
    // A type that says how it compares.
    let a = Money { cents: 250 }
    let b = Money { cents: 195 }
    io::println(largest(a, b).cents.$str())
    io::println(total(a, b).cents.$str())

    // And the ones the language already knows how to compare.
    io::println(largest(3, 9).$str())
    io::println(largest("ab", "cd"))
    io::println(total(2.5, 1.5).$str())
    0
}
```

**A type that does not overload it**

```rune
struct Plain { v: i64 }
fn largest<T: operator::cmp>(a: T, b: T) -> T { if a > b { a } else { b } }
fn main() -> i64 {
    largest(Plain { v: 1 }, Plain { v: 2 }).v
}
```

> [!NOTE]
> **Which names**
>
> Any operator name works, in any spelling — `operator::cmp`, `operator::LessThan`, `operator::"<"` — and several may be combined with `+` just like mark bounds.
