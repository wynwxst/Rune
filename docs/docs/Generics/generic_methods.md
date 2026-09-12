# Generic methods

A method may have type parameters of its own. They are inferred from the arguments where there is something to infer from, and written out where there is not.

**Type parameters on a method**

```rune
import std::io
import std::mem

class Box {
    pub tag: i64
    fn init(self, tag: i64) { self.tag = tag }

    pub fn pick<T>(&self, v: T) -> T { v }
    pub fn width<T>(&self) -> i64 { mem::size_of<T>() as i64 }
}

fn main() -> i64 {
    let b = Box(1)
    io::println(b.pick(3))              // T inferred from the argument
    io::println(b.pick("text"))
    io::println(b.width::<f64>())       // nothing to infer from
    io::println(b.width::<i32>())
    0
}
```

> [!NOTE]
> **Note**
>
> A generic method is dispatched from the type at the call site, never through a vtable: each set of arguments is a different function, so there is no single address a slot could hold.

**Nothing to infer from**

```rune
import std::mem

class Box {
    pub tag: i64
    fn init(self, tag: i64) { self.tag = tag }
    pub fn width<T>(&self) -> i64 { mem::size_of<T>() as i64 }
}

fn main() -> i64 {
    Box(1).width()
}
```
