# Where a reference is borrowed from

A returned reference has to point somewhere that outlives the call. The checker works out where from on its own, from the body, so most functions say nothing. When you want the boundary written down — to pin a public interface, or where a result could come from more than one argument — a `from` clause names the place, as a plain path rather than an invented lifetime name.

**`from` names the places a result may borrow**

```rune
fn longest(a: &String, b: &String) -> &String from (a, b) {
    if a.$length() > b.$length() { a } else { b }
}
```

```ebnf
Type       ::= ... | RefType OriginClause?
OriginClause ::= 'from' ( Place | '(' Place (',' Place)* ')' )
Place      ::= ( 'self' | ident ) ( '.' ident )*   |   'global'
```

Returning a borrow of a local is refused whatever the annotation: the local is gone the moment the function returns.

**A borrow that does not outlive the call**

```rune
fn dangling() -> &i64 {
    let n = 5
    &n
}
fn main() -> i64 { *dangling() }
```

A `from` clause is not only for results. On a **parameter** it is a requirement on the caller: the argument must borrow from the named place and nowhere else. This is what keeps a value handed to a thread off the caller's own locals — see `thread::scope` — and it is checked at the call, not in the body.

**`from` on a parameter: a caller-side contract**

```rune
class Item { pub v: i64  fn init(self, v: i64) { self.v = v } }
class List {
    head: Item
    fn init(self, h: Item) { self.head = h }
    fn first(&self) -> &Item from self { &self.head }
}
// `attach` promises the caller only ever hands it something borrowed from
// `list`, so what goes in cannot outlive the list it goes into.
fn attach(list: &var List, item: &Item from list) { }
```

On a **local binding** it is an assertion the checker verifies — for documentation, or to narrow what would otherwise be inferred:

**`from` on a local binding**

```rune
class Item { pub v: i64  fn init(self, v: i64) { self.v = v } }
class List {
    head: Item
    fn init(self, h: Item) { self.head = h }
    fn first(&self) -> &Item from self { &self.head }
}
fn head(list: &List) -> &Item from list {
    let first: &Item from list = list.first()   // verified against `list`
    first
}
```

A **longer lifetime coerces to a shorter one**. A borrow of a `global` outlives every place a `from` clause could name, so it satisfies any of them: a function that promises `-> &String from a` may return a global, and an argument required `from list` may be one. The caller keeps the named place alive, which is more than a `'static` borrow ever needs — the place-based reading of `&'static T` fitting where `&'a T` was asked for. A string literal is the same case: it is interned once and never freed, so `&"text"` may be taken outright and goes wherever a borrow of a global goes.

**A longer lifetime coerces to a shorter**

```rune
global BANNER: String = "welcome"
// Promises to borrow from `a`; returning the global is accepted, because a
// global outlives `a` — and so is a literal.
fn label(a: &String) -> &String from a {
    if a.$isEmpty() { &BANNER } else { a }
}
fn labelOr(a: &String) -> &String from a {
    if a.$isEmpty() { &"(none)" } else { a }
}
```

> [!NOTE]
> **A local is still a local**
>
> The coercion is for what is immortal, not for what happens to hold it: `let h = "Hello"` is a local, which can be reassigned, so `&h` is a borrow of `h` and no more.
