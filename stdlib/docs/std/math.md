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
