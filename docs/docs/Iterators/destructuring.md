# Destructuring

The loop binding is a pattern. It has to match every value, because every turn has to bind — a pattern that could fail is refused, and the value should be taken apart with `match` inside the body instead.

**Patterns in the loop head**

```rune
import std::io

struct Entry { pub key: String, pub value: i64 }

fn main() -> i64 {
    let pairs: [2:(i64, String)] = [(1, "one"), (2, "two")]
    for (n, name) in pairs { io::println(n.$str() + " = " + name) }

    let entries: [2:Entry] = [
        Entry { key: "a", value: 1 },
        Entry { key: "b", value: 2 },
    ]
    for Entry { key, value } in entries { io::println(key + " -> " + value.$str()) }

    let nested: [2:((i64, i64), String)] = [((1, 2), "first"), ((3, 4), "second")]
    for ((a, b), label) in nested {
        io::println(label + ": " + (a * b).$str())
    }

    var turns = 0
    for _ in pairs { turns += 1 }
    io::println(turns)
    0
}
```

**A pattern that could fail**

```rune
import std::io

enum Colour { Red, Green }

fn main() -> i64 {
    let cs: [2:Colour] = [Colour::Red, Colour::Green]
    for Colour::Red in cs { io::println("red") }
    0
}
```
