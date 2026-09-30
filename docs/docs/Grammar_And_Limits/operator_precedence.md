# Operator precedence

|  | Operators | Associativity |
| --- | --- | --- |
| 1 | `.` `::` `()` `[]` `?` `.await` | left |
| 2 | `-` `!` `~` `&` `&var` `*` (prefix) | right |
| 3 | `as` `into` | left |
| 4 | `*` `/` `%` | left |
| 5 | `+` `-` | left |
| 6 | `<<` `>>` | left |
| 7 | `&` | left |
| 8 | `^` | left |
| 9 | `\|` | left |
| 10 | `..` `..=` | none |
| 11 | `<` `<=` `>` `>=` | left |
| 12 | `==` `!=` | left |
| 13 | `is` | left |
| 14 | `&&` | left |
| 15 | `\|\|` | left |
| 16 | `??` | right |
| 17 | `=` and every compound assignment | right |

*Tightest first. `..` does not associate: write parentheses if you meant to nest ranges.*
