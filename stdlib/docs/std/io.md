# std::io

Where a program meets the outside world: the console, files, directories, and
the two marks — `Reader` and `Writer` — that everything byte-shaped is written
against. `println` takes anything bound to `Display`, and every builtin is.

## Printing

`print` and `println` take one printable value. Build the line with `+`, or
with `println!`, which takes a format string.

```rune
import std::io

struct Point { pub x: f64, pub y: f64 }

bind io::Display to Point {
    fn display(&self) -> String { "(" + self.x.$str() + ", " + self.y.$str() + ")" }
}

fn main() -> i64 {
    io::println("hello")
    io::println(42)
    io::println(Point { x: 1.5, y: 2.0 })
    println!("{} and {}", "text", 3)
    io::eprintln("this goes to standard error")
    0
}
```

## Reading a line

`readLine` hands back an empty string at the end of input; `readLineOrEnd`
tells the two apart.

```rune
import std::io

fn main() -> i64 {
    var lines = 0
    while io::readLineOrEnd() is Some(line) {
        lines += 1
    }
    io::println("read " + lines.$str() + " line(s)")
    0
}
```

## Reading by length

A protocol that says how long its next message is — an HTTP body, or a
language server's `Content-Length` header — needs the message by count,
newlines and all. `readExactly` gives exactly that many bytes, or `nil` when
the input ends first. `flush` pushes buffered output out, which a program
answering requests over a pipe does after every answer.

```rune
import std::io

fn main() -> i64 {
    // Run with no input, so there is nothing to read.
    match io::readExactly(5) {
        Some(body) => io::println("got " + body),
        None => io::println("the input ended first"),
    }
    io::print("answered")
    io::flush()
    io::newline()
    0
}
```

## Files

Every failure is a `FileError`, and `describe` turns one into a sentence.
`readToString` and `writeString` are the whole-file forms; `open`, `create`
and `append` give a `File` to read or write a piece at a time.

```rune
import std::io

fn main() -> i64 {
    let path = "/tmp/rune-io-example.txt"
    match io::writeString(path, "one\ntwo\nthree\n") {
        Ok(n) => io::println("wrote " + n.$str() + " bytes"),
        Err(e) => io::println(io::describe(e)),
    }

    match io::open(path) {
        Ok(f) => {
            var file = f
            while file.readLine() is Some(line) { io::println("> " + line) }
        },
        Err(e) => io::println(io::describe(e)),
    }

    io::println(io::exists(path))
    io::delete(path)
    io::println(io::exists(path))

    match io::readToString("/definitely/not/here") {
        Ok(text) => io::println(text),
        Err(e) => io::println(io::describe(e)),
    }
    0
}
```

## Directories

```rune
import std::io

fn main() -> i64 {
    let dir = "/tmp/rune-io-dir"
    io::makeDirectories(io::joinPath(dir, "nested"))
    io::writeString(io::joinPath(dir, "a.txt"), "a")
    io::writeString(io::joinPath(dir, "nested/b.txt"), "b")

    io::println(io::isDirectory(dir))
    match io::readDirectory(dir) {
        Ok(entries) => io::println(entries.length()),
        Err(e) => io::println(io::describe(e)),
    }
    io::println(io::walkDirectory(dir, 4).length())
    0
}
```

## Bytes and streams

`Bytes` is a growable byte buffer. A `File` becomes a `Stream` with
`asStream()`, and `BufferedReader` turns any reader into lines — which is how
a socket from `std::net` is read the same way a file is.

```rune
import std::io

fn main() -> i64 {
    var b = io::bytes("hello, ")
    b.appendText("world")
    io::println(b.length())
    io::println(b.toString())
    io::println(b.find("world", 0))
    0
}
```
