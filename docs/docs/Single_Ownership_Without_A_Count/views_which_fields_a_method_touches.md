# Views: which fields a method touches

A `&var self` method that only touches some of the object's fields can say so with a view, `{ field, ... }` after the receiver. A caller may then hold a borrow of another field across the call. The checker infers a view for every method on its own; writing one pins it as part of the interface and lets a caller be checked without reading the body.

**A view keeps a method out of the fields it does not name**

```rune
import std::io

class Ledger {
    var total: i64
    var note: String
    fn init(self) { self.total = 0; self.note = "" }

    // Promises to touch `total` and nothing else.
    fn add(&var self { total }, n: i64) { self.total += n }

    fn label(&self) -> &String from self { &self.note }
}

fn main() -> i64 {
    var l = Ledger()
    let tag = l.label()     // a borrow of `note`
    l.add(10)               // touches only `total`: allowed alongside `tag`
    io::println(tag + " " + l.total.$str())
    0
}
```
