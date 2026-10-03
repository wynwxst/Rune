# Builders nest

An item is an ordinary expression, so it may itself be a block. Nothing about the outer one has to know.

**A builder whose children are built**

```rune
import std::io
import std::builder
import std::collections::vector

struct Node { text: String }
fn Text(t: String) -> Node { Node { text: t } }

struct Body { children: vector::Vector<Node> }
bind builder::Builder to Body {
    type Child = Node
    fn empty() -> Self { Body { children: vector::Vector<Node>() } }
    fn add(&var self, child: Node) { self.children.push(child) }
}

struct Panel { parts: vector::Vector<Body> }
bind builder::Builder to Panel {
    type Child = Body
    fn empty() -> Self { Panel { parts: vector::Vector<Body>() } }
    fn add(&var self, child: Body) { self.parts.push(child) }
}

fn main() -> i64 {
    let panel = Panel {
        Body { Text("one") }
        Body { Text("two"); Text("three") }
    }
    io::println(panel.parts.length().$str())
    0
}
```

> [!NOTE]
> **It is a rewrite, not a feature**
>
> The rewrite happens in the parser, before anything is checked. Everything after that point — type checking, the borrow checker, code generation — sees a block, a local and a run of calls, which is why a builder behaves the same under either memory model and costs nothing a hand-written loop would not.
