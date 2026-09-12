# Sharing a value

`Arc<T>` is one value several threads may read. A class already is a shared reference, but a *mutable* one, which is why no class is `Send`; `Arc` is the immutable counterpart — what it holds is set when it is made, and everything after that is a read. It is `Sync` when `T` is, since an `Arc` around something writable would only move the problem down a level.

**One value, two readers**

```rune
import std::io
import std::thread

fn total(shared: thread::Arc<[4:i64]>) -> i64 {
    var sum = 0
    for v in shared.get() { sum += v }
    sum
}

fn main() -> i64 {
    let table = thread::Arc<[4:i64]>([2, 3, 5, 7])
    var here = thread::spawn(total, table)
    var there = thread::spawn(total, table)
    println!("{} and the original still has {} entries",
             here.join() + there.join(), table.get().$length())
    0
}
```

| Want | Reach for |
| --- | --- |
| read the same value from several threads | `Arc<T>` |
| change the same value from several threads | `Mutex<T>` |
| hand values from one thread to another | `Channel<T>` |
