# Internal references

A field may borrow from another field of the same value, written `from self.field`. Such a struct owns everything it needs: it can be moved, returned and passed on, because moving it moves the handle to the borrowed data, not the data itself. The borrowed-from field has to own something on the heap — a `String`, a class — and be declared before the field that points into it.

**A struct that borrows from itself**

```rune
import std::io

struct Message {
    text: String
    body: &String from self.text     // points into this value's own `text`
}

fn parse(text: String) -> Message {
    let body = &text
    Message { text: text, body: body }
}

fn main() -> i64 {
    let m = parse("hello world")     // moved out of `parse`, borrow and all
    io::println(m.body.$length().$str())   // 11
    0
}
```
