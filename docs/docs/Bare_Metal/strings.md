# Strings

A string literal nothing asks to be a `String` is a `CString`: it can be named, stored in a table and handed to a function without an annotation, and it costs nothing at run time. A `String` is there too — the freestanding runtime makes one over the program's `#allocator` — so asking for one, adding a literal to one, `format!`, `$str()`, `std::fmt` and `Display` all work. A literal that becomes a `String` is built into the image rather than the heap, so it never takes memory from the allocator. The same holds under `--no-stdlib`.

**No annotations**

```text
let banner = "TETRIS-OS\n"        // a CString

fn greet(name: String) {
    serial::write(banner)
    let line = "ready, " + name       // a String: the literal follows `name`
    println!("{line} at {} Hz", 1193182 / 65536)
}
```

Numbers become text exactly as they do on a hosted build, byte for byte — the shortest digits that read back as the same double, found with exact big-integer arithmetic rather than a C library — and `$toFloat()` rounds correctly the same way.

> [!NOTE]
> **Single ownership**
>
> A freestanding program is built with `--memory zombie`. Every object has one owner and nothing is counted, so the heap needs nothing but an allocator; reference counting would need the hosted runtime's atomics and weak table, and is refused like any other use of it.
