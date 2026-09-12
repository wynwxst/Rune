# What single ownership does without

Two things that only make sense with a count are gone. A `weak` field cannot tell when its target has been freed without one, so it is an error; keep an index or a borrow instead. And a type that exists to be shared — `thread::Arc`, `mem::retain`/`release` — is marked unavailable, with the alternative named in the message.

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

> [!WARNING]
> **Standard library**
>
> The core of the standard library is being brought over to compile under both memory models; until it is, some modules that lean on shared containers are checked but not yet clean under `--memory zombie`. The language, the checker and the code generator are complete — this is library work, tracked in the roadmap.
