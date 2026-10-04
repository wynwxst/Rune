# Explicit discriminants

A variant with no payload may be given a number. Such an enum converts to an integer with `as`; the numbering continues from the last value given.

**Enums with wire values**

```rune
import std::io

enum Status {
    Ok = 200,
    Created,            // 201
    NotFound = 404,
    Teapot = 418,
}

fn main() -> i64 {
    io::println(Status::Ok as i64)
    io::println(Status::Created as i64)
    io::println(Status::NotFound as i64)
    io::println(Status::Teapot as i64)
    io::println(Status::Ok == Status::Ok)
    0
}
```

A value may also be integer arithmetic on numbers and on the variants declared before it — `+ - * / %`, the shifts, `& | ^` and `~` — which is how flags and C's aliases are written. Two variants with one value are one tag: a `match` takes the first arm that names it. A call or a variable is not a constant, and is E0400.

**Flags, and an alias**

```rune
import std::io

enum Style {
    Titled = 1 << 0,
    Closable = 1 << 1,
    Resizable = 1 << 3,
    Standard = Titled | Closable | Resizable,
}

enum Family { Iso = 7, Osi = Iso, Ecma }    // Osi is Iso; Ecma is 8

fn main() -> i64 {
    io::println(Style::Standard as i64)
    io::println(Family::Osi as i64)
    io::println(Family::Ecma as i64)
    0
}
```

An alias is written as the other variant's name, as `Osi = Iso` is. Two variants that land on one value any other way — two equal numbers, or a value counted on from the variant before into one already taken — are usually a slip, and warn (W0402).

**A value taken twice**

```rune
enum Step { First = 1, Reset = 0, Second }   // Second is 1 too

fn main() -> i64 { 0 }
```

A value may be negative, and the counting goes on from it as from any other. It may also be a **float** — exchange rates, thresholds, the constants a C header names. A float cannot be a tag, so a float-valued enum keeps its tags counting up from zero and each variant's value beside its tag; `as` gives that value back as any number type. Since there is no next float to count on to, every variant of one has to be given a value (E0401). A suffix — `1f64`, `-40.5f32` — chooses the type; without one it is `f64`.

**Negative and float values**

```rune
import std::io
import std::fmt

enum Level { Low = -1, Mid, High = 10, Top }        // Mid is 0, Top is 11
enum Currency { USD = 1.0, AUD = 1.43, GBP = 0.76 }

fn main() -> i64 {
    io::println(Level::Low as i64)
    io::println(Level::Top as i64)
    io::println(Currency::AUD as f64)
    var total = 0.0
    for c in [Currency::USD, Currency::AUD, Currency::GBP] { total += c as f64 }
    io::println(fmt::fixed(total, 2))
    io::println(Currency::GBP == Currency::GBP)      // compared by tag
    0
}
```

**A float enum gives every variant its value**

```rune
enum Ratio { Half = 0.5, Third }

fn main() -> i64 { 0 }
```
