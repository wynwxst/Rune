# Values have destructors too

A struct or an enum may declare a `deinit`, and it runs when the value it lives in is destroyed rather than when a count reaches zero. That is what lets a value own something reference counting cannot see: a file descriptor, a lock, a handle from C. The two kinds differ only in what decides the moment — a class's runs when the last reference goes, a value's when the binding does. See [Structs](#structs) for the whole of it, and [Safety levels](#safety) for what the compiler checks.

**Both kinds, side by side**

```rune
import std::io

struct Slot { pub n: i64 }
extend Slot { fn deinit(&self) { io::println("released " + self.n.$str()) } }

class Holder {
    slot: Slot
    fn init(self) { self.slot = Slot { n: 7 } }
    fn deinit(self) { io::println("holder going") }
}

fn main() -> i64 {
    { let h = Holder() }            // the class first, then its field
    { let s = Slot { n: 1 } }       // the value on its own
    0
}
```

A value that owns something is *moved* rather than copied when it is handed on, so exactly one binding owns it at a time. A class reference is still shared as it always was; ownership of a value and counting of a reference are separate questions, and a struct that has both answers both.
