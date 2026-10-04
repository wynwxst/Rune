# std::math

| Group | Members |
| --- | --- |
| constants | `PI`, `E`, `TAU`, `SQRT_2`, `LN_2`, `LN_10`, `EPSILON`, `F64_MAX`, `F64_MIN_POSITIVE` |
| powers and roots | `squareRoot`, `cubeRoot`, `power`, `lengthOf`, `exponential` |
| logarithms | `naturalLog`, `log2Of`, `log10Of`, `logOf` |
| trigonometry | `sine`, `cosine`, `tangent`, `arcSine`, `arcCosine`, `arcTangent`, `angleOf`, `hyperbolicSine`, `hyperbolicCosine`, `hyperbolicTangent`, `radians`, `degrees` |
| rounding | `roundDown`, `roundUp`, `roundNearest`, `wholePart`, `fractionPart`, `remainderOf`, `withSignOf` |
| rounding to decimal places | `roundTo`, `truncateTo`, `floorTo`, `ceilTo`, `roundWith` (a `Rounding` mode), `roundToStep` |
| exact decimal amounts | `scaled`, `unscaled`, `splitDecimal` |
| asking about a float | `isNan`, `isInfinite`, `isFinite`, `nearlyEqual`, `infinity`, `nan` |
| comparison, for anything ordered | `least`, `greatest`, `clamp` — bounded on `operator::cmp` |
| magnitude and sign | `absInt`, `absFloat`, `signInt`, `signFloat` |
| exact integer answers | `powerInt`, `squareRootInt`, `gcd`, `lcm`, `divFloor`, `modFloor` |
| fixed-width shorthands | `minInt`, `maxInt`, `minFloat`, `maxFloat`, `clampInt` |
| overflow, for any integer type | `wrappingAdd`/`Sub`/`Mul`, `saturatingAdd`/`Sub`/`Mul`, `checkedAdd`/`Sub`/`Mul` (these answer `T?`) — the `$` intrinsics of the same names as functions |

*Everything takes and returns `f64` unless its name says `Int`. Needs `-l m`, which `rune` adds for you.*

**Numerics**

```rune
import std::io
import std::math

fn main() -> i64 {
    io::println(math::squareRoot(2.0).$str())
    io::println(math::roundUp(math::PI).$str())

    // The comparisons are bounded on an operator rather than on a mark, so
    // they work for anything `<` works on — builtin or your own.
    io::println(math::least(3, 5).$str())
    io::println(math::greatest(2.5, 1.5).$str())
    io::println(math::least("apple", "banana"))
    io::println(math::clamp(120, 0, 100).$str())

    // The integer versions give exact answers where the float ones round.
    io::println(math::squareRootInt(50).$str())      // 7*7 <= 50 < 8*8
    io::println(math::powerInt(3, 5).$str())
    io::println(math::gcd(12, 18).$str())

    // `/` truncates towards zero; `divFloor` goes towards negative infinity,
    // which is what indexing a grid at negative coordinates wants.
    io::println((-7 / 2).$str() + " vs " + math::divFloor(-7, 2).$str())

    // NaN is the one value not equal to itself, so `== nan()` never works.
    io::println(math::isNan(math::nan()).$str())
    io::println(math::nearlyEqual(0.1 + 0.2, 0.3, 1.0e-9).$str())
    0
}
```

Rounding to a number of decimal places rounds the decimal that was *meant*. `2.675` has no exact binary form and is stored a hair below itself, so scaling and rounding naively gives `2.67`; these settle a scaled value that is within float error of a half as that half. `roundWith` takes the mode: `Floor`, `Ceiling`, `TowardZero`, `AwayFromZero`, `HalfAwayFromZero` (what `roundTo` does) and `HalfEven`, banker's rounding. A negative `places` rounds to tens, hundreds and so on.

**Decimal places**

```rune
import std::io
import std::math
import std::text

fn main() -> i64 {
    io::println(math::roundTo(2.675, 2))                            // 2.68
    io::println(math::truncateTo(0.29, 2))                          // 0.29
    io::println(math::roundTo(1234.0, -2))                          // 1200.0
    io::println(math::roundWith(2.25, 1, math::Rounding::HalfEven)) // 2.2
    io::println(math::roundToStep(1.23, 0.05))                      // 1.25

    // Money: count whole hundredths, add exactly, and turn back only to show.
    let cents = math::scaled(103.4, 2) + math::scaled(0.29, 2)
    io::println(cents)                                              // 10369
    let (whole, part) = math::splitDecimal(math::unscaled(cents, 2), 2)
    println!("{}.{}", whole, text::padStart(part.$str(), 2, '0'))   // 103.69
    0
}
```
