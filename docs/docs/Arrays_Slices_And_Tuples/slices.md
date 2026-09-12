# Slices

**One function, any run of elements**

```rune
import std::io

// A slice is the shape to write against: one function, arrays of any length,
// and any run inside one.
fn mean(values: [f64]) -> f64 {
    if values.$isEmpty() { return 0.0 }
    var total = 0.0
    for v in values { total += v }
    total / (values.$length() as f64)
}

fn main() -> i64 {
    let week: [7:f64] = [3.0, 4.0, 2.0, 8.0, 6.0, 1.0, 4.0]
    io::println(mean(week).$str())
    io::println(mean(week[0..5]).$str())
    io::println(mean(week[5..]).$str())
    0
}
```

`[T]` is a pointer and a length. An array converts to a slice on its own, and `values[a..b]` borrows part of one.

**Every slicing form**

```rune
import std::io

fn total(values: [i64]) -> i64 {
    var sum = 0
    for v in values { sum += v }
    sum
}

fn main() -> i64 {
    let all: [6:i64] = [1, 2, 3, 4, 5, 6]

    io::println(total(all))              // the array decays to a slice
    io::println(total(all[0..3]))
    io::println(total(all[3..6]))
    io::println(total(all[0..=2]))       // inclusive
    io::println(total(all[2..]))         // to the end
    io::println(total(all[..2]))         // from the start
    io::println(total(all[..]))          // the whole thing

    let middle = all[1..5]
    io::println(middle.$length())
    io::println(total(middle[1..3]))     // slicing a slice
    io::println(middle.$isEmpty())
    0
}
```

> [!NOTE]
> **Borrowed, not owned**
>
> A slice borrows: it does not own the storage, and it does not copy. Slicing is bounds checked in the same way indexing is.
