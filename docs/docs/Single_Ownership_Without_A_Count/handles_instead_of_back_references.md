# Handles instead of back-references

A `weak` back-reference needs a count to know when its target is gone, so single ownership does without it. `mem::Arena<T>` takes its place: it owns its entries and hands out small `Slot` handles that stay valid until an entry is removed. A handle to a removed slot is caught by a generation stamp rather than dangling — so a graph or a cache keeps arena handles where it would have kept weak pointers.

**A generational handle**

```rune
import std::io
import std::mem

fn main() -> i64 {
    var a = mem::Arena<String>()
    let h = a.insert("first")
    a.remove(h)
    io::println(a.get(h).isNil().$str())    // true: the handle went stale
    0
}
```
