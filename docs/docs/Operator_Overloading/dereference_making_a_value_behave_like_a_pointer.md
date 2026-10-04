# Dereference: making a value behave like a pointer

`deref` says what `*value` produces; `derefSet` says what `*value = x` does. A type with both behaves like a pointer, including through compound assignment — `*h += 5` reads once, applies the operator, and writes once.

**`*` on a value of your own**

```rune
import std::io

struct Celsius { deg: f64 }

// The punctuation spelling; the two methods claim their own operators.
bind operator::"*" to Celsius {
    fn deref(&self) -> f64 { self.deg }
    fn derefSet(&var self, value: f64) { (*self).deg = value }
}

fn main() -> i64 {
    var c = Celsius { deg: 21.5 }
    io::println((*c).$str())
    *c = 30.0
    io::println((*c).$str())
    *c += 2.5
    io::println((*c).$str())
    0
}
```

`std::mem` binds both for `Handle<T>`, which is what makes a handle read like a pointer to the value it owns.

**A handle is a pointer**

```rune
import std::io
import std::mem

fn main() -> i64 {
    var h = mem::Handle<i64>(41)
    *h = *h + 1
    io::println((*h).$str())

    var s = mem::of("hello")
    *s += " there"
    io::println(*s)
    0
}
```

`#alias` on the block registers a further spelling for the same operator, everywhere. That is the one case where an alias is not local to the declaration it decorates.

**An operator spelling of your own**

```rune
import std::io

struct Celsius { deg: f64 }

#alias("degrees")
bind operator::"*" to Celsius {
    fn deref(&self) -> f64 { self.deg }
}

struct Kelvin { deg: f64 }

// `degrees` now names the same operator as `*` does.
bind operator::"degrees" to Kelvin {
    fn deref(&self) -> f64 { self.deg - 273.15 }
}

fn main() -> i64 {
    io::println((*Celsius { deg: 21.5 }).$str())
    io::println((*Kelvin { deg: 373.15 }).$str())
    0
}
```

**Readable through `*`, but not writable**

```rune
struct Reading { v: f64 }

bind operator::"*" to Reading {
    fn deref(&self) -> f64 { self.v }
}

fn main() -> i64 {
    var r = Reading { v: 1.0 }
    *r = 2.0
    0
}
```
