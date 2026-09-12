# Testing and binding at once

`if value is Pattern` matches and, when the pattern binds names, makes them available in the body. It is the two-case form of `match`.

**`is` with a binding pattern**

```rune
import std::io

enum Reading { Missing, Celsius(f64), Fahrenheit(f64) }

fn asCelsius(r: Reading) -> f64 {
    if r is Reading::Celsius(c) {
        return c
    }
    if r is Reading::Fahrenheit(f) {
        return (f - 32.0) / 1.8
    }
    0.0
}

fn main() -> i64 {
    io::println(asCelsius(Reading::Celsius(21.5)))
    io::println(asCelsius(Reading::Fahrenheit(212.0)))
    io::println(asCelsius(Reading::Missing))

    // It works on Option too, which is just an enum.
    let maybe: i64? = 42
    if maybe is Some(v) {
        io::println("got " + v.$str())
    } else {
        io::println("nothing")
    }
    0
}
```
