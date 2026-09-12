# std::result

| Member | Signature | Does |
| --- | --- | --- |
| `Result<T, E>` | `enum { Ok(T), Err(E) }` | success or failure |
| `isOk` / `isErr` | `(&self) -> bool` | which one it is |
| `isSuchThat` | `(&self, @function(T) -> bool) -> bool` | succeeded *and* the value passes |
| `or` | `(&self, fallback: T) -> T` | the value, or the fallback |
| `orElse` | `(&self, @function() -> T) -> T` | same, producing the fallback only if needed |
| `recover` | `(&self, @function(E) -> T) -> T` | the value, or what the error is turned into |
| `unwrap` | `(&self) -> T` | the value; **aborts** on `Err` |
| `expect` | `(&self, message: CString) -> T` | same, with your message |
| `unwrapErr` | `(&self) -> E` | the error; **aborts** on `Ok` |
| `map` | `<U>(&self, @function(T) -> U) -> Result<U, E>` | the value transformed; an error passes through |
| `mapErr` | `<F>(&self, @function(E) -> F) -> Result<T, F>` | the error transformed — a conversion local to one call, when a `bind As` would be too wide |
| `andThen` | `<U>(&self, @function(T) -> Result<U, E>) -> Result<U, E>` | what `?` does, without the early return |
| `ok` | `(&self) -> Option<T>` | the success as an `Option` |
| `error` | `(&self) -> Option<E>` | the failure as an `Option` |
| `ok` / `err` | `<T, E>(...) -> Result<T, E>` | constructors |
| `from` | `<T, E>(Option<T>, error: E) -> Result<T, E>` | an empty lookup made into one that says why |
| `flatten` | `<T, E>(Result<Result<T, E>, E>) -> Result<T, E>` | one out of two |

> [!NOTE]
> **Errors convert through `As`**
>
> `?` hands the error out as the function's own error type. When the two differ, `bind Theirs into Ours` (the same as `bind As<Ours> to Theirs`) says how to get from one to the other and `?` calls its `convert` on the way out — the same `As` that `into` dispatches through. `mapErr` is still there for a conversion that is local to one call. See [`?` converts the error](#option-result).

**Result in practice**

```rune
import std::io

fn halve(n: i64) -> Result<i64, String> {
    if n % 2 != 0 { return Err(n.$str() + " is odd") }
    Ok(n / 2)
}

fn main() -> i64 {
    io::println(halve(84).unwrap().$str())
    io::println(halve(7).or(-1).$str())
    io::println(halve(7).error().or("none").$str())
    io::println(halve(84).ok().hasValue().$str())
    0
}
```
