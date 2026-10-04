# Decorators

A decorator is attached to a declaration. The compiler's own are written `#name` or `#name(arguments)` and change how code is compiled, checked or exposed; one the program declares is written `@name` and runs code of the program's own. The sigil tells a reader which is which at a glance.

```ebnf
decorator  ::= ( "#" | "@" ) identifier [ "(" argument { "," argument } ")" ]
             // `#` for the compiler's own, `@` for one the program declares
             // `#alias("...")` adds a name; `#intrinsic("...")` has no body
argument   ::= expression | identifier ":" expression
```

| Sigil | Means | If it is the other kind |
| --- | --- | --- |
| `#name` | a decorator the compiler provides — everything in the table below | `#route` names nothing the compiler knows: refused (E0132), with the `@route` spelling suggested |
| `@name` | a function the program declared, called before `main`; see below | `@inline` still works and warns (W0133), saying to write `#inline` |

| Decorator | Applies to | Effect |
| --- | --- | --- |
| `#unsafe` | function | the body may perform unchecked operations; calling it is unsafe |
| `#safe("reason")` | function | a checked interface over an unchecked implementation; records why |
| `#inline` | function | asks the optimiser to inline it |
| `#noinline` | function | forbids inlining |
| `#export("name")` | function | gives it that exact symbol name, for C to call |
| `#alias("name")` | any declaration | another name for it; a string, so it can hold what an identifier cannot |
| `#as("name")` | an `extern` declaration | renames it for Rune's side only; the symbol stays what C exports |
| `#resource` | a field | the type's `deinit` has to release it, or say so at `--safety full` |
| `#intrinsic("name")` | function | the compiler answers it directly; how `std::mem` gets `size_of` |
| `#sync("reason")` | a class | it synchronises its own access, so it may cross a thread |
| `#Doc("...")` | any declaration | prose the compiler keeps; what `rune doc` reads |
| `#type(Executable \| Library \| …)` | **a file** | what that file produces |
| `#link("m")` | **a file** | a native library this file needs |
| `#linkpath("/opt/lib")` | **a file** | where to look for them |
| `#macro` | a `pub fn` | a procedural macro, built and run while compiling; see **Macros** |
| `#Config(condition)` | any declaration or member | it exists only when the condition holds; see **Conditional compilation** |
| `#Convention("C")` | a struct or enum | laid out the way C lays it out; see **Calling C** |
| `#auto` / `#never(Mark)` | a mark / a type | the mark is decided by a type's parts; or this type never has it; see **Automatic marks** |
| `#zombie_unavailable("use …")` | any declaration | it only makes sense with reference counting; under `--memory zombie` a use is refused with that advice |
| `#weak` | a function | a definition another can replace at link time; see **Bare metal** |
| `#runtime(none)` / `#entry(none)` / `#panicHandler` / `#output` / `#allocator` / `#deallocator` | a file / functions | a freestanding program and the hooks it supplies; see **Bare metal** |
| `#lint(allow(…), warn(…), note(…))` | a file, declaration or statement | sets lint rules for that item — or for the whole file when it stands at the top; the compiler ignores it, `rune lint` and the editor read it |

*Unknown decorators are diagnosed, not ignored — a typo in a decorator name is a mistake worth hearing about.*

## Pages

- [Safety decorators](safety_decorators.md)
- [`#alias` and `#as`](alias_and_as.md)
- [Code generation decorators](code_generation_decorators.md)
- [Decorators you write yourself](decorators_you_write_yourself.md)
- [`#alias`: a second name](alias_a_second_name.md)
- [`#type`: what a file produces](type_what_a_file_produces.md)
- [`#link` and `#linkpath`: what a file needs](link_and_linkpath_what_a_file_needs.md)
- [Exporting to C](exporting_to_c.md)
- [Spelling and placement](spelling_and_placement.md)
