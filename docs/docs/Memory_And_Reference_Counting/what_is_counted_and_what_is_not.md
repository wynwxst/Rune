# What is counted, and what is not

Only *classes* carry a reference count. Integers, floats, `bool`, `Character`, tuples, structs, arrays and enums are values: assigning one copies it, and it dies with the scope that named it. `String` is counted too, but its buffer is an implementation detail you never see.

| Kind | Storage | Assignment | Counted |
| --- | --- | --- | --- |
| `i64`, `f64`, `bool`, `Character` | inline | copies | no |
| `struct` | inline | copies every field | no |
| `enum` | inline (tag + payload) | copies the payload | no |
| `[N:T]` | inline | copies every element | no |
| tuple | inline | copies every member | no |
| `class` | heap | shares the same object | yes |
| `String` | heap | shares the same buffer | yes |

*A struct holding a class field is itself a value, but copying it retains the class the field points at.*
