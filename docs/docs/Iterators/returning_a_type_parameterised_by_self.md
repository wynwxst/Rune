# Returning a type parameterised by `Self`

`map` returns `Map<Self, B>` and `filter` returns `Filter<Self>`. Every type carrying the mark gets those methods, and the wrappers carry the mark too — so resolving the results for every type would never finish. They are resolved where the method is *called*, which is why `Map<Filter<Ticks>, String>` exists exactly when a chain asks for it. Nothing has to be declared to get this; it is how such a signature is handled, and it works for a mark of your own just as well.

**A mark of your own, doing the same thing**

```rune
import std::io

struct Boxed<I> { pub inner: I }

mark Wrapping {
    fn step(&var self) -> i64?

    // The result wraps `Self`, and `Boxed<I>` carries the mark as well.
    fn boxed(self) -> Boxed<Self> { Boxed<Self> { inner: self } }
}

bind<I: Wrapping> Wrapping to Boxed<I> {
    fn step(&var self) -> i64? { self.inner.step() }
}

struct Ticks { pub left: i64 }
bind Wrapping to Ticks {
    fn step(&var self) -> i64? {
        if self.left <= 0 { return nil }
        self.left -= 1
        self.left
    }
}

fn main() -> i64 {
    // Three deep, because this line asks for three — and no deeper.
    var b = (Ticks { left: 2 }).boxed().boxed().boxed()
    io::println(b.step().or(-1))
    io::println(b.step().or(-1))
    io::println(b.step().or(-1))
    0
}
```

> [!NOTE]
> **Note**
>
> What has no end is a type parameterised by a *deeper* version of itself — `struct Nest<T> { deeper: Nest<Nest<T>>? }`. That is reported rather than run out of stack. A recursive structure holds itself, `Nest<T>?`, which is fine.

A function that *returns* a chain does not have to write the wrapper type out. `-> some Iterator` is the spelling: the body still produces one concrete type, callers see only the mark, and there is no box. See [Opaque results: `some Mark`](#marks).
