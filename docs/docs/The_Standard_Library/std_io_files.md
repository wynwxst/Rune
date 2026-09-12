# std::io: files

Every file operation that can fail says so in its type, so there is no errno to remember and no return code to forget. A `File` closes itself when the last reference to it goes, so a program that never calls `close` still does not leak a descriptor.

| Member | Signature | Does |
| --- | --- | --- |
| `open` | `(path: String) -> Result<File, FileError>` | an existing file, for reading |
| `create` | `(path) -> Result<File, FileError>` | a new one, or empties an old one |
| `append` | `(path) -> Result<File, FileError>` | for writing at the end |
| `readToString` | `(path) -> Result<String, FileError>` | the whole file, in one call |
| `writeString` | `(path, text) -> Result<i64, FileError>` | the file's entire contents |
| `appendString` | `(path, text) -> Result<i64, FileError>` |  |
| `exists` | `(path) -> bool` | can it be opened? |
| `delete` | `(path) -> FileError?` | `nil` means it is gone |
| `describe` | `(e: FileError) -> String` | what went wrong, in words |

| Directories | Signature | Does |
| --- | --- | --- |
| `isDirectory` | `(path) -> bool` | false for a file, and for nothing at all |
| `readDirectory` | `(path) -> Result<Vector<DirEntry>, FileError>` | one level, in the filesystem’s own order |
| `walkDirectory` | `(path, depth: i64) -> Vector<String>` | every file underneath, as paths relative to `path` |
| `makeDirectory` | `(path) -> FileError?` | succeeds when it is already there |
| `makeDirectories` | `(path) -> FileError?` | and every missing parent above it |
| `joinPath` | `(left, right) -> String` | exactly one separator, and none for an empty side |

*A `DirEntry` is a `name` and an `isDirectory`. The name is the entry alone, not a path — `joinPath` reaches it. `.` and `..` never appear.*

**Walking a tree**

```rune
import std::io

fn main() -> i64 {
    let dir = "/tmp/rune_docs_tree/inner"
    io::makeDirectories(dir)
    io::writeString(io::joinPath(dir, "a.txt"), "a")
    io::writeString("/tmp/rune_docs_tree/b.txt", "b")

    // One level: files and folders together, told apart by `isDirectory`.
    match io::readDirectory("/tmp/rune_docs_tree") {
        Ok(entries) => {
            for e in entries {
                io::println((if e.isDirectory { "dir  " } else { "file " }) + e.name)
            }
        },
        Err(e) => io::println(io::describe(e)),
    }

    // The whole tree, files only, relative to where the walk started.
    for path in io::walkDirectory("/tmp/rune_docs_tree", 4) {
        io::println("  " + path)
    }

    io::delete(io::joinPath(dir, "a.txt"))
    io::delete("/tmp/rune_docs_tree/b.txt")
    0
}
```

> [!WARNING]
> **Order is not promised**
>
> The order `readDirectory` hands entries back in is the filesystem’s, which is neither alphabetical nor the same on two machines. Sort the result when the order matters.

| On a `File` | Signature | Does |
| --- | --- | --- |
| `readLine` | `(&var self) -> String?` | next line, newline removed; nothing at end of file |
| `readAll` | `(&var self) -> String` | everything left |
| `write` | `(&var self, text) -> Result<i64, FileError>` | bytes written |
| `writeLine` | `(&var self, text) -> Result<i64, FileError>` |  |
| `flush` | `(&var self)` | push buffered writes out |
| `close` | `(&var self)` | now rather than later; twice is harmless |
| `isOpen` | `(&self) -> bool` |  |
| `name` | `(&self) -> String` | the path it was opened under |

**Writing, then reading line by line**

```rune
import std::io

fn main() -> i64 {
    let path = "/tmp/rune_docs_example.txt"

    match io::writeString(path, "first line\nsecond line\n") {
        Ok(n) => io::println("wrote " + n.$str() + " bytes"),
        Err(e) => io::println("write failed: " + io::describe(e)),
    }

    // A line at a time, until there are none left.
    match io::open(path) {
        Ok(f) => {
            var n = 0
            while f.readLine() is Some(line) {
                n += 1
                io::println("  " + n.$str() + ": " + line)
            }
        },
        Err(e) => io::println("open failed: " + io::describe(e)),
    }

    io::delete(path)
    0
}
```

A failure is a value, so it can be answered where it happens or passed on with `?` like any other `Result`.

**`?` carries a file error out**

```rune
import std::io

/// Reads a file and counts its lines, or explains why it could not.
fn countLines(path: String) -> Result<i64, io::FileError> {
    let text = io::readToString(path)?
    var lines = 0
    var i = 0
    while i < text.$length() {
        if text.$byteAt(i) == 10 { lines += 1 }
        i += 1
    }
    Ok(lines)
}

fn main() -> i64 {
    let path = "/tmp/rune_docs_count.txt"
    io::writeString(path, "a\nb\nc\n")

    match countLines(path) {
        Ok(n) => io::println(n.$str() + " lines"),
        Err(e) => io::println("failed: " + io::describe(e)),
    }
    match countLines("/tmp/rune_docs_absent") {
        Ok(n) => io::println("unexpected"),
        Err(e) => io::println("missing: " + io::describe(e)),
    }

    io::delete(path)
    0
}
```

> [!NOTE]
> **The error cases**
>
> `FileError` names the cases worth telling apart — `NotFound`, `PermissionDenied`, `AlreadyExists`, `IsDirectory`, `TooManyOpenFiles`, `Closed` and `Other` — so a caller can match on the one it wants to handle rather than parsing a message.
