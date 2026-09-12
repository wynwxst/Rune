# Borrows and ownership

`full` also reads each function body for what it does with what it owns. This is a whole-body pass rather than a run-time check, so it costs nothing at run time and reports before the program is built. Below `full` each finding is a warning instead, and the build carries on.

| Reported | Because |
| --- | --- |
| A borrow returned from the function it points into | the binding is destroyed on the way out, so the caller would be handed an address to nothing |
| Two live borrows of one place, one of them able to write | a writer has to be the only one |
| A value that owns a resource read out of a field by value | that would make a second owner, and the resource would be handed back twice |
| A use of a binding that has been handed away | it no longer refers to anything |

*What the ownership pass reports at `--safety full`*

**A borrow that outlives what it borrows**

```rune
fn dangling() -> &i64 {
    let n = 5
    &n
}

fn main() -> i64 { *dangling() }
```

**Two borrows, one of them mutable**

```rune
struct Point { var x: i64, var y: i64 }

fn main() -> i64 {
    var p = Point { x: 1, y: 2 }
    let a = &var p
    let b = &p
    a.x + b.y
}
```

A borrow stops mattering after its last mention rather than at the end of the block, so finishing with one and then reading the value again is fine.

**A borrow that has been finished with**

```rune
import std::io

struct Point { var x: i64, var y: i64 }

fn main() -> i64 {
    var p = Point { x: 1, y: 2 }
    {
        var a = &var p
        a.x = 3
    }
    let b = &p              // the first borrow is finished with
    io::println(b.x)
    0
}
```

> [!NOTE]
> **How precise it is**
>
> A borrow is followed back to the binding it starts from, so two *fields* of the same value count as the same place. Following an index the compiler cannot evaluate would report the same conflicts with less certainty, so it does not try; the diagnostic says as much rather than leaving the reader to guess.
