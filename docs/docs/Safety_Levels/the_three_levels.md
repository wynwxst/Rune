# The three levels

| `--safety` | Bounds | Nil | Division by zero | Leak report | Strong cycles |
| --- | --- | --- | --- | --- | --- |
| `full` *(default)* | checked | checked | checked | yes | **refused** |
| `minimal` | — | checked | — | yes | warned |
| `none` | — | — | — | — | warned |

*Set it per build with `runec --safety <level>` or per package with `safety = "..."` under `[build]`.*

At `full`, a failed check aborts with a diagnostic naming the file and line — not undefined behaviour, and not a silent wrong answer.

**A bounds check firing**

```rune
import std::io

fn main() -> i64 {
    let values: [4:i64] = [1, 2, 3, 4]
    var index = 0
    // The compiler cannot see how far this goes, so the check stays in.
    while index < 6 {
        io::println(values[index].$str())
        index += 1
    }
    0
}
```

An index the compiler *can* see is out of range never gets that far — it is a compile error, at every safety level.

**A statically known overrun**

```rune
fn main() -> i64 {
    let values: [3:i64] = [1, 2, 3]
    values[7]
}
```
