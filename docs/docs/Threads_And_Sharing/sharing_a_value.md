# Sharing a value

`Arc<T>` is one value several threads may read. A class already is a shared reference, but a *mutable* one, which is why no class is `Send`; `Arc` is the immutable counterpart — what it holds is set when it is made, and everything after that is a read. It is `Sync` when `T` is, since an `Arc` around something writable would only move the problem down a level.

**One value, two readers**

```rune
import std::io
import std::thread

fn total(shared: &thread::Arc<[4:i64]>) -> i64 {
    var sum = 0
    for v in shared.look() { sum += v }
    sum
}

fn main() -> i64 {
    let table = thread::Arc<[4:i64]>([2, 3, 5, 7])
    let both = thread::scope(table, ||(s: &thread::Scope<thread::Arc<[4:i64]>>) -> String {
        var here = s.spawn(total, s.env())
        var there = s.spawn(total, s.env())
        let sum = here.join() + there.join()
        sum.$str() + " and the original still has " +
            s.env().look().$length().$str() + " entries"
    })
    println!("{}", both)
    0
}
```

| Want | Reach for |
| --- | --- |
| read the same value from several threads | `Arc<T>` |
| change the same value from several threads | `Mutex<T>` |
| hand values from one thread to another | `Channel<T>` |
