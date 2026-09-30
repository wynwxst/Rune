# std::math

libm wrapped and named, the constants, comparison for anything ordered, and
the integer answers a float cannot give exactly. Everything float is `f64`;
an `f32` widens on the way in.

## Floats

```rune
import std::io
import std::math

fn main() -> i64 {
    io::println(math::squareRoot(2.0))
    io::println(math::power(2.0, 10.0))
    io::println(math::roundNearest(2.5))
    io::println(math::lengthOf(3.0, 4.0))
    io::println(math::degrees(math::PI))
    io::println(math::nearlyEqual(0.1 + 0.2, 0.3, math::EPSILON * 4.0))
    io::println(math::isNan(math::nan()))
    io::println(math::clamp(1.7, 0.0, 1.0))
    0
}
```

## Decimal places

`roundTo`, `truncateTo`, `floorTo` and `ceilTo` round to a number of decimal
places, and `roundWith` takes a `Rounding` mode — including `HalfEven`, banker's
rounding. They round the decimal that was *meant*: a scaled value within float
error of a whole number (or of a half) is taken to be exactly that, so
`truncateTo(0.29, 2)` is `0.29` rather than the `0.28` that cutting off
`28.999999999999996` would give. A negative `places` rounds to tens, hundreds
and so on.

For money, count in whole units: `scaled` turns an amount into hundredths (or
any `10^-places`), `unscaled` turns it back, and `splitDecimal` gives the whole
part and the digits after the point, carrying when rounding says to.

```rune
import std::io
import std::math
import std::text

fn main() -> i64 {
    io::println(math::truncateTo(0.29, 2))                          // 0.29
    io::println(math::roundTo(2.675, 2))                            // 2.68
    io::println(math::roundTo(1234.0, -2))                          // 1200.0
    io::println(math::ceilTo(0.1 + 0.2, 1))                         // 0.3
    io::println(math::roundWith(2.25, 1, math::Rounding::HalfEven)) // 2.2
    io::println(math::roundToStep(1.23, 0.05))                      // 1.25

    let cents = math::scaled(103.4, 2) + math::scaled(0.29, 2)      // 10369
    io::println(math::unscaled(cents, 2))                           // 103.69
    let (dollars, rest) = math::splitDecimal(19.999, 2)             // (20, 0)
    io::println("$" + dollars.$str() + "." + text::padStart(rest.$str(), 2, '0'))
    0
}
```

## Integers

```rune
import std::io
import std::math

fn main() -> i64 {
    io::println(math::powerInt(3, 4))
    io::println(math::squareRootInt(99))
    io::println(math::gcd(12, 18))
    io::println(math::lcm(4, 6))
    io::println(math::divFloor(-7, 2))    // -4, where `/` gives -3
    io::println(math::modFloor(-7, 2))    // 1
    io::println(math::least(3, 9))
    io::println(math::greatest("apple", "pear"))
    0
}
```

## Overflow, by name

`+`, `-` and `*` trap on overflow in a debug build and wrap in a release one.
These say which is meant, and mean it at every setting.

```rune
import std::io
import std::math

fn main() -> i64 {
    let x: i8 = 127
    let one: i8 = 1
    io::println(math::wrappingAdd(x, one))       // -128
    io::println(math::saturatingAdd(x, one))     // 127
    io::println(math::checkedAdd(x, one).isNil())   // true: it did not fit
    let basis: u64 = 0xcbf29ce484222325
    let prime: u64 = 0x100000001b3
    io::println(math::wrappingMul(basis, prime))
    0
}
```
