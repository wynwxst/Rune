# Handing an owning value on

Only one thing may own a resource, so giving one away is a *move*: returning it, passing it by value, or storing it somewhere that outlives the binding. The binding it came from stops owning it, and stops being usable.

**Moved, or borrowed**

```rune
import std::io

struct Handle { pub id: i64 }
extend Handle { fn deinit(&self) { io::println("closing " + self.id.$str()) } }

/// The local is returned, so this scope stops owning it: one `closing`, not
/// two.
fn make(n: i64) -> Handle {
    let h = Handle { id: n }
    io::println("made " + n.$str())
    h
}

fn consume(h: Handle) { io::println("consumed " + h.id.$str()) }
fn borrow(h: &Handle) { io::println("borrowed " + h.id.$str()) }

fn main() -> i64 {
    { let x = make(5); io::println("have " + x.id.$str()) }
    // Passing by value hands it over; the callee destroys it.
    { let a = Handle { id: 2 }; consume(a) }
    // Borrowing takes nothing, so the caller still destroys it.
    { let b = Handle { id: 3 }; borrow(&b) }
    0
}
```

**Using what has been handed away**

```rune
struct Handle { id: i64 }
extend Handle { fn deinit(&self) { } }

fn consume(h: Handle) { }

fn main() -> i64 {
    let a = Handle { id: 2 }
    consume(a)
    a.id
}
```

> [!NOTE]
> **What the check covers**
>
> A move is tracked per binding, and the check is textual rather than a walk of every path. What that misses — a move inside a loop that runs twice — is safe at run time: the binding carries a flag saying whether it still owns anything, so a destructor never runs twice.
