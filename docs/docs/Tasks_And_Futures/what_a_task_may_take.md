# What a task may take

The body runs after the call that started it has returned — the caller may have moved on, returned, dropped its locals — so every parameter is captured into the task by value. A shared borrow of a class, `&Counter`, is the handle itself, kept alive by the capture, and is fine. A `&var` of anything, or a `&` of a value type, would point at a slot the caller has left, and is refused as `E0284`.

**A borrow that would dangle**

```rune
import std::task

async fn bump(count: &var i64) { *count += 1 }

fn main() -> i64 { 0 }
```

The same rule decides what `self` an `async` method may take: a class's `&self` or `&var self` is the object, and is fine; a struct's would be a pointer into the caller's slot, so an `async` method on a struct or an enum takes `self` by value.

**An async method**

```rune
import std::io
import std::task
import std::time

class Store {
    var prefix: String
    var count: i64
    fn init(self, prefix: String) { self.prefix = prefix; self.count = 0 }

    pub async fn load(&var self, id: i64) -> String {
        task::sleep(time::milliseconds(1)).await
        self.count += 1
        self.prefix + id.$str()
    }
}

async fn main() -> i64 {
    var store = Store("item")
    let a = store.load(1)
    let b = store.load(2)
    io::println(a.await + " " + b.await + " count=" + store.count.$str())
    0
}
```

`async fn` goes wherever `fn` goes: in a class, a struct, an `extend`, a mark's requirements and the `bind` that supplies them, a library's public interface. It cannot be an `init` or a `deinit`, which have to finish before the object exists and before it is gone.
