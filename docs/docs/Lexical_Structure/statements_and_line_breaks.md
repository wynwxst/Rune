# Statements and line breaks

Semicolons are optional. A line break ends a statement when the token before it *could* end one — an identifier, a literal, a closing bracket, or a keyword like `return`. Otherwise the statement carries on to the next line.

**Three ways a statement can end**

```rune
import std::io

fn main() -> i64 {
    // `+` cannot end a statement, so this is one expression.
    let total = 1 +
                2 +
                3

    // A line starting with `.` continues the previous expression.
    let text = "chained"
        .$repeat(2)

    // An explicit `;` ends a statement wherever you want it to.
    let a = 1; let b = 2

    io::println(total.$str() + " " + text + " " + (a + b).$str())
    0
}
```

> [!NOTE]
> **Grouping**
>
> Inside `(` … `)` and `[` … `]`, line breaks are never terminators, so argument lists and array literals can span as many lines as you like.

The `;` also decides whether a trailing expression is the block's value or a discarded statement — see **Blocks are expressions**.
