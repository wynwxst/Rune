# Testing

A test is an ordinary program. `rune test` builds and runs every file under `tests/`, and the exit status is the verdict — which is what `std::testing` produces for you.

There is no test runner to configure and no attribute to remember. A file under `tests/` has a `main` like any other program, makes whatever checks it wants, and hands the tally back:

**A test file, start to finish**

```rune
import std::testing

fn twice(n: i64) -> i64 { n * 2 }

fn main() -> i64 {
    testing::equal("twice doubles", twice(21), 42)
    testing::isTrue("and is monotonic", twice(3) > twice(2))
    testing::equal("this one fails on purpose", twice(2), 5)
    testing::summary()
}
```

Every check prints its own line as it runs, so a failure names itself and says what it expected. `summary()` prints the tally and returns zero when everything passed and one otherwise, which is exactly what `main` should hand back.

## Pages

- [The checks](the_checks.md)
- [Running them](running_them.md)
