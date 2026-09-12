# Values move; borrows look

Anything the heap owns — a class, a `String`, a closure, a mark object — has one owner. Assigning it, passing it by value, returning it or capturing it by value all move it, and the binding it came from is empty afterwards. A borrow, `&x` or `&var x`, reaches the value without owning it, and takes nothing away.

**A value borrowed, then moved**

```rune
import std::io

class Greeting {
    text: String
    fn init(self, text: String) { self.text = text }
}

// `&self`: borrows the greeting, so the caller keeps it.
fn shout(g: &Greeting) -> String { g.text + "!" }

fn main() -> i64 {
    let hello = Greeting("hello")
    io::println(shout(&hello))      // borrowed: `hello` is still ours
    let moved = hello               // moved: `hello` is empty from here
    io::println(moved.text)
    0
}
```

Use a value after it has been moved and the checker stops the build, naming where it went.

**Use after move**

```rune
import std::io

class Box { var v: i64  fn init(self, v: i64) { self.v = v } }
fn take(b: Box) -> i64 { b.v }

fn main() -> i64 {
    let x = Box(1)
    let a = take(x)         // `x` is moved into `take`
    let b = take(x)         // and cannot be used again
    io::println((a + b).$str())
    0
}
```

When a copy is what you meant, ask for one. `$clone()` builds a second value that owns everything the first did — a fresh `String`, a new object with each field cloned in turn.

**An explicit copy**

```rune
import std::io

class Box { var v: i64  fn init(self, v: i64) { self.v = v } }

fn main() -> i64 {
    let a = Box(7)
    let b = a.$clone()      // an independent copy
    b.v = 8
    io::println(a.v.$str() + " " + b.v.$str())   // 7 8
    0
}
```
