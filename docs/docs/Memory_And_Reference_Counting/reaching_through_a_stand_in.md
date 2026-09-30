# Reaching through a stand-in

A pointer that held its value at arm's length would be tedious to use, so `.` reaches through it. What makes a type one of these is that it **lends**: a `look(&self) -> &T from self` to read the value where it lies, and a `touch(&var self) -> &var T from self` to write it. `Handle`, `Box`, `Rc` and the borrows `Checked` hands out all have them, and so may anything you write.

**`.` goes through to the value**

```rune
import std::io
import std::mem
import std::collections::vector

struct Point { x: i64, y: i64 }

extend Point {
    fn sum(&self) -> i64 { self.x + self.y }
    fn shift(&var self, by: i64) { self.x += by; self.y += by }
}

fn main() -> i64 {
    var b = mem::boxed(Point { x: 1, y: 2 })
    io::println(b.x.$str())         // through `look`
    io::println(b.sum().$str())     // through `look`
    b.x = 10                        // through `touch`
    b.shift(5)                      // through `touch`: `shift` takes `&var self`
    io::println(b.y.$str())

    // Whatever is inside keeps its own methods, however deep.
    var v = mem::boxed(vector::Vector<i64>())
    v.push(1)
    v.push(2)
    io::println(v.length().$str())
    0
}
```

> [!NOTE]
> **It cannot shadow**
>
> It can never hide anything. The reach-through only happens once a member has *not* been found on the stand-in itself, so `b.duplicate()` is still the box's own and only a name the box does not have goes through.

**An `Rc` lends for reading only**

```rune
import std::mem

struct Point { x: i64, y: i64 }

fn main() -> i64 {
    var shared = mem::shared(Point { x: 1, y: 2 })
    shared.x = 5
    0
}
```

An `Rc` has no `touch`, because several owners writing at once is the thing it exists to make impossible. A shared value that has to change keeps a `mem::Checked<T>` inside, which decides at run time that a write is the only one out.

**Changing what is shared**

```rune
import std::io
import std::mem

fn main() -> i64 {
    let cell = mem::shared(mem::Checked<i64>(0))
    let alias = cell.$clone()
    { var w = alias.look().borrowVar(); *w = 42 }
    io::println((*cell.look().borrow()).$str())
    0
}
```
