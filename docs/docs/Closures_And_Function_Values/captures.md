# Captures

A closure copies whatever it uses from the enclosing scope at the moment it is created. Reference-counted captures are retained for as long as the closure lives, so the closure can outlive the block that made it.

**Capture happens at creation**

```rune
import std::io

fn makeAdder(amount: i64) -> @function(i64) -> i64 {
    ||(n: i64) -> i64 { n + amount }      // `amount` is captured by value
}

fn main() -> i64 {
    let addTen = makeAdder(10)
    io::println(addTen(5))

    var scale = 3
    let triple = ||(n: i64) -> i64 { n * scale }
    scale = 100                            // too late: the copy was taken
    io::println(triple(7))

    let prefix = "value: "
    let show = ||(n: i64) -> String { prefix + n.$str() }
    io::println(show(42))
    0
}
```

> [!NOTE]
> **By value, always**
>
> Because captures are copies, a closure never observes a later change to the variable it captured. That also means a closure can be returned safely.

Captures are **copied** when the closure is made — writing `move` in front says so, and changes nothing. What the closure holds is its own; the enclosing scope carries on independently.

**`move` names what already happens**

```rune
import std::io

fn main() -> i64 {
    var n = 1
    let copied = move ||() -> i64 { n }
    n = 99
    io::println(copied().$str())     // still 1
    0
}
```

Which means assigning to a captured name changes only the closure's copy. That is easy to write by accident and impossible to notice at run time, so the compiler says so.

**Assigning to a capture is a copy**

```rune
fn main() -> i64 {
    var total = 0
    let add = ||(v: i64) -> () { total += v }
    add(5)
    total
}
```

To share one value, capture something that *is* shared: a class is a reference, so every copy of it names the same object. `mem::Handle<T>` is that, for a single value.

**Sharing one value with a closure**

```rune
import std::io
import std::mem

fn main() -> i64 {
    let total = mem::of(0)
    let add = ||(v: i64) -> () { *total += v }
    add(5)
    add(7)
    io::println((*total).$str())
    0
}
```
