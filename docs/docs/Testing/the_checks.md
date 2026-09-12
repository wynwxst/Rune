# The checks

| Function | Passes when |
| --- | --- |
| `equal(name, got, want)` | the two render the same |
| `notEqual(name, got, want)` | they do not |
| `isTrue(name, cond)` | the condition holds |
| `isFalse(name, cond)` | it does not |
| `isSome(name, opt)` | the Option holds something |
| `isNone(name, opt)` | it holds nothing |
| `unreachable(name)` | never — for a branch that should not have run at all |

> [!NOTE]
> **Why equality is by rendering**
>
> `equal` compares values **as they print**. That covers every builtin exactly, and it means a type only has to be bound to `io::Display` to be testable — there is no separate equality bound to satisfy, which matters because a builtin like `i64` has built-in `==` rather than a binding to `operator::eq`.

**Checking a container**

```rune
import std::testing
import std::mem

fn main() -> i64 {
    let b = mem::Buffer<i64>(4)
    testing::isTrue("a fresh buffer holds its size", b.holds(3))
    testing::isFalse("and not past it", b.holds(9))
    testing::equal("its slots start zeroed", b[0], 0)

    // An Option, with the payload type given: it cannot be inferred from
    // nothing.
    testing::isNone::<String>("a counted slot is empty until written",
                              mem::Buffer<String>(2).at(0))

    let (passed, failed) = testing::counts()
    testing::equal("four checks ran", passed + failed, 4)
    testing::summary()
}
```
