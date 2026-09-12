# When a type answers for itself

A type's own methods — its body and its `extend` blocks — always win `value.name()`. A `bind` fills in what the type does not already have; it never displaces it, and says so if you write one that would.

**Both are reachable**

```rune
import std::io

struct Ticket { number: i64 }

extend Ticket {
    pub fn label(&self) -> String { "#" + self.number.$str() }
}

mark Formal {
    fn label(&self) -> String
    fn heading(&self) -> String { "-- " + self.label() + " --" }
}

bind Formal to Ticket {
    fn label(&self) -> String { "Ticket number " + self.number.$str() }
}

fn main() -> i64 {
    let t = Ticket { number: 42 }
    io::println(t.label())             // the type's own
    io::println(t::Formal.label())     // the mark's, asked for by name
    io::println(t.heading())           // a default, calling the mark's `label`
    0
}
```

| Written | Finds |
| --- | --- |
| `value.name()` | the type's own method, or a mark's if it has none |
| `value::Mark.name()` | the method `Mark` binds for this type |
| `self.name()` inside a bound method | that mark's own `name` |
| `Mark::name(...)` | the static requirement, for the type in context |

*`value::Mark.name()` takes a plain name on the left — bind an expression to one first.*

A default body always calls the mark's own requirements, not the type's lookalikes. It was written against the mark, so that is what it gets — which is why `heading` above reads *Ticket number 42* rather than *#42*.

A field and a method may share a name. The syntax settles it: `self.name` is the field, `self.name()` the method — unless the field itself holds something callable, which then wins.

**A field and a method, one name**

```rune
import std::io

struct Sheep { name: String }

mark Named { fn name(&self) -> String }

bind Named to Sheep {
    // `self.name` reads the field; the mark's requirement is this method.
    fn name(&self) -> String { "the sheep called " + self.name }
}

fn main() -> i64 {
    let s = Sheep { name: "dolly" }
    io::println(s.name)      // the field
    io::println(s.name())    // the method
    0
}
```
