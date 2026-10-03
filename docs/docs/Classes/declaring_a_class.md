# Declaring a class

`init` is the constructor and `deinit` the destructor; both are optional. Calling the class runs `init`.

**Construction, use, destruction**

```rune
import std::io

class Buffer {
    pub name: String
    pub used: i64

    fn init(self, name: String) {
        self.name = name
        self.used = 0
    }

    fn deinit(self) {
        io::println("  releasing " + self.name)
    }

    pub fn write(&var self, amount: i64) {
        self.used += amount
    }

    pub fn describe(&self) -> String {
        self.name + " holds " + self.used.$str()
    }
}

fn main() -> i64 {
    let b = Buffer("frame")
    b.write(3)
    b.write(4)
    io::println(b.describe())
    io::println("leaving main")
    0
}
```

> [!NOTE]
> **No initialiser**
>
> A class with no `init` takes no arguments. Field defaults still apply, and they run before `init` does, so `init` can overwrite them.
