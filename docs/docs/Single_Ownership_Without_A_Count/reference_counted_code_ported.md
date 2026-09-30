# Reference-counted code, ported

Most reference-counted code compiles under `--memory zombie` unchanged: constructing values, calling methods, passing arguments, printing, building containers and looping over them all read exactly the same. What changes is underneath — a value that was shared is now moved — and the checker points at the one line where that matters rather than making you rewrite the rest.

Two habits answer almost everything it asks for. When you hand a value on but still need it, **borrow** it with `&` — most functions that only read already take `&T`, so a bare name auto-borrows and nothing changes at the call. When you need a second value that lives on its own, **copy** it with `$clone()` — a share under counting, an independent value under single ownership.

**The same code, either mode**

```rune
import std::io
import std::collections::vector

class Account { var balance: i64  fn init(self) { self.balance = 0 } }

fn main() -> i64 {
    var names = vector::Vector<String>()
    names.push("ada")
    names.push("grace")
    for name in names { io::println(name) }        // borrows each; `names` stays whole
    io::println(names.length().$str())             // 2 — still ours

    let a = Account()
    let b = a.$clone()                             // a second, independent account
    b.balance = 100
    io::println(a.balance.$str() + " " + b.balance.$str())   // 0 100
    0
}
```
