# std::collections::slice

Everything you can do with a run of values you did not allocate. Every function takes a slice, so an array works too. The compiler provides `$length` and `$isEmpty`, and `values[a..b]` slices one; the rest is here.

| Function | Signature | Does |
| --- | --- | --- |
| `at` | `([T], i64) -> T?` | checked access — nothing when out of range |
| `first` / `last` | `([T]) -> T?` |  |
| `indexWhere` | `([T], @function(T) -> bool) -> i64?` | index of the first match |
| `firstWhere` | `([T], @function(T) -> bool) -> T?` | the first match itself |
| `anyOf` / `allOf` | `([T], @function(T) -> bool) -> bool` | one matches / every one does |
| `countWhere` | `([T], @function(T) -> bool) -> i64` | how many match |
| `indexOf` / `contains` | `<T: Display>([T], T) -> ...` | search by value, compared as it prints |
| `minimumBy` / `maximumBy` | `([T], @function(T, T) -> bool) -> T?` | extremes by your ordering |
| `fold` | `([T], A, @function(A, T) -> A) -> A` | combine into one, left to right |
| `toVector` / `reversed` / `filtered` | `([T], ...) -> Vector<T>` | these allocate, which is why they say `Vector` |
| `sortedBy` | `([T], @function(T, T) -> bool) -> Vector<T>` | stable insertion sort, original untouched |
| `join` | `<T: Display>([T], String) -> String` | rendered and separated |
| `iterate` | `([T]) -> SliceIter<T>` | a cursor, so the `std::iter` adaptors chain |

> [!NOTE]
> **Why a slice needs `iterate` and a Vector does not**
>
> A slice indexes natively — `values[i]`, bounds checked — and `for value in values` needs nothing either. `iterate` exists only to reach `map`, `filter` and `zip`, which arrive through the `Iterator` mark: a mark cannot be bound to a builtin type, so the cursor is what carries them.

> [!NOTE]
> **Why a predicate, and why `Display`**
>
> The predicate forms exist because two values of an arbitrary `T` cannot be compared without knowing something about `T`. The `Display` forms compare values *as they print*, which is exact for every builtin and needs only a `Display` binding for anything else — there is no separate equality bound to satisfy.

**Arrays and slices**

```rune
import std::io
import std::collections::slice

fn main() -> i64 {
    let scores: [4:i64] = [3, 1, 4, 1]

    io::println(slice::join(scores, "-"))
    io::println(slice::contains(scores, 4).$str())
    io::println(slice::fold(scores, 0, ||(a: i64, n: i64) -> i64 { a + n }).$str())

    let ordered = slice::sortedBy(scores, ||(a: i64, b: i64) -> bool { a < b })
    var out = ""
    for n in ordered { out += n.$str() + " " }
    io::println(out)
    0
}
```
