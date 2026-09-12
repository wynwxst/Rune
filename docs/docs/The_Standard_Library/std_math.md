# std::math

| Group | Members |
| --- | --- |
| constants | `PI`, `E`, `TAU`, `SQRT_2`, `LN_2`, `LN_10`, `EPSILON`, `F64_MAX`, `F64_MIN_POSITIVE` |
| powers and roots | `squareRoot`, `cubeRoot`, `power`, `lengthOf`, `exponential` |
| logarithms | `naturalLog`, `log2Of`, `log10Of`, `logOf` |
| trigonometry | `sine`, `cosine`, `tangent`, `arcSine`, `arcCosine`, `arcTangent`, `angleOf`, `hyperbolicSine`, `hyperbolicCosine`, `hyperbolicTangent`, `radians`, `degrees` |
| rounding | `roundDown`, `roundUp`, `roundNearest`, `wholePart`, `fractionPart`, `remainderOf`, `withSignOf` |
| asking about a float | `isNan`, `isInfinite`, `isFinite`, `nearlyEqual`, `infinity`, `nan` |
| comparison, for anything ordered | `least`, `greatest`, `clamp` — bounded on `operator::cmp` |
| magnitude and sign | `absInt`, `absFloat`, `signInt`, `signFloat` |
| exact integer answers | `powerInt`, `squareRootInt`, `gcd`, `lcm`, `divFloor`, `modFloor` |
| fixed-width shorthands | `minInt`, `maxInt`, `minFloat`, `maxFloat`, `clampInt` |

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
