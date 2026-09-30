# What single ownership does without

A `weak` field cannot tell when its target has been freed without a count, so it is an error; keep a `mem::Arena<T>` handle or an ordinary borrow instead. A library can also mark a type reference-counting-only with `@zombie_unavailable("…")`, and reaching for it under `--memory zombie` fails with the alternative spelled out.

**`weak` needs a count**

```rune
class Parent { name: String  fn init(self, n: String) { self.name = n } }
class Child {
    weak owner: Parent?
    fn init(self) { self.owner = nil }
}
fn main() -> i64 { 0 }
```

> [!NOTE]
> **When you know better**
>
> Two escape hatches exist for the code the checker cannot vouch for. `@zombie("reason")` on a function tells the checker to trust its body, the way `@safe` does for an unsafe call; its signature is still the contract callers are held to. And `unsafe { }` leaves raw pointers untracked, exactly as under reference counting.
