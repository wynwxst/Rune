# Splitting, trimming and replacing

The everyday half of `std::text`. These work on bytes rather than code points, and are built from the compiler's own `$find`, `$substring` and `$length` — which makes them exact for the ASCII delimiters a separator, a newline or a piece of punctuation almost always is, and makes them cheap. Anything that has to be right about accents or case belongs above, with the normalisation.

| Function | Signature | Does |
| --- | --- | --- |
| `startsWith` / `endsWith` | `(String, String) -> bool` | an empty affix always matches |
| `contains` | `(String, String) -> bool` | anywhere in it |
| `find` | `(String, String) -> i64?` | where — `nil` rather than `$find`'s -1 |
| `withoutPrefix` / `withoutSuffix` | `(String, String) -> String` | removed if present, unchanged if not |
| `trim` / `trimStart` / `trimEnd` | `(String) -> String` | whitespace off the ends |
| `split` | `(String, String) -> Vector<String>` | cut at every occurrence; adjacent separators give empty pieces |
| `splitLines` | `(String) -> Vector<String>` | at `\n`, dropping a trailing `\r`; no empty final line |
| `replace` | `(String, String, String) -> String` | every occurrence; the replacement is not searched again |
| `join` | `(Vector<String>, String) -> String` | the other half of `split` |
| `padStart` / `padEnd` | `(String, i64, Character) -> String` | to a width; longer text is returned rather than cut |
| `lower` / `upper` | `(String) -> String` | case, **ASCII only** — every other byte is left as it is |
| `isSpace` | `(byte: i64) -> bool` | space, tab, carriage return, newline or form feed — what `trim` strips |

> [!NOTE]
> **Why only ASCII**
>
> `lower` and `upper` deliberately stop at ASCII. Real case mapping depends on the language (Turkish dotless ı, German ß, Greek final sigma) and can change a string's length, so a function that quietly did the wrong thing for those would be worse than one that says what it does. Use them for keywords, extensions and protocol tokens; for anything a person reads, normalise first.

**Taking a line apart**

```rune
import std::io
import std::text

fn main() -> i64 {
    let line = "  name, age , city  "

    io::println("[" + text::trim(line) + "]")
    for field in text::split(text::trim(line), ",") {
        io::println("<" + text::trim(field) + ">")
    }

    io::println(text::replace("banana", "a", "o"))
    io::println(text::join(text::split("x;y;z", ";"), "-"))
    io::println(text::withoutSuffix("report.txt", ".txt"))
    io::println(text::padStart("7", 3, '0'))

    // `$find` answers -1 for "nowhere". `text::find` answers `nil`, which
    // cannot be mistaken for a position.
    io::println((text::find("hello", "llo") ?? -1).$str())
    io::println((text::find("hello", "zzz") ?? -1).$str())
    0
}
```
