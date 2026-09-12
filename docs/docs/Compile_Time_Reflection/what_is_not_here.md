# What is not here

There is no way to build a type, call a method by name, or enumerate what a program contains. Reflection here reads what the compiler already worked out; it is not a second, dynamic way to write programs. Where a set of types is closed, an enum says so and `match` checks you covered it; where they share behaviour, `dyn Mark` dispatches without asking what they are.

| Want | Reach for |
| --- | --- |
| What is this *value*? | `Any` — `holds`, `get`, `typeName` |
| What is this *type*? | `std::reflect` |
| Dispatch without knowing | `dyn Mark` |
| A closed set of shapes | an `enum` and `match` |
| Generate code per field | a macro; see **Macros** |
