# std::random

A `Random` is a generator with its own state. Seeded from a number it repeats
exactly, which is what a test or a simulation wants; seeded from the operating
system with `new` it does not. The generator is xoshiro256** — fast and good,
and *not* a source of secrets: those come from `bytes`.

## A generator of your own

```rune
import std::io
import std::random
import std::collections::vector

fn main() -> i64 {
    var fixed = random::seeded(42)
    io::println(fixed.next())                 // always 1546998764402558742
    var again = random::seeded(42)
    again.next()
    io::println(fixed.next() == again.next())  // the same sequence

    var rng = random::new()
    let die = rng.between(1, 6)
    io::println(die >= 1 && die <= 6)
    io::println(rng.below(10) < 10)
    let f = rng.float()
    io::println(f >= 0.0 && f < 1.0)
    io::println(rng.chance(1.0))
    io::println(rng.chance(0.0))

    var deck = vec!("A", "K", "Q", "J")
    rng.shuffle(deck)
    io::println(deck.length())
    io::println(rng.pick(deck).hasValue())
    let items: [3:i64] = [1, 2, 3]
    io::println(rng.choose(items).hasValue())
    0
}
```

## The shared generator, and secrets

The module-level functions draw from one generator seeded from the OS the
first time it is used. `bytes` skips the generator entirely.

```rune
import std::io
import std::random

fn main() -> i64 {
    io::println(random::between(0, 0))
    io::println(random::coin() || true)
    io::println(random::bytes(16).length())
    io::println(random::bytes(0).length())
    0
}
```
