# Consuming an Option

**Every way to get at the value**

```rune
import std::io

fn lookup(key: String) -> i64? {
    if key == "a" { return 1 }
    if key == "b" { return 2 }
    nil
}

fn main() -> i64 {
    // A default, two ways.
    io::println(lookup("a") ?? -1)
    io::println(lookup("z") ?? -1)
    io::println(lookup("z").or(-1))

    // Questions.
    io::println(lookup("a").hasValue())
    io::println(lookup("z").isNil())

    // Taking the value out, when you know it is there.
    io::println(lookup("b").unwrap())

    // Matching, which handles both cases at once.
    match lookup("b") {
        Some(v) => io::println("found " + v.$str()),
        None => io::println("nothing"),
    }

    // Or the two-case form.
    if lookup("a") is Some(v) {
        io::println("also found " + v.$str())
    }
    0
}
```

| Method | Result |
| --- | --- |
| `hasValue()` | `bool` — true when a value is present |
| `isNil()` | `bool` — the opposite |
| `or(fallback)` | the value, or `fallback` |
| `unwrap()` | the value; aborts when empty |
| `expect(message)` | the value; aborts with `message` when empty |

*Option's methods*

**`unwrap` on an empty Option aborts**

```rune
import std::io

fn main() -> i64 {
    let empty: i64? = nil
    io::println("about to unwrap")
    io::println(empty.unwrap())
    0
}
```
