# Ownership

An `Any` owns what it holds. Boxing retains, and releasing the `Any` releases the value; `get` and `expect` hand back a copy, retained. So a value lives exactly as long as the `Any` holding it, not as long as the expression that built it.

**The session closes with the `Any`**

```rune
import std::io

class Session {
    pub user: String
    fn init(self, user: String) { self.user = user }
    fn deinit(self) { io::println("closed " + self.user) }
}

fn open() {
    let held: Any = Session("ada")
    io::println("holding a " + held.typeName())
    io::println("user is " + held.expect::<Session>().user)
}

fn main() -> i64 {
    open()
    io::println("past it")
    0
}
```

> [!NOTE]
> **Note**
>
> A `Unique<T>` cannot go into an `Any`: the `Any` would be its second owner, and a `Unique` has room for one.
