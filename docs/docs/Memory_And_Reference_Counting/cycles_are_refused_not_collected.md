# Cycles are refused, not collected

Reference counting frees an object when the last reference to it goes. A ring of objects holding each other never reaches zero, so it never gets freed — the one way a fully safe program could still leak. There is no cycle collector; instead, at `--safety full` the compiler refuses the shape that allows one.

**A ring the compiler reports**

```rune
class Parent {
    child: Child?
    fn init(self) { self.child = nil }
}

class Child {
    owner: Parent?      // strong both ways: this is the shape that leaks
    fn init(self) { self.owner = nil }
}

fn main() -> i64 { 0 }
```

The check follows what an object *owns* — its fields, and through structs, tuples, arrays, enum payloads and Options. It stops at anything that does not keep its target alive, so a `weak` field, a borrow or a raw pointer is not an edge.

| Shape | At `--safety full` |
| --- | --- |
| `A.b: B` and `B.a: A` | reported |
| `N.me: N?` | reported |
| a ring of three | reported |
| through a struct field | reported |
| through an enum payload | reported |
| through an array element | reported |
| `weak` on either edge | silent |
| `Unique` on the owning edge | silent |
| `&T` or `*T` — a borrow owns nothing | silent |

**One weak edge, and it frees**

```rune
import std::io
import std::process

class Parent {
    name: String
    child: Child?
    fn init(self, name: String) { self.name = name; self.child = nil }
    fn deinit(self) { io::println("drop parent") }
}

class Child {
    weak owner: Parent?     // one weak edge is enough
    fn init(self) { self.owner = nil }
    fn deinit(self) { io::println("drop child") }
}

fn main() -> i64 {
    let before = process::liveObjectCount()
    {
        let p = Parent("configuration")
        let c = Child()
        p.child = c
        c.owner = p
        match c.owner {
            Some(up) => io::println("child sees parent: " + up.name),
            None => io::println("orphaned"),
        }
    }
    // Bound before printing: calling `liveObjectCount()` inside a
    // concatenation counts the half-built string too.
    let after = process::liveObjectCount()
    io::println("balanced " + (before == after).$str())
    0
}
```

> [!WARNING]
> **It reports on types, not on programs**
>
> The rule looks at **types**, not at the objects you actually build. A forward-linked `class Node { next: Node? }` is reported even when you only ever build a chain, because nothing in the type stops the last node pointing back at the first. That is why it is a warning and not a refusal: a type that *can* loop is not a program that does, and refusing the shape would refuse every owning list and tree with it.

When a structure should be incapable of looping at all, do not argue with the warning — own it through `Unique`, below, and there is nothing left to warn about.

> [!WARNING]
> **The remaining hole**
>
> One thing the rule cannot see: a closure's captures are not part of its type, so a class holding a closure that captures that same class is a ring the check will not catch. The exit report still finds it.
