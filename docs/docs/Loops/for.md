# `for`

**Three ways round a sequence**

```rune
import std::io

fn main() -> i64 {
    var line = ""
    for i in 0..4 { line += i.$str() }
    io::println(line)

    let names: [3:String] = ["ada", "grace", "alan"]
    for n in names { io::print(n + " ") }
    io::newline()

    var i = 0
    while i < names.$length() {
        io::println(i.$str() + ": " + names[i])
        i += 1
    }
    0
}
```

`for` walks a range, an array or a slice natively, and anything bound to `Iterator` or `Sequence` besides — see [Iterators](#iterators). The loop variable is a pattern, so it can destructure as it goes.

**Ranges, arrays, slices and patterns**

```rune
import std::io

fn main() -> i64 {
    var sum = 0
    for i in 1..=4 { sum += i }
    io::println(sum)

    let values: [4:i64] = [10, 20, 30, 40]
    var total = 0
    for v in values { total += v }
    io::println(total)

    // Over a slice, including one taken from an array.
    var half = 0
    for v in values[0..2] { half += v }
    io::println(half)

    // The binding is a pattern.
    let points: [3:(i64, i64)] = [(1, 2), (3, 4), (5, 6)]
    var crossSum = 0
    for (x, y) in points { crossSum += x * y }
    io::println(crossSum)
    0
}
```
