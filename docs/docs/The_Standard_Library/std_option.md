# std::option

Every method below is an ordinary method on an ordinary enum. The sugar — `T?`, `nil`, `??`, `?` — is only in the spellings.

| Member | Signature | Does |
| --- | --- | --- |
| `Option<T>` | `enum { Some(T), None }` | a value or nothing |
| `hasValue` | `(&self) -> bool` | true when `Some` |
| `isNil` | `(&self) -> bool` | true when `None` |
| `isSuchThat` | `(&self, @function(T) -> bool) -> bool` | true when present *and* it passes |
| `or` | `(self, fallback: T) -> T` | the value, or the fallback |
| `orElse` | `(self, @function() -> T) -> T` | same, producing the fallback only if needed |
| `unwrap` | `(self) -> T` | the value; **aborts** on `None` |
| `expect` | `(self, message: CString) -> T` | same, with your message |
| `map` | `<U>(self, @function(T) -> U) -> Option<U>` | the value transformed; empty passes through |
| `mapOr` | `<U>(self, fallback: U, @function(T) -> U) -> U` | `map` then `or`, in one step |
| `andThen` | `<U>(self, @function(T) -> Option<U>) -> Option<U>` | `map` for a function that may itself come up empty |
| `filter` | `(self, @function(T) -> bool) -> Option<T>` | the value, but only if it passes |
| `otherwise` | `(self, other: Option<T>) -> Option<T>` | this one if present, else the other; stays an `Option` |
| `zip` | `<U>(self, Option<U>) -> Option<(T, U)>` | both as a pair, or nothing |
| `take` | `(&var self) -> Option<T>` | hands the value over and leaves this one empty |
| `replace` | `(&var self, value: T) -> Option<T>` | stores one, returns what was there |
| `clear` | `(&var self)` | empties it |
| `some` / `none` | `<T>(...) -> Option<T>` | constructors |
| `when` | `<T>(bool, T) -> Option<T>` | the value if the condition holds |
| `flatten` | `<T>(Option<Option<T>>) -> Option<T>` | one out of two |

**Option without unwrapping it**

```rune
import std::io

fn main() -> i64 {
    let found: i64? = 7

    // A chain of these reads as one calculation rather than four nil checks:
    // the empty case passes straight through every step.
    io::println(found.map(||(v: i64) -> String { "got " + v.$str() }) ?? "none")
    io::println((found.filter(||(v: i64) -> bool { v > 100 }) ?? -1).$str())
    io::println(found.mapOr(0, ||(v: i64) -> i64 { v * 2 }).$str())

    // `take` empties as it hands over, which is how a field that owns
    // something is moved out of.
    var pending: String? = "job"
    let claimed = pending.take()
    io::println((claimed ?? "-") + " / " + (pending ?? "-"))
    0
}
```
