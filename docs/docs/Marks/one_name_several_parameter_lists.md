# One name, several parameter lists

A `fn` may not be declared twice. A `bind` may: the same mark may be bound to one type more than once, and the versions are told apart by what they take. Together they are an **overload set**, and the call site picks by the arguments it hands over. This is the only overloading in the language, and a `bind` is the only place to write it.

**Three ways to call one name**

```rune
import std::io

struct Cart { pub total: i64 }

mark Add { fn add(&var self, item: String) }

// The version whose signature is the one the mark asked for is the one that
// answers the requirement. The others are extra ways to call the name.
bind Add to Cart {
    fn add(&var self, item: String) { io::println("item " + item) }
}
bind Add to Cart {
    fn add(&var self, cents: i64) { self.total = self.total + cents }
}
bind Add to Cart {
    fn add(&var self, item: String, cents: i64) {
        io::println(item + " " + cents.$str())
        self.total = self.total + cents
    }
}

fn main() -> i64 {
    var c = Cart { total: 0 }
    c.add("apple")
    c.add(150)
    c.add("pear", 200)
    io::println(c.total.$str())
    0
}
```

Selection is by type, not by conversion first: an exact match beats one that would need a widening, and a call that two versions accept equally well is reported rather than decided by declaration order. So is a call no version accepts — with every candidate listed.

Everything else about the call works the way it always does. A labelled argument goes to the parameter of that name, the positional ones fill what is left in order, and a parameter nothing filled has to have brought its own default — so a version with a defaulted tail is a candidate for the shorter call too.

**Labels and defaults, as usual**

```rune
import std::io

struct Line { pub width: i64 }
mark Draw { fn draw(&self, fill: Character) }

bind Draw to Line {
    fn draw(&self, fill: Character) {
        var out = ""
        for _ in 0..self.width { out += fill }
        io::println(out)
    }
}
bind Draw to Line {
    fn draw(&self, label: String, pad: Character = '.') {
        var out = label
        while out.$length() < self.width { out += pad }
        io::println(out)
    }
}

fn main() -> i64 {
    let l = Line { width: 8 }
    l.draw('-')                        // the Character version
    l.draw("name")                     // the String one, `pad` defaulted
    l.draw("name", '_')
    l.draw(pad: '*', label: "name")    // labels, in either order
    0
}
```

**Two that fit equally well**

```rune
import std::io

struct Cart { pub total: i64 }
mark Add { fn add(&var self, cents: i32) }

bind Add to Cart { fn add(&var self, cents: i32) { } }
bind Add to Cart { fn add(&var self, cents: i64) { } }

fn main() -> i64 {
    var c = Cart { total: 0 }
    small: i16 = 3
    c.add(small)          // widens to both, equally
    0
}
```

> [!NOTE]
> **What is still a clash**
>
> Two bindings that take the *same* things are still two answers to one question, and are reported as equally specific. Specificity comes first: a written-out target beats a parameterised one whatever either of them takes, so a narrower binding replaces a wider one rather than overloading against it.

Only one version answers the mark's requirement — the one whose signature matches it — and that is what a `dyn Mark` value calls and what `value::Mark.name()` reaches when nothing distinguishes the call. The rest are reachable on the concrete type, and through `value::Mark.name(...)` when the arguments say which.
