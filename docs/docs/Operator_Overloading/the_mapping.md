# The mapping

A word, a bracket name, or the punctuation itself in quotes — `operator::index`, `operator::LeftSquareBracket` and `operator::"[]"` are three ways to write one thing. The method inside is what actually claims the operator, so a single block may carry more than one.

| Operator | Method | Also written |
| --- | --- | --- |
| `+` `-` `*` `/` `%` | `add` `sub` `mul` `div` `rem` | `Plus` `Minus` `Star` `Slash` `Percent`, or `"+"` … `"%"` |
| `&` `\|` `^` | `bitand` `bitor` `bitxor` | `Ampersand` `Pipe` `Caret`, or `"&"` `"^"` |
| `<<` `>>` | `shl` `shr` | `ShiftLeft` `ShiftRight` |
| `==` `!=` | `eq` | `EqualEqual` `Equals`, or `"=="` |
| `<` `<=` `>` `>=` | `cmp` | `LessThan` `Compare`, or `"<"` |
| `-a` | `neg` | `Negate` |
| `!a` | `not` | `Bang`, or `"!"` |
| `~a` | `bitnot` | `Tilde`, or `"~"` |
| `a[i]` | `index` | `LeftSquareBracket` `Subscript`, or `"[]"` |
| `*a` | `deref` | `Asterisk`, or `"*"` |
| `*a = v` | `derefSet` | `AsteriskEquals` |

*Operator to method name*

> [!NOTE]
> **Two comparison protocols**
>
> `eq` returns a `bool` and also answers `!=`, inverted. `cmp` returns a negative number, zero or a positive number and answers all four relational operators.
