# `typeof`: the type an expression has

`typeof(expr)` is whatever type the expression would have. The expression is checked and **never run** — it is there to be asked about, not evaluated — so `typeof(boom())` costs nothing and calls nothing.

**A type read off a value**

```rune
import std::io

struct Point { x: f64, y: f64 }

fn boom() -> i64 { io::println("never printed"); 0 }

fn main() -> i64 {
    let a = 7
    let b: typeof(a) = 9
    var p: typeof(Point { x: 0.0, y: 0.0 }) = Point { x: 3.0, y: 4.0 }
    let c: typeof(boom()) = 5

    io::println(b.$str())
    io::println(p.x.$str())
    io::println(c.$str())
    0
}
```

What it is really for is a macro that has to write a signature out of the values it was handed. Without it the caller spells every type a second time and keeps the two in step by hand; with it the signature is built from the call itself.

**A signature built from the arguments**

```text
macro send {
    ($fn: expr, $recv: expr $(, $item: expr)*) => {
        ($fn as @cfunction(*var u8 $(, typeof($item))*) -> *var u8)(
            $recv $(, $item)*)
    }
}

extern "C" {
    fn objc_msgSend(id: *var u8, ...) -> *var u8
}

struct Rect { x: f64, y: f64, w: f64, h: f64 }

#unsafe fn main() -> i64 {
    let obj = 0 as *var u8
    let r = Rect { x: 1.0, y: 2.0, w: 3.0, h: 4.0 }
    // Stands for a call through
    //   @cfunction(*var u8, Rect, u64, bool) -> *var u8
    let _ = send!(objc_msgSend, obj, r, 7u64, false)
    0
}
```

> [!NOTE]
> **Contextual, not reserved**
>
> `typeof` is not a keyword. It is read this way only in a type position followed by `(`, so a function or a variable called `typeof` goes on meaning what it did.
