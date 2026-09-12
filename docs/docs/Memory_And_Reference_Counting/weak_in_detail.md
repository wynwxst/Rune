# `weak`, in detail

Reference counting cannot collect a cycle: two objects that point at each other keep each other's count at one forever. Break the cycle by marking the back-reference `weak`. A weak field does not raise the count, and it is set to `nil` the moment its target is released — so reading it can never hand you a dead object.

**A weak back-reference**

```rune
import std::io
import std::process

class Parent {
    name: String
    child: Child?
    fn init(self, name: String) { self.name = name; self.child = nil }
    fn deinit(self) { io::println("drop parent") }
}

class Child {
    // Without `weak` this pair would keep each other alive forever.
    weak owner: Parent?
    fn init(self) { self.owner = nil }
    fn deinit(self) { io::println("drop child") }
}

fn main() -> i64 {
    {
        let p = Parent("configuration")
        let c = Child()
        p.child = c
        c.owner = p          // weak: does not retain
        match c.owner {
            Some(up) => io::println("child belongs to " + up.name)
            None => io::println("orphaned")
        }
    }
    let remaining = process::liveObjectCount()
    io::println("live after scope: " + remaining.$str())
    0
}
```

**A weak field zeroes itself**

```rune
import std::io

class Cache {
    label: String
    fn init(self, label: String) { self.label = label }
}

class Watcher {
    weak target: Cache?
    fn init(self) { self.target = nil }
}

fn main() -> i64 {
    let w = Watcher()
    {
        let c = Cache("hot")
        w.target = c
        io::println("while alive: " + w.target.hasValue().$str())
    }
    // `c` is gone, so the weak slot was zeroed for us.
    io::println("after release: " + w.target.hasValue().$str())
    0
}
```

> [!WARNING]
> **weak implies optional**
>
> `weak` needs an optional type: the field has to be able to hold `nil`, because that is what it becomes.
