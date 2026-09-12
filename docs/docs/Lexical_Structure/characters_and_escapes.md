# Characters and escapes

A `Character` is one Unicode scalar, written in single quotes.

**Character literals and escapes**

```rune
import std::io

fn main() -> i64 {
    let letter = 'R'
    let newline = '\n'
    let tab = '\t'
    let quote = '\''
    let backslash = '\\'
    let nul = '\0'
    let escape = '\e'
    let hexByte = '\x41'
    let greek = 'λ'
    let scalar = '\u{1F600}'

    io::println(letter)
    io::println(hexByte)
    io::println(greek)
    io::println(scalar)
    io::println("tab between:[" + tab.$str() + "]")
    io::println(nul.$str().$length())
    io::println(quote.$str() + backslash.$str() + newline.$str().$length().$str() +
                escape.$str().$length().$str())
    0
}
```

| Escape | Meaning |
| --- | --- |
| `\n` `\t` `\r` | newline, tab, carriage return |
| `\0` | the NUL scalar |
| `\\` `\"` `\'` | backslash, double quote, single quote |
| `\e` | escape (0x1B), handy for terminal output |
| `\xNN` | one byte from exactly two hex digits |
| `\u{...}` | any Unicode scalar up to `\u{10FFFF}` |
