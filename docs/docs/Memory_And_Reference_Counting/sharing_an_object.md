# Sharing an object

Two bindings that name the same instance see the same object. The count is how many bindings are alive, not how many were ever made.

**One object, two names**

```rune
import std::io
import std::process

class Node {
    label: String
    fn init(self, label: String) { self.label = label }
    fn deinit(self) { io::println("releasing " + self.label) }
}

fn main() -> i64 {
    // Measure before printing: building the message would itself allocate.
    let before = process::liveObjectCount()
    io::println("live before: " + before.$str())
    {
        let first = Node("root")
        let second = first        // no copy: both name one object
        second.label = "renamed"  // so this is visible through `first`
        io::println(first.label)
        let inside = process::liveObjectCount()
        io::println("live inside: " + inside.$str())
    }
    // Both bindings went out of scope, so the object is gone.
    let after = process::liveObjectCount()
    io::println("live after: " + after.$str())
    0
}
```

> [!NOTE]
> **Why 2**
>
> Two live objects inside the scope, not one: the `Node` and the `String` its `label` field holds. Strings are counted as well.
