# The pieces underneath

A placeholder expands into a call from `std::fmt`, which is an ordinary module: the functions are there to be used directly when a format string is not the clearest way to say something.

| Placeholder | Expands to |
| --- | --- |
| `{}` | `std::fmt::show(x)` |
| `{:.2}` | `std::fmt::fixed(x, 2)` |
| `{:#x}` | `std::fmt::radix(x, 16, false, true)` |
| `{:+}` | `std::fmt::plus(std::fmt::show(x))` |
| `{:*^9}` | `std::fmt::pad(std::fmt::show(x), 9, '^', '*')` |

> [!NOTE]
> **Errors point back**
>
> An error inside an expansion shows what the macro stood for, so a type mismatch in a `{:.2}` names `fmt::fixed` and the argument it was given. See **Macros**.
