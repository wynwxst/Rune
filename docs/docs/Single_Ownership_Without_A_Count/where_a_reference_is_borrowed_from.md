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
