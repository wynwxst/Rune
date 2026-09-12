# Labelled arguments

**Labels may be given in any order**

```rune
import std::io

// A label makes the call read like a sentence, and stops two arguments of the
// same type from being swapped by accident.
fn transfer(source: i64, target: i64, amount: i64) -> String {
    "moved " + amount.$str() + " from " + source.$str() +
    " to " + target.$str()
}

fn main() -> i64 {
    io::println(transfer(source: 1, target: 9, amount: 3))
    io::println(transfer(amount: 3, target: 9, source: 1))   // any order
    0
}
```

Any argument may be passed by name. Labels and positions mix freely, and a label lets you skip past a default.

**Naming arguments at the call site**

```rune
import std::io

fn box(text: String, width: i64 = 10, pad: String = "-") -> String {
    pad.$repeat(width) + text + pad.$repeat(width)
}

fn main() -> i64 {
    io::println(box("all positional", 3, "="))
    io::println(box("all labelled", width: 3, pad: "="))
    io::println(box("mixed", pad: "*"))          // skips `width`
    io::println(box(text: "reordered", pad: "+", width: 2))
    0
}
```

**A label that does not exist**

```rune
fn greet(name: String, greeting: String = "hello") -> String {
    greeting + ", " + name
}

fn main() -> i64 {
    greet(nmae: "typo")
    0
}
```
