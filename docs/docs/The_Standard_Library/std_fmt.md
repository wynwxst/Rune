# std::fmt

| Function | Signature | Does |
| --- | --- | --- |
| `show` | `<T: Display>(value: T) -> String` | what `{}` does |
| `fixed` | `(value: f64, places: i64) -> String` | `{:.N}` — that many places, rounded |
| `radix` | `<T>(value: T, base: i64, upper: bool, prefix: bool) -> String` | `{:x}`, `{:b}`, `{:o}` |
| `plus` | `(text: String) -> String` | `{:+}` — a leading `+` where there is no sign |
| `pad` | `(text: String, width: i64, align: Character, fill: Character) -> String` | `{:>8}` — widen to `width`, counting characters |

*What a `format!` placeholder expands into. Each is an ordinary function, usable on its own.*
