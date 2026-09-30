# Three owning pointers, and what tells them apart

`std::mem` offers three. They differ in one thing only — how many places may own the value — and that decides everything else about them.

| Type | Owners | Costs | Made by |
| --- | --- | --- | --- |
| `Handle<T>` | as many as share the handle | a class, so a reference count | `mem::of(v)` |
| `Box<T>` | exactly one | one machine word, no bookkeeping | `mem::boxed(v)` |
| `Rc<T>` | as many as ask, each by cloning | two counts in the block it allocates | `mem::shared(v)` |
| `Weak<T>` | none — it watches | a share of the same block | `rc.downgrade()` |

*A `Box` is a value with a destructor, so it is moved rather than copied, and the one place holding it frees it.*

`Rc` keeps its counts in fields of its own rather than leaving them to the compiler, which is what makes it mean the same thing under `--memory zombie`, where nothing is counted for you. It is the way two places share a value there.

**One owner, several owners, and a watcher**

```rune
import std::io
import std::mem

struct Point { x: i64, y: i64 }

fn main() -> i64 {
    // One owner, moved rather than copied.
    var b = mem::boxed(Point { x: 1, y: 2 })
    b.x = 10
    io::println(b.x.$str())

    // As many owners as ask, each by cloning.
    let a = mem::shared("hello")
    let second = a.$clone()
    io::println(a.strongCount().$str())     // 2
    io::println(*second)

    // A watcher that does not keep it alive.
    var watcher: mem::Weak<String>
    {
        let held = mem::shared("gone soon")
        watcher = held.downgrade()
        io::println(watcher.isAlive().$str())
    }
    io::println(watcher.isAlive().$str())
    0
}
```
