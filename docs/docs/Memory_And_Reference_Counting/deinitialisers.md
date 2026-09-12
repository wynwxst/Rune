# Deinitialisers

**Closing happens where you can see it**

```rune
import std::io

// `deinit` runs the moment the last reference does — not at some later
// collection — so closing happens where you can see it.
class Connection {
    name: String
    fn init(self, name: String) {
        self.name = name
        io::println("open " + name)
    }
    fn deinit(self) { io::println("close " + self.name) }
}

fn useOne() {
    let c = Connection("inner")
    io::println("  working with " + c.name)
}

fn main() -> i64 {
    let outer = Connection("outer")
    useOne()
    io::println("back in main")
    0
}
```

`deinit` runs when the last reference goes away. It runs before the object's own fields are released, and before the superclass's `deinit`, so a subclass always tears down before the base it was built on.

**Teardown runs subclass first**

```rune
import std::io

class Resource {
    name: String
    fn init(self, name: String) { self.name = name }
    fn deinit(self) { io::println("close " + self.name) }
}

class Pooled: Resource {
    index: i64
    fn init(self, name: String, index: i64) {
        super.init(name: name)
        self.index = index
    }
    fn deinit(self) { io::println("return slot " + self.index.$str()) }
}

fn main() -> i64 {
    let p = Pooled("socket", 3)
    io::println("using " + p.name)
    0
}
```

> [!NOTE]
> **deinit is not a method**
>
> A `deinit` takes no parameters, returns nothing, and cannot be called by hand. There is no way to run one early — drop the last reference instead.
