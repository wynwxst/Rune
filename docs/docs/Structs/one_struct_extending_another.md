# One struct extending another

`struct Derived : Base` puts the parent's fields at the **front** of the child's. So a `Derived` is a `Base` with more on the end, and the bytes a `Base` occupies are the first bytes of one: the parent's methods work on the child, and the child reads as a `Base` wherever one is wanted.

**Fields first, methods along with them**

```rune
import std::io

struct Base { pub id: i64 = 0, pub name: String = "" }

extend Base {
    fn label(&self) -> String { self.name + "#" + self.id.$str() }
    fn bump(&var self) { self.id += 1 }
}

struct Derived : Base { pub extra: i64 = 0 }
struct Deeper : Derived { pub more: String = "" }

fn describe(b: Base) -> String { b.label() }

fn main() -> i64 {
    var d = Derived { id: 7, name: "seven", extra: 3 }
    io::println(d.label())          // the parent's method
    d.bump()                        // including one that writes
    io::println(describe(d.$clone()))   // read as a `Base`

    let deep = Deeper { id: 1, name: "one", extra: 2, more: "yes" }
    io::println(deep.label())
    0
}
```

|  | Struct | Class |
| --- | --- | --- |
| written | `struct D : B` | `class D : B` |
| fields | spliced in, the parent's first | kept in the parent, reached through it |
| reading as the parent | a copy of the prefix | the same object |
| how deep | as many levels as you like, either way | as many levels as you like, either way |

*A value has no indirection to walk, so its parent's fields are spliced in once and everything downstream sees one flat type.*

> [!NOTE]
> **Two limits**
>
> A child may add fields, not redefine the parent's, and the parent may not be generic — its members are spliced in as they are written, and nothing would say what its parameters were.
