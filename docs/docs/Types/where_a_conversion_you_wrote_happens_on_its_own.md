# Where a conversion you wrote happens on its own

A conversion is put in for you wherever the destination type is **written down**: an argument, an annotated binding, a field of a struct literal, a declared result, a `return`, an assignment into a typed place. Nowhere else. Nothing converts between two types that never said they convert, and `bind T into U` is the saying — so a conversion can always be found by searching for the binding that allows it.

**Five places the destination is written down**

```rune
import std::io

struct Celsius { v: f64 }
struct Fahrenheit { v: f64 }

bind Celsius into Fahrenheit {
    fn convert(&self) -> Fahrenheit { Fahrenheit { v: self.v * 1.8 + 32.0 } }
}

struct Reading { at: Fahrenheit }

fn warmer(t: Fahrenheit) -> Fahrenheit { Fahrenheit { v: t.v + 1.0 } }
fn boiling() -> Fahrenheit { return Celsius { v: 100.0 } }

fn main() -> i64 {
    let annotated: Fahrenheit = Celsius { v: 100.0 }
    let argument = warmer(Celsius { v: 0.0 })
    let field = Reading { at: Celsius { v: 20.0 } }
    var place: Fahrenheit = Fahrenheit { v: 0.0 }
    place = Celsius { v: 10.0 }

    io::println(annotated.v.$str())
    io::println(argument.v.$str())
    io::println(field.at.v.$str())
    io::println(place.v.$str())
    io::println(boiling().v.$str())
    0
}
```

The source does not have to be a type with a name. A tuple is a perfectly good thing to convert out of, which is how `(x, y)` comes to mean a point at every call site that takes one.

**Converting out of a tuple**

```rune
import std::io

struct CGPoint { x: f64, y: f64 }
struct CGSize { width: f64, height: f64 }
struct CGRect { origin: CGPoint, size: CGSize }

bind (f64, f64) into CGPoint {
    fn convert(&self) -> CGPoint { CGPoint { x: self.0, y: self.1 } }
}
bind (f64, f64) into CGSize {
    fn convert(&self) -> CGSize { CGSize { width: self.0, height: self.1 } }
}

fn area(s: CGSize) -> f64 { s.width * s.height }

fn main() -> i64 {
    let r = CGRect { origin: (100.0, 100.0), size: (500.0, 300.0) }
    io::println(r.size.width.$str())
    io::println(area((2.0, 3.0)).$str())
    0
}
```

> [!NOTE]
> **Why only `into` takes an unnamed source**
>
> A mark is always named, so a `bind` whose first type is not a name can only be the `into` form — there is nothing else it could mean. `bind` on a named type still reads as a mark, as it always did.

The source may be generic, and may be a **pointer**. A binding written once for every `*var T` lets a typed pointer go wherever C asks for bytes, with no cast at each call. Importing a file or library that declares the binding brings it along, like any other.

**Converting out of a pointer**

```rune
import std::io
import std::mem

bind<T> *var T into *var u8 {
    fn convert(&self) -> *var u8 { unsafe { self as *var u8 } }
}

extern "C" {
    fn realloc(ptr: *var u8, size: usize) -> *var u8
    fn free(ptr: *var u8)
}

#unsafe
fn main() -> i64 {
    var items = 0 as *var i64
    items = realloc(items, mem::size_of<i64>() * 4) as *var i64
    items[3] = 42
    io::println(items[3])
    free(items)
    0
}
```

> [!NOTE]
> **What `self` is**
>
> In a binding on a pointer type, `self` is the pointer — not the place it was read from. `self as *var u8` is the address it holds.

> [!NOTE]
> **No import needed**
>
> `As` is in scope everywhere — the prelude puts it there alongside `Option` and `Result`, so a binding never needs an import. It is declared in `std::convert`.
