# Spelling and placement

**An unrecognised decorator**

```rune
@notarealdecorator
fn f() -> i64 { 0 }
```

**`#` is only for the compiler's own**

```rune
fn route(handler: @function() -> ()) { }

#route
fn health() { }

fn main() -> i64 { 0 }
```

> [!NOTE]
> **Placement**
>
> Decorators sit on their own line above the declaration, or inline before it — both parse. One per line reads better when there are several.
