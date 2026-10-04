# Integer overflow

`+`, `-`, `*` and unary `-` on a fixed-width integer can produce a result that does not fit. A build with safety checks — any build, at any optimisation level, `-O2` being the default — traps with a panic, the way a bounds check does; a **release build** (`rune build --release`) wraps in two's complement. The mistake is caught while the program is being written and costs nothing once it is shipped. `--overflow-checks` and `--no-overflow-checks` pin it either way, `overflow-checks = true|false` under `[build]` does the same for a package, and `--safety none` never traps.

**Overflow in a debug build**

```rune
import std::io

fn main() -> i64 {
    var count: i8 = 127
    count += 1
    io::println(count)
    0
}
```

When the program *means* it — a hash that is supposed to wrap, a counter that must never — the operation says so by name and gets that behaviour at every setting: `$wrappingAdd`, `$saturatingAdd` and `$checkedAdd`, with `Sub` and `Mul` beside each, and the same family as free functions in `std::math`.

**Wrapping, saturating and checked, by name**

```rune
import std::io
import std::math

fn main() -> i64 {
    let x: i8 = 127
    io::println(x.$wrappingAdd(1))         // -128: two's complement
    io::println(x.$saturatingAdd(1))       // 127: clamped at the limit
    io::println(x.$checkedAdd(1) ?? -1)    // nil, so -1
    io::println(x.$checkedAdd(0) ?? -1)    // 127: it fitted

    // A hash is supposed to wrap, and says so.
    let basis: u64 = 0xcbf29ce484222325
    let prime: u64 = 0x100000001b3
    io::println(math::wrappingMul(basis, prime))
    0
}
```

| Form | On overflow |
| --- | --- |
| `a + b`, `a - b`, `a * b`, `-a` | traps, or wraps in a release build |
| `a.$wrappingAdd(b)` `$wrappingSub` `$wrappingMul` | wraps |
| `a.$saturatingAdd(b)` `$saturatingSub` `$saturatingMul` | clamps to the type's limits |
| `a.$checkedAdd(b)` `$checkedSub` `$checkedMul` | `nil`; otherwise `Some(result)` |
| `math::wrappingAdd(a, b)` and the rest | the same three families as functions |

*Both operands share one integer type.*

> [!NOTE]
> **Asking at compile time**
>
> `#Config(overflow_checks == "on")` tells a declaration which world it is in, for the rare case that wants to know.
