# Reserved words

These 37 words are keywords and cannot be used as identifiers.

| Group | Words |
| --- | --- |
| Declarations | `fn` `struct` `enum` `class` `mark` `bind` `to` `extend` `import` `extern` `type` `pub` `global` |
| Bindings | `let` `var` `mut` |
| Control flow | `if` `elif` `else` `while` `loop` `for` `in` `match` `return` `break` `continue` `defer` |
| Types and values | `self` `Self` `super` `dyn` `weak` `true` `false` `nil` |
| Other | `as` `is` `where` `unsafe` `operator` |

`Option`, `Result`, `Some`, `None`, `Ok` and `Err` are not keywords — they are ordinary declarations the compiler happens to know by name, and a module may shadow them.
