# Precedence

Tighter binding first. Everything on one row associates left to right, except `??`, which associates right, and assignment, which associates right.

| Level | Operators | Kind |
| --- | --- | --- |
| tightest | `f(x)` &nbsp; `a[i]` &nbsp; `a.b` &nbsp; `a?` &nbsp; `a.await` | postfix |
|  | `-a` &nbsp; `!a` &nbsp; `~a` &nbsp; `&a` &nbsp; `&var a` &nbsp; `*a` | prefix |
|  | `a as T` &nbsp; `a is T` | cast and type test |
| 10 | `*` &nbsp; `/` &nbsp; `%` | multiplicative |
| 9 | `+` &nbsp; `-` | additive |
| 8 | `<<` &nbsp; `>>` | shift |
| 7 | `&` | bitwise and |
| 6 | `^` | bitwise xor |
| 5 | `\|` | bitwise or |
| 4 | `==` &nbsp; `!=` &nbsp; `<` &nbsp; `<=` &nbsp; `>` &nbsp; `>=` | comparison |
| 3 | `&&` | logical and, short-circuiting |
| 2 | `\|\|` | logical or, short-circuiting |
| 1 | `??` | nil coalescing, right associative |
| 0 | `..` &nbsp; `..=` | range |
| loosest | `=` and every compound form | assignment, right associative |

*Operator precedence, tightest first*

> [!WARNING]
> **Unlike C**
>
> Bitwise operators bind **tighter** than comparison, so `flags & MASK == 0` parses as `flags & (MASK == 0)` would in C — except Rune rejects that outright, because `MASK == 0` is a `bool`. Write the parentheses.
