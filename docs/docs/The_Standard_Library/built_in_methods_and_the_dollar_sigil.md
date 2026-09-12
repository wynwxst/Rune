# Built-in methods and the `$` sigil

These are not library functions — the compiler knows them, and they answer questions about a value rather than doing anything a library could. They are written with a `$`, which is what keeps them out of the way of the methods your own types declare: a type is free to have its own `length` or `str`, and neither name can ever shadow the other.

| Receiver | Method | Result |
| --- | --- | --- |
| `String` | `$length()` / `$len()` | `i64` — bytes |
| `String` | `$charCount()` | `i64` — characters |
| `String` | `$isEmpty()` | `bool` |
| `String` | `$at(i)` | `Character` |
| `String` | `$byteAt(i)` | `u8` |
| `String` | `$substring(from, to)` | `String` |
| `String` | `$find(needle)` | `i64`, `-1` when absent |
| `String` | `$repeat(n)` | `String` |
| `String` | `$toInt()` | `Option<i64>` |
| `String` | `$toFloat()` | `Option<f64>` |
| `String` | `$cstr()` | `CString` for C |
| `String` | `$hash()` | `u64` |
| `[N:T]`, `[T]` | `$length()` / `$len()` | `i64` |
| `[N:T]`, `[T]` | `$isEmpty()` | `bool` |
| any scalar | `$str()` | `String` |

**String methods**

```rune
import std::io

fn main() -> i64 {
    let text = "hello, world"
    io::println(text.$length().$str())
    io::println(text.$substring(7, 12))
    io::println(text.$find("world").$str())
    io::println("-".$repeat(12))
    io::println(text.$at(0).$str())
    io::println("120".$toInt().or(0).$str())
    io::println("not a number".$toInt().hasValue().$str())
    0
}
```

Because the two namespaces are separate, a type can answer to both spellings without either shadowing the other.

**Both namespaces at once**

```rune
import std::io

struct Tag { text: String }

extend Tag {
    // The same names the compiler uses, and no conflict.
    pub fn length(&self) -> String { "a tag" }
    pub fn str(&self) -> i64 { self.text.$length() }
}

fn main() -> i64 {
    let t = Tag { text: "hello" }
    io::println(t.length())            // the type's own
    io::println(t.str().$str())        // the type's own, then the compiler's
    io::println(t.text.$length().$str())
    0
}
```

**Forgetting the sigil**

```rune
fn main() -> i64 {
    let values: [3:i64] = [1, 2, 3]
    values.length()
}
```
