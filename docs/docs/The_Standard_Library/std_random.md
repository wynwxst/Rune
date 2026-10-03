# std::random

A `Random` is a generator with its own state. Seeded from a number it repeats exactly, which is what a test or a simulation wants; seeded from the operating system with `new` it does not. The generator is xoshiro256** over SplitMix64 — fast and good, and *not* a source of secrets: a key or a token comes from `bytes`, which is the operating system's own entropy.

| Name | Signature | Does |
| --- | --- | --- |
| `Random` | `struct` | 256 bits of state; a copy is a second generator producing the same numbers |
| `seeded` | `(seed: u64) -> Random` | reproducible |
| `new` | `() -> Random` | seeded from the OS |
| `Random::next` | `(&var self) -> u64` | the next 64 bits |
| `Random::below` | `(&var self, bound: i64) -> i64` | `0` up to but not including `bound`, unbiased |
| `Random::between` | `(&var self, low: i64, high: i64) -> i64` | both ends included |
| `Random::float` / `floatBetween` | `(&var self) -> f64` | `[0, 1)` with 53 bits; or `[low, high)` |
| `Random::chance` / `coin` | `(&var self, p: f64) -> bool` | true with probability `p`; or evenly |
| `Random::choose` / `pick` | `<T>(&var self, [T]) -> T?` | one element of a slice, or a copy of one from a borrowed `Vector` |
| `Random::shuffle` | `<T>(&var self, &var Vector<T>)` | a uniformly random order, in place |
| `bytes` | `(count: i64) -> Vector<u8>` | straight from the OS — for anything secret |
| `next`, `below`, `between`, `float`, `chance`, `coin`, `choose`, `shuffle` | free functions | the same, on one shared generator seeded from the OS on first use |

**Repeatable, and not**

```rune
import std::io
import std::random
import std::collections::vector

fn main() -> i64 {
    var fixed = random::seeded(42)
    io::println(fixed.next())                 // always this number
    io::println(fixed.between(1, 6) >= 1)

    var rng = random::new()                   // different every run
    var deck = vec!("A", "K", "Q", "J")
    rng.shuffle(&var deck)
    io::println(deck.length())
    io::println(rng.float() < 1.0)
    io::println(random::bytes(16).length())
    0
}
```
