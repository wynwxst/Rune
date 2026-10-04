# Reserved words

These 47 words are keywords and cannot be used as identifiers.

| Group | Words |
| --- | --- |
| Declarations | `fn` `struct` `enum` `class` `mark` `bind` `to` `extend` `import` `extern` `type` `pub` `global` `macro` |
| Bindings | `let` `var` `mut` |
| Control flow | `if` `elif` `else` `while` `loop` `for` `in` `match` `return` `break` `continue` `defer` `async` `await` |
| Types and values | `self` `Self` `super` `dyn` `weak` `uniq` `true` `false` `nil` |
| Other | `as` `into` `is` `where` `unsafe` `operator` `move` |

`move` is a keyword only where it is an operator — `move value`, or `move ||` before a closure. Followed by `(` it is a name like any other, so a game's `fn move(&var self, dx: i64, dy: i64)` and the call `piece.move(1, 0)` are both fine.

A few more words mean something only in one position, and are ordinary names everywhere else: `take` at the start of a pattern binding, `some` and `typeof` where a type is written, and `from` after a borrowed type in a signature.

`Option`, `Result`, `Some`, `None`, `Ok` and `Err` are not keywords — they are ordinary declarations the compiler happens to know by name, and a module may shadow them.
