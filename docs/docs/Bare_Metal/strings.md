# Strings

With no hosted runtime there is no `String` to make, so a string literal nothing asks to be a `String` is a `CString`: it can be named, stored in a table and handed to a function without an annotation. Asking for `String` by name still means the hosted runtime's, and is refused like any other use of it. The same holds under `--no-stdlib`.

**No annotations**

```text
let banner = "TETRIS-OS\n"        // a CString

fn greet() {
    serial::write(banner)
    serial::write("ready\n")
}
```

> [!NOTE]
> **Single ownership**
>
> A freestanding program is built with `--memory zombie`. Every object has one owner and nothing is counted, so the heap needs nothing but an allocator; reference counting would need the hosted runtime's atomics and weak table, and is refused like any other use of it.
