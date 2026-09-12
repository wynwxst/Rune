# Bounds checking

With `--safety=full` — the default — every index is checked. The panic names the index and the length.

**An out-of-range index aborts**

```rune
import std::io

fn main() -> i64 {
    let values: [4:i64] = [1, 2, 3, 4]
    var index = 0
    for i in 0..9 { index = i }          // computed, so nothing is folded away
    io::println("reading index " + index.$str())
    io::println(values[index])
    0
}
```
