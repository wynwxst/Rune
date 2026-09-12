# std::testing

What a file under `tests/` reports through. A test is an ordinary program:
each check prints its own line, `summary()` returns what `main` should, and
`rune test` reads the exit status.

## A test file

```rune
import std::testing
import std::io
import std::collections::slice

fn double(n: i64) -> i64 { n * 2 }
fn find(xs: [i64], wanted: i64) -> i64? {
    for (i, x) in slice::iterate(xs).enumerate() { if x == wanted { return i } }
    nil
}

fn main() -> i64 {
    testing::equal("doubling", double(21), 42)
    testing::notEqual("not the same", double(1), 3)
    testing::isTrue("positive", double(1) > 0)
    testing::isFalse("not negative", double(1) < 0)
    testing::isSome("present", find([1, 2, 3], 2))
    testing::isNone("absent", find([1, 2, 3], 9))
    assert!(double(2) == 4)                     // named after the expression
    assertEq!(double(3), 6)
    let (passed, failed) = testing::counts()
    io::println(passed.$str() + " passed, " + failed.$str() + " failed")
    testing::summary()
}
```
