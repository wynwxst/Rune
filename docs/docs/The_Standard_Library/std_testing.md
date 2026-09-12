# std::testing

Assertions for the programs under `tests/`. Each check prints its own line, so a failure names itself instead of leaving you to work out which of twenty assertions went wrong, and `summary()` returns what `main` should — which is the verdict `rune test` reads. There is a section of its own on [testing](#testing).

| Function | Passes when |
| --- | --- |
| `equal(name, got, want)` | the two render the same |
| `notEqual(name, got, want)` | they do not |
| `isTrue(name, cond)` / `isFalse` | the condition holds, or does not |
| `isSome(name, opt)` / `isNone` | the Option holds something, or nothing |
| `unreachable(name)` | never — for a branch that should not run |
| `counts()` | — returns `(passed, failed)` |
| `summary()` | — prints the tally, returns 0 or 1 |
