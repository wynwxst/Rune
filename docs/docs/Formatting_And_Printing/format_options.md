# Format options

A `:` inside a placeholder is followed by how to render it: width and alignment, a number of places, or a radix.

**Every option, at work**

```rune
fn main() -> i64 {
    // Width, and which side the text is held to. A character before the
    // alignment is the fill.
    println!("[{:>8}] [{:<8}] [{:^8}] [{:*^9}]", "ab", "ab", "ab", "ab")

    // `0` fills with zeros, and keeps the sign in front of them.
    println!("[{:08}] [{:08}]", 42, -42)

    // Places after the point, alone and with a width.
    println!("{:.4}  [{:10.2}]  [{:08.2}]", 3.14159265, 3.14159, -3.14159)

    // Radix. `#` adds the prefix, and zeros go inside it.
    println!("{:x} {:X} {:b} {:o}", 255, 255, 10, 64)
    println!("{:#x} {:#b} {:#06x}", 255, 10, 255)

    // An explicit sign on anything that has none of its own.
    println!("{:+} {:+} {:+.2}", 5, -5, 1.5)
    0
}
```

| Option | Means | Example |
| --- | --- | --- |
| `<` `^` `>` | align left, centre, right | `{:>8}` |
| *char* before the align | what to pad with | `{:*^9}` |
| `0` | pad with zeros, inside the sign and any `0x` | `{:08}` |
| *number* | the minimum width, in characters | `{:8}` |
| `.`*number* | places after the point | `{:.2}` |
| `x` `X` | hexadecimal, lower or upper case | `{:x}` |
| `b` `o` | binary, octal | `{:b}` |
| `#` | the `0x`, `0b` or `0o` prefix | `{:#x}` |
| `+` | a leading `+` when there is no sign | `{:+}` |

The order inside a placeholder is `{[argument][:[[fill]align][+][#][0][width][.places][kind]]}`. Width counts characters rather than bytes, so a column of accented text lines up.

> [!NOTE]
> **Checked where it is written**
>
> A number of places and a radix cannot both apply, and neither can a kind the list above does not have. Both are errors at the `format!`, not surprises at run time.
