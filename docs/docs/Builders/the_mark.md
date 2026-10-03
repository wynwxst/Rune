# The mark

`std::builder::Builder` is what a type binds to become one. Three things: what goes in, the empty one a block starts from, and how to take one more.

**std::builder**

```rune
pub mark Builder {
    /// What goes in.
    type Child

    /// The empty one, which a block starts from.
    fn empty() -> Self

    /// Takes one more. Called once per item, in the order they were written.
    fn add(&var self, child: Self::Child)
}
```

> [!NOTE]
> **Why `empty`**
>
> `empty()` rather than a default value, because a builder may be a struct or a class and the two are built differently. One line says what an empty one is, and the block never has to know.
