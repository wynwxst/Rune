# Automatic marks

Some marks are not promises a type makes but facts about it: it holds nothing that has to be destroyed, everything in it can be copied, nothing in it stops it crossing to another thread. `@auto` says so, and the compiler answers for every type: a type has an automatic mark when **every part of it** has it.

**A mark the compiler answers for**

```rune
import std::io

/// A claim about a type, not a promise it makes: it holds plain values.
@auto
mark Plain {}

struct Point { x: i64, y: i64 }        // has it: two integers
struct Pair { first: Point, at: (i64, bool) }   // has it: so do its parts

fn describe<T: Plain>(value: &T) -> String { "plain" }

fn main() -> i64 {
    let p = Point { x: 1, y: 2 }
    io::println(describe(&p))
    0
}
```

An automatic mark carries no requirements — there is nobody to implement them, since nobody writes the binding. What it carries is the rule, and two ways to override it where the structure has nothing to say.

| Written | Means |
| --- | --- |
| `@auto mark M {}` | M is automatic: every part decides |
| `bind M to T {}` | T has it, whatever its parts say |
| `@never(M)` on `T` | T does not have it, whatever its parts say |
| `reflect::conforms<T, M>()` | the answer, at compile time |

Three things never have one on their own. A type that runs a `deinit` — a destructor is a promise the compiler cannot read. Anything it cannot look into: a closure and its captures, an `Any`, a `dyn Mark`, a raw or `weak` pointer. And a type that refuses it with `@never`, along with everything holding one.

**What a `deinit` costs**

```rune
@auto
mark Plain {}

struct Descriptor { fd: i32 }
extend Descriptor {
    fn deinit(&var self) { }
}

fn describe<T: Plain>(value: &T) -> String { "plain" }

fn main() -> i64 {
    let d = Descriptor { fd: 3 }
    describe(&d)
    0
}
```

> [!NOTE]
> **Claiming one**
>
> `bind M to T {}` is the escape hatch, and it is deliberately a line of code: claiming that a type has a mark its parts do not is exactly the kind of thing that should be written down. It is how `String` comes by `mem::Clone` — a string's contents never change, so a copy of one is a copy.

`std::mem::Clone` is the automatic mark the standard library ships. `std::thread::Send` and `std::thread::Sync` answer the same way, with rules of their own about references and shared mutable objects — see *Threads and sharing*.
