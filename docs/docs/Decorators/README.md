# Decorators

A decorator is a compiler instruction attached to a declaration, written `@name` or `@name(arguments)`. They never change what code means — only how it is compiled, checked, or exposed.

```ebnf
decorator  ::= "@" identifier [ "(" argument { "," argument } ")" ]
             // `@alias("...")` adds a name; `@intrinsic("...")` has no body
argument   ::= expression | identifier ":" expression
```

| Decorator | Applies to | Effect |
| --- | --- | --- |
| `@unsafe` | function | the body may perform unchecked operations; calling it is unsafe |
| `@safe("reason")` | function | a checked interface over an unchecked implementation; records why |
| `@inline` | function | asks the optimiser to inline it |
| `@noinline` | function | forbids inlining |
| `@export("name")` | function | gives it that exact symbol name, for C to call |
| `@alias("name")` | any declaration | another name for it; a string, so it can hold what an identifier cannot |
| `@as("name")` | an `extern` declaration | renames it for Rune's side only; the symbol stays what C exports |
| `@resource` | a field | the type's `deinit` has to release it, or say so at `--safety full` |
| `@intrinsic("name")` | function | the compiler answers it directly; how `std::mem` gets `size_of` |
| `@sync("reason")` | a class | it synchronises its own access, so it may cross a thread |
| `@Doc("...")` | any declaration | prose the compiler keeps; what `rune doc` reads |
| `@type(Executable \| Library \| …)` | **a file** | what that file produces |
| `@link("m")` | **a file** | a native library this file needs |
| `@linkpath("/opt/lib")` | **a file** | where to look for them |

*Unknown decorators are diagnosed, not ignored — a typo in a decorator name is a mistake worth hearing about.*

## Pages

- [Safety decorators](safety_decorators.md)
- [`@alias` and `@as`](alias_and_as.md)
- [Code generation decorators](code_generation_decorators.md)
- [Decorators you write yourself](decorators_you_write_yourself.md)
- [`@alias`: a second name](alias_a_second_name.md)
- [`@type`: what a file produces](type_what_a_file_produces.md)
- [`@link` and `@linkpath`: what a file needs](link_and_linkpath_what_a_file_needs.md)
- [Exporting to C](exporting_to_c.md)
- [Spelling and placement](spelling_and_placement.md)
