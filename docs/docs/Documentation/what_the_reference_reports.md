# What the reference reports

Everything public, and everything about one thing in one place. A type’s fields, variants, associated types, methods and bindings are rendered beneath it rather than given pages of their own, because that is where a reader looks for them.

| Reported | Sits under |
| --- | --- |
| modules | the top of the page |
| `class`, `struct`, `enum`, `mark` | their module |
| fields, variants, associated types, methods | the type they belong to |
| `bind Mark to Type` | the type, when it is one this package documents; the module otherwise — `bind i64 into Value` (the same as `bind As<Value> to i64`) is still public API |
| `fn`, `macro`, `global`, `type` aliases | their module |

A declaration with nothing written about it is one line: its signature already names it, and a heading above that would say the same word twice. One with prose gets a heading, because that is what a reader scans for. Non-public declarations get nothing — `init` is not documentation.
