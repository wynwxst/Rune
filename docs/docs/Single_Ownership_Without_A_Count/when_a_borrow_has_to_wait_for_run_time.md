# When a borrow has to wait for run time

Sometimes two parts of a program genuinely reach one value and the compiler cannot see that only one touches it at a time. `mem::Checked<T>` moves the check to run time: `borrow()` hands out a read-only `Ref`, `borrowVar()` the one writable `RefVar`, and asking for a conflicting one aborts rather than letting them race. It keeps no count — one state word — so it reads the same in both modes, and it is what the exclusive-borrow diagnostics point to.

**A borrow decided as the program runs**

```rune
import std::io
import std::mem

fn main() -> i64 {
    let cell = mem::Checked<i64>(0)
    { var w = cell.borrowVar(); *w = 41; *w = *w + 1 }   // exclusive, checked
    io::println((*cell.borrow()).$str())                  // 42
    0
}
```
