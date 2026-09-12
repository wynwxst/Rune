# Extending a builtin, without overriding it

A builtin's own meanings are the language's and stay that way — but the pairs it has *no* meaning for are yours to give. `String + String` is built in and cannot be replaced; `String + Character` is not, so the standard library defines it:

**Appending a character**

```rune
import std::io

fn main() -> i64 {
    var text = ""
    text += 'a'          // String + Character, from std::io
    text += 'b'
    io::println(text)
    0
}
```

The rule is one sentence: **an overload may teach a type to work with another type, but never replace what the language already does.** Anything that would shadow a builtin meaning is refused rather than silently ignored.

**Redefining `String + String`**

```rune
bind operator::add to String {
    fn add(&self, rhs: String) -> String { self }
}

fn main() -> i64 { 0 }
```
