# The macros the compiler supplies

Two macros cannot be written as rules, because both need to see something a macro body no longer can. They are built in for that reason and no other.

| Macro | Needs to see |
| --- | --- |
| `stringify!(...)` | the argument tokens as they were spelled |
| `format!("...", ...)` | inside the format string literal |

`format!` is covered in **Formatting**; `println!` and `print!` are ordinary `pub macro`s written in terms of it, which is the usual shape: one built-in doing the part rules cannot, and rules on top.

**`println!`, as the standard library writes it**

```text
pub macro println {
    () => { std::io::println("") }
    ($($arg: expr),+) => { std::io::println(format!($($arg),+)) }
}
```
