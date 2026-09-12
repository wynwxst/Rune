# Arrays

`[N:T]` is `N` values of `T`, stored inline. The length is part of the type and must be a constant expression.

**Literals, repeats, constant lengths, nesting**

```rune
import std::io

let ROWS: i64 = 3
let COLS: i64 = 4

fn main() -> i64 {
    let literal: [5:i64] = [1, 2, 3, 4, 5]
    let repeated = [7; 4]                      // four sevens
    let computed: [ROWS * COLS:i64] = [0; ROWS * COLS]
    let shifted: [1 << 3:u8] = [0; 8]
    var uninitialised: [3:f64]                 // zeroed
    let nested: [2:[3:i64]] = [[1, 2, 3], [4, 5, 6]]

    io::println(literal[0].$str() + " " + literal[4].$str())
    io::println(repeated.$length())
    io::println(computed.$length())
    io::println(shifted.$length())
    io::println(uninitialised[0])
    io::println(nested[1][2])

    var mutable: [3:i64] = [0, 0, 0]
    mutable[1] = 42
    io::println(mutable[1])
    0
}
```

**A length that is not constant**

```rune
fn main() -> i64 {
    var count = 4
    let dynamic: [count:i64] = [0; 4]
    0
}
```
