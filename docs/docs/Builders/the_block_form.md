# The block form

`Name { ... }` whose contents are **values** rather than `field: value` pairs is a builder block. It stands for

```ebnf
{ var b = Name::empty()
  b.add(<first>)
  b.add(<second>)
  b }
```

so what a builder does is entirely up to the `add` it writes. Items are separated by a line break, a `;` or a `,`.

**A page written as its own shape**

```rune
import std::io
import std::builder
import std::collections::vector

struct Node { tag: String, text: String, style: String = "" }

extend Node {
    fn style(self, s: String) -> Node {
        Node { tag: self.tag, text: self.text, style: s }
    }
}

fn Text(t: String) -> Node { Node { tag: "text", text: t } }

struct Button { label: String = "ok" }

extend Button {
    fn style(self, s: String) -> Node {
        Node { tag: "button", text: self.label, style: s }
    }
}

struct Body { children: vector::Vector<Node> }

bind builder::Builder to Body {
    type Child = Node
    fn empty() -> Self { Body { children: vector::Vector<Node>() } }
    fn add(&var self, child: Node) { self.children.push(child) }
}

fn main() -> i64 {
    let page = Body {
        Text("Hello")
        Button {}.style("wide")
        Text("Bye").style("small")
    }
    var i = 0
    while i < page.children.length() {
        let c = page.children.at(i).unwrap()
        io::println(c.tag + " " + c.text + " [" + c.style + "]")
        i += 1
    }
    0
}
```
