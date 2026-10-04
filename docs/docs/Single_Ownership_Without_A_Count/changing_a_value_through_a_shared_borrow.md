# Changing a value through a shared borrow

A shared borrow promises that nothing changes underneath it — with one exception the language makes on purpose. A `mem::Cell<T>` can be changed through `&self`: `set` and `replace` work on a cell that is only lent out, so a hit counter in a struct that is passed around by `&`, or the cursor of an allocator every container shares, can still move. What keeps that sound is what a cell never does: hand out a borrow of what is inside. `get` gives a copy and `replace` gives the old value back, so there is no reference into the cell for a change to pull the rug from under.

**A counter behind a shared borrow**

```rune
import std::io
import std::mem

struct Stats { hits: mem::Cell<i64> }

fn record(s: &Stats) { s.hits.set(s.hits.get() + 1) }

fn main() -> i64 {
    let stats = Stats { hits: mem::cell<i64>(0) }
    record(&stats)
    record(&stats)
    io::println(stats.hits.get().$str())               // 2
    io::println(stats.hits.replace(10).$str())         // 2, and now 10
    0
}
```

A cell is one thread's: two threads setting one would race. `thread::Mutex` and `atomic::Counter` are built on it, and are what to share across threads. A cell is also what a method on `&self` should reach for rather than casting `&self.field` to a `*var T` and writing through it: the cast is unsafe, and says nothing the cell does not.
