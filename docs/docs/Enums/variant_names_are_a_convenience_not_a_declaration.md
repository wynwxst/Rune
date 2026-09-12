# Variant names are a convenience, not a declaration

`Colour::Red` is the name of a variant. A bare `Red` is a shorthand for it, and a shorthand is all it is: it lives beside the names a module declares rather than among them. So a variable, a function or a type may be called `Red` too, and two enums may each have one.

**Three things called `Wheel`**

```rune
import std::io

enum Car  { Body, Wheel, Door }
enum Ship { Hull, Wheel, Sail }

// A binding may take a variant's name. Neither enum loses anything.
global Wheel: i64 = 42
fn Door() -> i64 { 7 }

fn describe(c: Car) -> String {
    match c {
        // The scrutinee is a `Car`, so a bare name means one of its variants.
        Wheel => "car wheel",
        Body  => "car body",
        Door  => "car door",
    }
}

fn main() -> i64 {
    io::println(Wheel)                    // the global
    io::println(describe(Car::Wheel))     // the variant
    io::println(Door())                   // the function
    if Body == Car::Body { io::println("`Body` is claimed by one enum, so it works bare") }
    0
}
```

What a shared name costs is that a bare mention of it has to say which enum it belongs to. The error arrives where the ambiguity actually is — at the use, not at the declaration — and lists the candidates.

**A bare name two enums claim**

```rune
enum Car  { Body, Wheel }
enum Ship { Hull, Wheel }

fn main() -> i64 {
    let c = Wheel
    0
}
```

> [!NOTE]
> **Note**
>
> A `match` arm is the exception, because the scrutinee's type already says which enum is meant. `Wheel =>` inside a `match` on a `Car` is `Car::Wheel`, and needs no qualification.

> [!NOTE]
> **Note**
>
> `Option` and `Result` are in scope everywhere, so `Some`, `None`, `Ok` and `Err` are too — but only as the lowest tier. A module that declares an enum with a `Some` of its own wins outright, and is not made ambiguous by the prelude's.
