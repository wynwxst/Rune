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

`$clone()` is what a type's own `clone` is reached through, wherever that method is written — in the body, in an `extend`, or supplied by a `bind` — and however deep in the value it sits: a struct holding a `Vector` is cloned by cloning the vector, which copies its storage rather than handing out a second holder of the same block.

**A clone that reaches the parts**

```rune
import std::io
import std::collections::vector

struct Tally { counts: vector::Vector<i64>, name: String }

fn main() -> i64 {
    var one = Tally { counts: vector::Vector<i64>(), name: "first" }
    one.counts.push(1)

    var two = one.$clone()      // the vector is copied, not shared
    two.counts.push(2)

    io::println(one.counts.length().$str() + " and " +
                two.counts.length().$str())
    0
}
```

What the compiler will **not** do is copy a value that owns something: a copy made field by field would hand one obligation to two values, and the second to go would close the same descriptor, or free the same block, a second time. Such a type says what a copy of it means by writing `clone` itself.

**What the compiler will not copy**

```rune
struct Descriptor { @resource fd: i32 = -1 }

extend Descriptor {
    @safe("the descriptor is ours, and the flag stops a second close")
    fn deinit(&var self) {
        if self.fd >= 0 { self.fd = -1 }
    }
}

fn main() -> i64 {
    let one = Descriptor { fd: 7 }
    let two = one.$clone()
    0
}
```

> [!NOTE]
> **The mark behind `$clone`**
>
> `std::mem::Clone` is the mark that stands behind all of this, and it is automatic: a type has it when every part of it has it, and a type that owns something claims it — `bind mem::Clone to T {}` — once it has written `clone`. Write `T: mem::Clone` as a bound when a function of your own has to copy what it is given. See *Marks → Automatic marks*.
