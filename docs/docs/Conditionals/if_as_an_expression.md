# `if` as an expression

**Choosing a value**

```rune
import std::io

fn grade(score: i64) -> String {
    if score >= 90 { "A" }
    elif score >= 80 { "B" }
    elif score >= 70 { "C" }
    else { "F" }
}

fn main() -> i64 {
    let scores: [4:i64] = [95, 83, 71, 40]
    var line = ""
    for s in scores { line += grade(s) + " " }
    io::println(line)
    0
}
```

Used for its value, an `if` must have an `else`, and both branches must produce the same type.

**Producing a value**

```rune
import std::io

fn main() -> i64 {
    let n = 7
    let label = if n % 2 == 0 { "even" } else { "odd" }
    let bigger = if n > 10 { n } else { 10 }

    // The branches can be whole blocks.
    let scaled = if n > 5 {
        let doubled = n * 2
        doubled + 1
    } else {
        0
    }

    io::println(label)
    io::println(bigger)
    io::println(scaled)
    0
}
```

**Branches must agree**

```rune
fn main() -> i64 {
    let n = 7
    let mixed = if n > 0 { 1 } else { "one" }
    0
}
```
