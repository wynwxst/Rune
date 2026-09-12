# std::io: bytes and streams

A `String` is text: UTF-8, and NUL-terminated for the C boundary. That makes it the wrong thing to read a socket into, because what arrived is whatever the other end sent. `Bytes` is a growable run of bytes, and it is what the stream marks move.

| Member | Signature | Does |
| --- | --- | --- |
| `Bytes` | `class` | a growable byte buffer over `mem::allocator` |
| `length` / `isEmpty` | `(&self)` |  |
| `at` | `(&self, index: i64) -> u8?` | nothing past the end |
| `push` | `(&var self, value: u8)` | one byte |
| `append` / `appendText` | `(&var self, Bytes)` / `(&var self, String)` | another buffer, or a string's UTF-8 |
| `slice` | `(&self, from: i64, upto: i64) -> Bytes` | a buffer of its own |
| `consume` | `(&var self, n: i64)` | drops the first `n`, moving the rest to the front |
| `find` | `(&self, needle: String, from: i64) -> i64` | where it starts, or `-1` |
| `toString` | `(&self) -> String` | the contents read as text, as they are |
| `raw` / `writePoint` / `advance` | — | the block itself, for the C boundary |
| `bytes` | `(text: String) -> Bytes` | a buffer holding it |

*`Bytes` binds `Display`, so it prints as its own text.*

Two marks describe where bytes come from and where they go, and a third is both. Everything else in the library is written against them rather than against a file or a socket, so the same code works over either.

| Member | Signature | Does |
| --- | --- | --- |
| `Reader` | `mark` | somewhere bytes come from |
| `read` | `(&var self, sink: Bytes, most: i64) -> Result<i64, StreamError>` | the one thing to supply; `0` means finished |
| `readAll` | `(&var self, sink: Bytes) -> Result<i64, …>` | everything left |
| `readExact` | `(&var self, sink: Bytes, count: i64) -> …` | exactly that many; short is a failure |
| `Writer` | `mark` | somewhere bytes go |
| `write` | `(&var self, data: Bytes) -> Result<i64, …>` | what it can; short is not a failure |
| `writeAll` / `writeText` / `writeLine` | — | every byte, however many calls that takes |
| `flush` | `(&var self) -> Result<i64, …>` | push anything buffered out |
| `Stream` | `mark Stream: Reader + Writer` | both at once — a socket, a pipe |
| `BufferedReader<R: Reader>` | `class` | a reader with a buffer in front of it, so `readLine` costs one call rather than one per byte |
| `copy` | `<R: Reader, W: Writer>(&var R, &var W) -> …` | everything from one to the other |
| `File::asStream` | `(&self) -> FileStream` | a file in the shape the marks take |

*`StreamError` is `Closed`, `WouldBlock`, `Interrupted`, `TimedOut` or `Failed`; `describeStream` puts it in words.*

> [!NOTE]
> **Why `asStream` and not `File` itself**
>
> `File` has a `write` and a `flush` of its own, taking text and reporting a `FileError`. `Stream` asks for the same two names with different shapes, and Rune has one name per method — so a file is *turned into* a stream rather than being one. `asStream` costs an object, not a copy of the file, and both views share the handle.

**Bytes, a file as a stream, and lines**

```rune
import std::io

fn main() -> i64 {
    var b = io::Bytes()
    b.appendText("hello, ")
    b.appendText("world")
    io::println(b)
    io::println(b.slice(0, 5).toString())
    io::println(b.find("world", 0))

    let path = "/tmp/rune_docs_stream.txt"
    match io::create(path) {
        Ok(f) => {
            var out = f.asStream()
            out.writeLine("alpha")
            out.writeLine("beta")
            out.flush()
        },
        Err(e) => { io::println("create failed"); return 1 },
    }

    // `BufferedReader` works over anything that reads — a file here, a
    // socket in `std::net`.
    match io::open(path) {
        Ok(f) => {
            var lines = io::BufferedReader<io::FileStream>(f.asStream())
            while lines.readLine() is Some(line) { io::println("  " + line) }
        },
        Err(e) => { io::println("open failed"); return 1 },
    }
    io::delete(path)
    0
}
```
