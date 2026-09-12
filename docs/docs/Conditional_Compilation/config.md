# `@Config`

Write the condition on the declaration. Two definitions of one name under conditions that cannot both hold are the ordinary way to give a function a different body per platform.

**One name, one definition per family**

```rune
import std::io

@Config(family == "unix")
fn lineEnding() -> String { "\n" }

@Config(family == "windows")
fn lineEnding() -> String { "\r\n" }

fn main() -> i64 {
    io::println("bytes in a line ending: " + lineEnding().$length().$str())
    0
}
```

> [!NOTE]
> **Gone, not unused**
>
> A ruled-out declaration is removed after parsing and before anything is collected, so its body is never name-resolved and never type-checked. It still has to *parse* — it is Rune, not text — but it may call functions that exist on no other platform.
