# Slice and array patterns

`[a, b, c]` takes an array or a slice apart element by element. `..` stands for a run in the middle — any length, including none — and `..rest` binds that run as a slice into the same storage. The elements before `..` are matched from the front and the ones after it from the back, so `[first, .., last]` reaches both ends of anything with at least two.

**Matching a slice by shape**

```rune
import std::io

fn describe(xs: [i64]) -> String {
    match xs {
        [] => "empty",
        [x] => "one: " + x.$str(),
        [a, b] => "two: " + a.$str() + " " + b.$str(),
        [first, .., last] => "many: " + first.$str() + ".." + last.$str(),
    }
}

fn sum(xs: [i64]) -> i64 {
    match xs {
        [] => 0,
        [head, ..tail] => head + sum(tail),
    }
}

fn command(words: [String]) -> String {
    match words {
        ["go", ref dir] => "going " + dir,   // `ref`: borrowed, not moved out
        ["say", ..rest] => "saying " + rest.$length().$str() + " words",
        [.., "end"] => "ends with end",
        _ => "unknown",
    }
}

fn main() -> i64 {
    let empty: [0:i64] = []
    io::println(describe(empty))
    io::println(describe([7, 8]))
    io::println(describe([1, 2, 3, 4]))
    io::println(sum([1, 2, 3, 4]))
    let words: [2:String] = ["go", "north"]
    io::println(command(words))
    let more: [3:String] = ["say", "a", "b"]
    io::println(command(more))
    0
}
```

A slice is matched by length, and a `match` over one is exhaustive when every length has an arm: an arm without `..` covers one length exactly, and one with `..` covers every length from its minimum up. A fixed array's length is known, so a pattern that accounts for every element is irrefutable — it works in `let` and `for`, and one that cannot line up is an error rather than a test that fails.

**Irrefutable against a fixed array**

```rune
import std::io

fn main() -> i64 {
    let xs: [3:i64] = [1, 2, 3]
    let [a, b, c] = xs                 // the count matches, so it cannot fail
    io::println(a + b + c)
    let [head, ..tail] = xs
    io::println(head.$str() + " then " + tail.$length().$str() + " more")

    let pairs: [2:[2:i64]] = [[1, 2], [3, 4]]
    for [x, y] in pairs { io::println(x * 10 + y) }

    if xs is [1, ..rest] { io::println("starts with 1, then " + rest.$length().$str()) }
    0
}
```

**A count that cannot line up**

```rune
fn main() -> i64 {
    let xs: [3:i64] = [1, 2, 3]
    let [a, b] = xs
    a + b
}
```

**A length no arm accepts**

```rune
fn first(xs: [i64]) -> i64 {
    match xs {
        [] => 0,
        [a] => a,
        [a, b, c, ..] => a,
    }
}

fn main() -> i64 { first([1]) }
```
