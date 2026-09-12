# `Unique`: one owner, so no ring

`weak` breaks a cycle after the fact — you have to see it coming and pick an edge to weaken. `Unique` makes one impossible instead.

A `Unique<T>` is a reference to a class instance with **exactly one owner**. It is represented like any other class reference and costs nothing extra at run time; what makes it unique is that the compiler will not duplicate it. It can be *moved*, with `.$move()`, or *borrowed*, with `&`. There is no third thing. Closing a ring needs a second reference to the same object, and nobody can produce one.

**A chain that cannot cycle**

```rune
import std::io
import std::process

class Node {
    pub value: i64
    pub next: Unique<Node>?           // this node owns the rest of the chain
    fn init(self, value: i64) { self.value = value }
    fn deinit(self) { io::println("releasing " + self.value.$str()) }
}

/// Borrowing reaches the object without becoming a second owner.
fn total(n: &Node) -> i64 {
    var sum = n.value
    match n.next {
        Some(c) => sum += total(&c),
        None => {}
    }
    sum
}

fn main() -> i64 {
    let before = process::liveObjectCount()
    {
        let head: Unique<Node> = Node(1)
        let second: Unique<Node> = Node(2)
        second.next = Node(3)          // fresh: nothing to move from
        head.next = second        // `second` is unusable from here
        io::println("total " + total(&head).$str())
    }
    // Bound before printing: calling `liveObjectCount()` inside a
    // concatenation counts the half-built string too.
    let after = process::liveObjectCount()
    io::println("balanced " + (before == after).$str())
    0
}
```

Releasing the head releases the link it owns, and so on down the chain — which is why the whole thing goes at once, and why the count comes back to where it started.

> [!WARNING]
> **Measure it outside the string**
>
> `liveObjectCount()` counts what is alive *at the instant it is called*. A string literal costs nothing — literals are interned into one immortal object each, which is never counted — but the **result** of a concatenation is a real object, so calling the count in the middle of building a longer message counts that partial result too and reads exactly like a leak. Bind the count first, as above, and compare the bindings.

> [!NOTE]
> **Where a `Unique` comes from**
>
> A fresh construction may become a `Unique` without any transfer, because nothing else refers to it yet. Everything else has to be handed over explicitly.

Four things are refused, all as `E0239`. Between them they are every way a second reference could have appeared:

**The four ways it says no**

```rune
class Node {
    pub value: i64
    pub next: Unique<Node>?
    pub sneaky: Node?
    fn init(self, value: i64) { self.value = value }
}

fn main() -> i64 {
    let a: Unique<Node> = Node(1)
    let b: Unique<Node> = Node(2)
    a.next = b
    let seen = b.value            // 'b' has been moved out of
    let c: Unique<Node> = Node(3)
    let d: Unique<Node> = c          // cannot copy into this initialiser
    let e: Node = c               // cannot copy into a counted reference
    a.sneaky = c                  // ... which is what would close the ring
    0
}
```

Returning one is a transfer, so it is written out. A chain is built from the tail forwards, because each node owns the one after it: there is no way to keep a cursor on the end and still hand the whole thing back.

**Building one, tail first**

```rune
import std::io

class Node {
    pub value: i64
    pub next: Unique<Node>?
    fn init(self, value: i64) { self.value = value }
}

fn buildChain(count: i64) -> Unique<Node> {
    var head: Unique<Node> = Node(count - 1)
    var i = count - 2
    while i >= 0 {
        let node: Unique<Node> = Node(i)
        node.next = head      // the new node takes the chain over
        head = node           // and becomes the chain
        i -= 1
    }
    head
}

fn walk(n: &Node) -> String {
    var out = n.value.$str() + " "
    match n.next {
        Some(c) => out += walk(&c),
        None => {}
    }
    out
}

fn main() -> i64 {
    let chain = buildChain(5)
    io::println(walk(&chain))
    0
}
```

> [!NOTE]
> **A moved-from name can be reused**
>
> Assigning to a local that was moved out of gives it something to hold again, which is what makes `head = node` work on the second turn of that loop.

| Rule | Why |
| --- | --- |
| `Unique` is not an explicit generic argument | `Unique<Node>?` is fine — that is `Option`, the compiler's own — but `Handle<Unique<Node>>` is refused, because a generic written for a copyable `T` would copy this one |
| move tracking is flow-insensitive | a move in one branch of an `if` marks the local moved after the `if` on every path; it refuses some valid programs, and accepts no invalid ones |
| you cannot move out of a field | the field would be left holding nothing, and only an optional field can say that — assign a replacement instead |
| `Unique` needs a class | only a class instance is counted, so only one has an owner to hand over |
