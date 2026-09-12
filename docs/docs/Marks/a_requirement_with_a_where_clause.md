# A requirement with a `where` clause

A requirement may carry type parameters and a clause of its own, and so may a mark's default. The clause is enforced wherever the method is called — through the type, and through the mark, where only the requirement is in view.

**Clauses on a requirement and on a default**

```rune
import std::io

mark Show { fn show(&self) -> String }
bind Show to i64 { fn show(&self) -> String { self.$str() } }

mark Sink {
    /// A requirement with a parameter and a bound of its own.
    fn accept<T>(&var self, value: T) -> String where T: Show

    /// A default may carry one too, and call the requirement under it.
    fn acceptTwice<T>(&var self, value: T) -> String where T: Show {
        self.accept(value) + self.accept(value)
    }
}

/// A clause may be about the mark's own associated type rather than a
/// parameter.
mark Holder {
    type Item
    fn render(&self) -> String where Self::Item: Show
}

struct Log { var lines: i64 }
bind Sink to Log {
    fn accept<T>(&var self, value: T) -> String where T: Show {
        self.lines += 1
        value.show()
    }
}

struct Box { v: i64 }
bind Holder to Box {
    type Item = i64
    fn render(&self) -> String where Self::Item: Show { self.v.show() }
}

/// Called through the mark, so the requirement's clause is what is checked.
fn record<S: Sink>(s: &var S, value: i64) -> String { s.accept(value) }

fn main() -> i64 {
    var l = Log { lines: 0 }
    io::println(l.acceptTwice(7))
    io::println(record(&var l, 9))
    io::println(Box { v: 5 }.render())
    io::println(l.lines)
    0
}
```

**The clause is what refuses this**

```rune
import std::io

mark Show { fn show(&self) -> String }
mark Sink { fn accept<T>(&var self, value: T) -> String where T: Show }

struct Opaque { n: i64 }
struct Log { }
bind Sink to Log {
    fn accept<T>(&var self, value: T) -> String where T: Show { value.show() }
}

fn main() -> i64 {
    var l = Log { }
    io::println(l.accept(Opaque { n: 1 }))
    0
}
```

> [!NOTE]
> **Asking for more**
>
> An implementation may ask for *more* than the mark promised — a bind whose `accept` says `where T: Show, T: Total` compiles — but the extra bound is then enforced at every call, including the ones that only knew about the mark.
