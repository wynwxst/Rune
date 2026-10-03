# Generic enums

An enum may take type parameters. Each set of arguments produces its own instantiation, and the variants carry the substituted types.

**One enum, several instantiations**

```rune
import std::io

pub enum Tree<T> {
    Leaf,
    Node(T),

    pub fn value(&self, fallback: T) -> T {
        match self {
            Tree::Node(v) => v.$clone(),     // a copy: `self` is only borrowed
            Tree::Leaf => fallback,
        }
    }
}

fn main() -> i64 {
    let numeric = Tree<i64>::Node(7)
    let empty = Tree<i64>::Leaf
    let textual = Tree<String>::Node("hello")

    io::println(numeric.value(0))
    io::println(empty.value(-1))
    io::println(textual.value("nothing"))
    0
}
```

> [!NOTE]
> **Spelling the arguments**
>
> Write the arguments as `Tree<i64>::Node(...)` or `Tree::<i64>::Node(...)` — both parse. When an argument determines them, as in `Tree::Node(7)` used where a `Tree<i64>` is wanted, they are inferred.
